// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/uipreferences.h>
#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/drainexecutors.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QQmlContext>
#include <QQmlEngine>
#include <deque>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace {
bool Deliver(std::deque<std::function<void()>>& gui){if(gui.empty())return false;auto turn=std::move(gui.front());gui.pop_front();turn();return true;}
bool Settle(ManualRoutedExecution& transport,std::deque<std::function<void()>>& gui){for(int turn=0;turn<1024;++turn){if(!transport.queuedIdle()){if(!transport.stepNext())return false;continue;}if(Deliver(gui))continue;return true;}return false;}
UiPreferencesNamespace File(const QString& path){UiPreferencesNamespace id;id.file=path;return id;}
}
class UiPreferencesTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void loadWaitsForActualReadAndPublication();
    void acceptedWriteAndDrainSurviveQuietClose();
    void legacyFiltersAndOriginalNamespaceAreNative();
    void heldGuiFanoutIsBoundedAndOlderResultsStayStale();
    void rejectedReadAndThrowingPostAreExplicit();
    void realThreadRoundTrip();
    void scalarWritesOwnTheirValidationAndPersistedKeys_data();
    void scalarWritesOwnTheirValidationAndPersistedKeys();
    void scalarReadsRejectInvalidLegacyValues_data();
    void scalarReadsRejectInvalidLegacyValues();
    void geometryValidationKeepsTheOriginalPersistedRectangle_data();
    void geometryValidationKeepsTheOriginalPersistedRectangle();
    void failedStorageRetainsTheLastConfirmedValues();
    void filterCommandsPreserveCanonicalSelectionAndIntentKeys_data();
    void filterCommandsPreserveCanonicalSelectionAndIntentKeys();
    void parserContextAndUnconfiguredValues_data();
    void parserContextAndUnconfiguredValues();
    void capturedNamespaceKeepsItsNativeStorageIdentity();
    void missingSchedulerRejectsConstruction();
    void manualCapacityRefusals_data();
    void manualCapacityRefusals();
    void manualStartRetryAndCloseKeepOneRead();
    void manualViewCapacityAndRemovalKeepBoundedFanout();
    void manualFanoutRetiresWithItsStore_data();
    void manualFanoutRetiresWithItsStore();
    void manualOlderWriteFailureCannotReplaceTheLatestOutcome();
    void manualObserverLossKeepsOriginalEffects_data();
    void manualObserverLossKeepsOriginalEffects();
    void manualHeldOutcomesRefuseFurtherWritesWithoutEffects();
    void legacyGeometryReadbackPreservesOnlyRepresentableValues_data();
    void legacyGeometryReadbackPreservesOnlyRepresentableValues();
    void manualOlderReadFailureCannotReplaceNewerConfirmedWrite();
};
void UiPreferencesTests::loadWaitsForActualReadAndPublication(){
    QTemporaryDir directory;QVERIFY(directory.isValid());const auto path=directory.filePath("prefs.ini");
    {QSettings settings{path,QSettings::IniFormat};settings.setValue("windowWidth",1234);settings.setValue("windowHeight",777);settings.setValue("dark",false);settings.sync();}
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}},File(path)};AppPreferences view;view.configure(&store);
    store.start();QVERIFY(!store.ready());QCOMPARE(view.windowWidth(),0.0);
    QVERIFY(transport.step(execution::Lane::Core));QVERIFY(!store.ready());QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(!store.ready());
    QVERIFY(Settle(transport,gui));QVERIFY(store.ready());QCOMPARE(view.windowWidth(),1234.0);QCOMPARE(view.windowHeight(),777.0);QVERIFY(!view.dark());
    store.closeAdmission();transport.drain();QVERIFY(transport.quiescent());
}
void UiPreferencesTests::acceptedWriteAndDrainSurviveQuietClose(){
    QTemporaryDir directory;QVERIFY(directory.isValid());const auto path=directory.filePath("prefs.ini");ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}},File(path)};AppPreferences view;view.configure(&store);store.start();QVERIFY(Settle(transport,gui));
    QVERIFY(view.requestGeometry(25,35,1200,800));const auto receipt=store.lastWriteReceipt();QVERIFY(receipt);QCOMPARE(receipt->load(),UiPreferencesStore::Pending);
    transport.scheduler->closeOrdinaryAdmission();store.closeAdmission();bool drained{false};store.whenDrained([&]{drained=true;});QSignalSpy notices{&view,&AppPreferences::changed};
    QVERIFY(!drained);QVERIFY(transport.step(execution::Lane::Core));QCOMPARE(receipt->load(),UiPreferencesStore::Applied);QVERIFY(!drained);
    QVERIFY(Settle(transport,gui));QVERIFY(drained);QCOMPARE(notices.size(),0);
    QSettings settings{path,QSettings::IniFormat};QCOMPARE(settings.value("windowX").toInt(),25);QCOMPARE(settings.value("windowHeight").toInt(),800);
    transport.drain();QVERIFY(transport.quiescent());
}
void UiPreferencesTests::legacyFiltersAndOriginalNamespaceAreNative(){
    QTemporaryDir directory;QVERIFY(directory.isValid());const auto pre=directory.filePath("pre.ini"),chain=directory.filePath("chain.ini");
    {QSettings settings{pre,QSettings::IniFormat};settings.setValue("peerNetworkFilters","ipv4,garbage,onion,ipv4");settings.setValue("peerTransportFilters","v1,v2");settings.setValue("peerDirectionFilters","inbound,outbound");settings.setValue("unrelated",42);settings.sync();}
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;auto services=execution::SnapshotServices{transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}};
    UiPreferencesStore first{services,File(pre)};AppPreferences one;one.configure(&first);first.start();QVERIFY(Settle(transport,gui));
    QCOMPARE(one.peerNetworkFilters(),(QStringList{"ipv4","onion"}));QVERIFY(one.peerTransportFilters().isEmpty());QCOMPARE(one.peerDirectionFilters(),QStringList{"inbound"});
    one.set_dark(false);const auto receipt=first.lastWriteReceipt();first.closeAdmission();QVERIFY(Settle(transport,gui));QCOMPARE(receipt->load(),UiPreferencesStore::Applied);
    UiPreferencesStore second{services,File(chain)};AppPreferences two;two.configure(&second);second.start();QVERIFY(Settle(transport,gui));QVERIFY(two.dark());
    QSettings original{pre,QSettings::IniFormat};QVERIFY(!original.value("dark").toBool());QCOMPARE(original.value("unrelated").toInt(),42);
    second.closeAdmission();transport.drain();QVERIFY(transport.quiescent());
}
void UiPreferencesTests::heldGuiFanoutIsBoundedAndOlderResultsStayStale(){
    QTemporaryDir directory;QVERIFY(directory.isValid());ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}},File(directory.filePath("prefs.ini"))};store.start();QVERIFY(Settle(transport,gui));
    std::vector<std::unique_ptr<AppPreferences>> views;int notices{0};for(int row=0;row<65;++row){auto view=std::make_unique<AppPreferences>();view->configure(&store);connect(view.get(),&AppPreferences::changed,this,[&]{++notices;});views.push_back(std::move(view));}
    views[0]->set_dark(false);transport.drain();QVERIFY(Deliver(gui));QCOMPARE(notices,32); // One bounded notification turn, before result publication.
    QVERIFY(Deliver(gui)); // Native immutable result publication.
    QVERIFY(Settle(transport,gui));QVERIFY(!views.back()->dark());
    notices=0;views[0]->set_dark(true);views[0]->set_dark(false);transport.drain();
    // Deliver actual outcome posts in reverse order; Native state/revision, not
    // GUI arrival order, owns the final cached preference.
    auto newest=std::move(gui.back());gui.pop_back();newest();QVERIFY(Settle(transport,gui));QVERIFY(!views[0]->dark());QVERIFY(!store.pending());
    store.closeAdmission();transport.drain();QVERIFY(transport.quiescent());
}
void UiPreferencesTests::rejectedReadAndThrowingPostAreExplicit(){
    QTemporaryDir directory;QVERIFY(directory.isValid());
    {ManualRoutedExecution transport;std::deque<std::function<void()>> gui;UiPreferencesStore store{{transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}},File(directory.filePath("reject.ini"))};transport.scheduler->closeOrdinaryAdmission();store.start();QVERIFY(!store.ready());QVERIFY(!store.error().isEmpty());store.closeAdmission();transport.drain();}
    {ManualRoutedExecution transport;UiPreferencesStore store{{transport.scheduler,[](auto){throw std::runtime_error("primary presentation rejected");}},File(directory.filePath("fallback.ini"))};store.start();transport.drain();QTRY_VERIFY(store.ready());store.closeAdmission();transport.drain();QVERIFY(transport.quiescent());}
}
void UiPreferencesTests::realThreadRoundTrip(){
    QTemporaryDir directory;QVERIFY(directory.isValid());BackendExecutor executor{execution::Limits::Bounded()};
    UiPreferencesStore store{{executor.scheduler(),presentation::QueuedPost()},File(directory.filePath("real.ini"))};AppPreferences view;view.configure(&store);store.start();QTRY_VERIFY(store.ready());
    view.set_activityGrouping("day");const auto receipt=store.lastWriteReceipt();QVERIFY(receipt);QTRY_COMPARE(receipt->load(),UiPreferencesStore::Applied);QTRY_COMPARE(view.activityGrouping(),QStringLiteral("day"));
    bool drained{false};store.whenDrained([&]{drained=true;});QTRY_VERIFY(drained);executor.shutdown();
    QEventLoop loop;bool retired{false};executor.whenDrained(&loop,[&]{retired=true;loop.quit();});while(!retired)loop.exec();
}

void UiPreferencesTests::scalarWritesOwnTheirValidationAndPersistedKeys_data()
{
    QTest::addColumn<QByteArray>("key");
    QTest::addColumn<QVariant>("input");
    QTest::addColumn<bool>("valid");
    const auto row = [](const char* name, const char* key, QVariant input, bool valid = true) {
        QTest::newRow(name) << QByteArray{key} << input << valid;
    };
    row("dark-off", "dark", false); row("dark-on", "dark", true);
    row("clock-fraction", "blockclocksize", 0.2); row("clock-maximum", "blockclocksize", 1.0);
    row("clock-zero", "blockclocksize", 0.0, false); row("clock-negative", "blockclocksize", -0.1, false);
    row("clock-too-large", "blockclocksize", 1.001, false);
    row("clock-nan", "blockclocksize", std::numeric_limits<double>::quiet_NaN(), false);
    row("clock-infinite", "blockclocksize", std::numeric_limits<double>::infinity(), false);
    row("group-day", "activityGrouping", QString{"day"}); row("group-month", "activityGrouping", QString{"month"});
    row("group-invalid", "activityGrouping", QString{"week"}, false);
    row("group-over-budget", "activityGrouping", QString(65, QLatin1Char('x')), false);
    row("density-compact", "activityDisplayDensity", QString{"compact"});
    row("density-comfortable", "activityDisplayDensity", QString{"comfortable"});
    row("density-invalid", "activityDisplayDensity", QString{"dense"}, false);
    row("density-over-budget", "activityDisplayDensity", QString(65, QLatin1Char('x')), false);
    for (const int scale : {300, 3600, 43200, 86400, 0, -1, 600}) {
        const QByteArray name = QByteArray("scale-") + QByteArray::number(scale);
        row(name.constData(), "trafficGraphScale", scale, scale >= 300 && scale != 600);
    }
    for (const auto* sort : {"nodeId", "address", "connectionType", "subversion", "sent", "received"}) {
        row(sort, "peerListSortBy", QString::fromLatin1(sort));
    }
    row("sort-invalid", "peerListSortBy", QString{"unknown"}, false);
    row("sort-over-budget", "peerListSortBy", QString(65, QLatin1Char('x')), false);
    row("sort-descending", "peerListSortAscending", false); row("sort-ascending", "peerListSortAscending", true);
}

void UiPreferencesTests::scalarWritesOwnTheirValidationAndPersistedKeys()
{
    QFETCH(QByteArray, key); QFETCH(QVariant, input); QFETCH(bool, valid);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("prefs.ini");
    AppPreferences view;
    const auto before = view.property(key.constData());
    QVERIFY(before.isValid());
    QVERIFY(view.setProperty(key.constData(), input));
    QCOMPARE(view.property(key.constData()), before);
    QVERIFY(!view.ready()); QVERIFY(!view.pending()); QVERIFY(!view.error().isEmpty());
    {
        QSettings settings{path, QSettings::IniFormat};
        settings.setValue(QString::fromLatin1(key), before); settings.setValue("unrelated", 42); settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(path)};
    const auto cleanup = qScopeGuard([&] { store.closeAdmission(); transport.drain(); Settle(transport, gui); });
    view.configure(nullptr); view.componentComplete();
    view.configure(&store); view.configure(&store); store.start(); QVERIFY(Settle(transport, gui)); QVERIFY(store.ready());
    const auto previous_operation = store.operationId();
    QVERIFY(view.setProperty(key.constData(), input));
    QCOMPARE(view.property(key.constData()), before);
    const bool immediate_refusal = input.userType() == QMetaType::QString && input.toString().size() > 64;
    const auto receipt = store.lastWriteReceipt();
    if (immediate_refusal) {
        QVERIFY(!receipt); QVERIFY(!store.pending()); QCOMPARE(store.operationId(), previous_operation);
    } else {
        QVERIFY(receipt); QCOMPARE(receipt->load(), UiPreferencesStore::Pending); QVERIFY(store.pending());
        QCOMPARE(store.operationId(), previous_operation + 1);
        transport.drain();
        QCOMPARE(receipt->load(), valid ? UiPreferencesStore::Applied : UiPreferencesStore::Failed);
        // Native persistence and Domain consumption do not publish GUI values.
        QCOMPARE(view.property(key.constData()), before);
    }
    {
        QSettings persisted{path, QSettings::IniFormat}; persisted.sync();
        QCOMPARE(persisted.value(QString::fromLatin1(key)), valid ? input : before);
        QCOMPARE(persisted.value("unrelated").toInt(), 42);
    }
    QVERIFY(Settle(transport, gui)); QVERIFY(!store.pending());
    QCOMPARE(view.property(key.constData()), valid ? input : before);
    QCOMPARE(store.error().isEmpty(), valid);
    store.closeAdmission(); transport.drain(); QVERIFY(transport.quiescent());
}

void UiPreferencesTests::scalarReadsRejectInvalidLegacyValues_data()
{
    scalarWritesOwnTheirValidationAndPersistedKeys_data();
}

void UiPreferencesTests::scalarReadsRejectInvalidLegacyValues()
{
    QFETCH(QByteArray, key); QFETCH(QVariant, input); QFETCH(bool, valid);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("legacy.ini");
    { QSettings settings{path, QSettings::IniFormat}; settings.setValue(QString::fromLatin1(key), input); settings.sync(); }
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(path)};
    const auto cleanup = qScopeGuard([&] { store.closeAdmission(); transport.drain(); Settle(transport, gui); });
    AppPreferences view; const auto fallback = view.property(key.constData());
    view.configure(&store); store.start(); QVERIFY(Settle(transport, gui));
    QVERIFY(store.ready()); QVERIFY(store.error().isEmpty());
    QCOMPARE(view.property(key.constData()), valid ? input : fallback);
    // Reading an invalid legacy value does not silently rewrite the user's file.
    QSettings persisted{path, QSettings::IniFormat};
    if (key == "blockclocksize" && std::isnan(input.toDouble())) QVERIFY(std::isnan(persisted.value(QString::fromLatin1(key)).toDouble()));
    else QCOMPARE(persisted.value(QString::fromLatin1(key)), input);
    store.closeAdmission(); transport.drain(); QVERIFY(transport.quiescent());
}

void UiPreferencesTests::geometryValidationKeepsTheOriginalPersistedRectangle_data()
{
    QTest::addColumn<double>("x"); QTest::addColumn<double>("y");
    QTest::addColumn<double>("width"); QTest::addColumn<double>("height"); QTest::addColumn<bool>("valid");
    const auto row = [](const QByteArray& name, const std::array<double, 4>& rectangle, bool valid) {
        QTest::newRow(name.constData()) << rectangle[0] << rectangle[1] << rectangle[2] << rectangle[3] << valid;
    };
    const double low = std::numeric_limits<int>::min(), high = std::numeric_limits<int>::max();
    row("ordinary", {25, 35, 1200, 800}, true);
    row("integer-boundaries", {low, high, high, 1}, true);
    row("fractional", {-0.5, 1.25, 1200.5, 800.25}, true);
    for (int axis = 0; axis < 4; ++axis) {
        int fault{0};
        for (const double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
            std::array<double, 4> rectangle{25, 35, 1200, 800}; rectangle[axis] = invalid;
            row(QByteArray("nonfinite-") + QByteArray::number(axis) + '-' + QByteArray::number(fault++), rectangle, false);
        }
        for (const double invalid : axis < 2 ? std::array<double, 3>{low - 1, high + 1, low - 0.5}
                                             : std::array<double, 3>{0, -1, high + 1}) {
            std::array<double, 4> rectangle{25, 35, 1200, 800}; rectangle[axis] = invalid;
            row(QByteArray("range-") + QByteArray::number(axis) + '-' + QByteArray::number(fault++), rectangle, false);
        }
    }
}

void UiPreferencesTests::geometryValidationKeepsTheOriginalPersistedRectangle()
{
    QFETCH(double, x); QFETCH(double, y); QFETCH(double, width); QFETCH(double, height); QFETCH(bool, valid);
    QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("geometry.ini");
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(path)};
    const auto cleanup = qScopeGuard([&] { store.closeAdmission(); transport.drain(); Settle(transport, gui); });
    AppPreferences view; QCOMPARE(view.requestGeometry(x, y, width, height), quint64{0});
    view.configure(&store); store.start(); QVERIFY(Settle(transport, gui));
    QVERIFY(view.requestGeometry(10, 20, 640, 480)); QVERIFY(Settle(transport, gui));
    const auto ticket = view.requestGeometry(x, y, width, height); QVERIFY(ticket);
    const auto receipt = store.lastWriteReceipt(); QVERIFY(receipt); QCOMPARE(receipt->load(), UiPreferencesStore::Pending);
    transport.drain(); QCOMPARE(receipt->load(), valid ? UiPreferencesStore::Applied : UiPreferencesStore::Failed);
    QCOMPARE(view.windowX(), 10.0); QCOMPARE(view.windowWidth(), 640.0);
    {
        QSettings persisted{path, QSettings::IniFormat};
        QCOMPARE(persisted.value("windowX").toDouble(), valid ? x : 10.0);
        QCOMPARE(persisted.value("windowY").toDouble(), valid ? y : 20.0);
        QCOMPARE(persisted.value("windowWidth").toDouble(), valid ? width : 640.0);
        QCOMPARE(persisted.value("windowHeight").toDouble(), valid ? height : 480.0);
    }
    QVERIFY(Settle(transport, gui)); QVERIFY(!store.pending()); QCOMPARE(store.error().isEmpty(), valid);
    QCOMPARE(view.windowX(), valid ? x : 10.0); QCOMPARE(view.windowY(), valid ? y : 20.0);
    QCOMPARE(view.windowWidth(), valid ? width : 640.0); QCOMPARE(view.windowHeight(), valid ? height : 480.0);
    store.closeAdmission(); transport.drain(); QVERIFY(transport.quiescent());
}

void UiPreferencesTests::failedStorageRetainsTheLastConfirmedValues()
{
    QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("blocked.ini");
    QVERIFY(QDir().mkdir(path));
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(path)};
    const auto cleanup = qScopeGuard([&] { store.closeAdmission(); transport.drain(); Settle(transport, gui); });
    AppPreferences view; view.configure(&store); store.start(); QVERIFY(Settle(transport, gui));
    QVERIFY(!store.ready()); QVERIFY(!store.error().isEmpty());
    QVERIFY(QDir().rmdir(path)); store.retry(); QVERIFY(Settle(transport, gui));
    QVERIFY(store.ready()); QVERIFY(store.error().isEmpty()); QVERIFY(view.dark());
    view.set_dark(false); QVERIFY(Settle(transport, gui)); QVERIFY(!view.dark());
    QVERIFY(QFile::remove(path)); QVERIFY(QDir().mkdir(path));
    view.set_dark(true); const auto receipt = store.lastWriteReceipt(); QVERIFY(receipt);
    transport.drain(); QCOMPARE(receipt->load(), UiPreferencesStore::Uncertain);
    QVERIFY(!view.dark()); QVERIFY(Settle(transport, gui)); QVERIFY(!view.dark());
    QVERIFY(!store.pending()); QVERIFY(store.error().contains("uncertain")); QVERIFY(QFileInfo(path).isDir());
    store.closeAdmission(); transport.drain(); QVERIFY(transport.quiescent());
}

void UiPreferencesTests::filterCommandsPreserveCanonicalSelectionAndIntentKeys_data()
{
    QTest::addColumn<QString>("group"); QTest::addColumn<QStringList>("choices");
    QTest::addColumn<QByteArray>("key"); QTest::addColumn<int>("fault");
    QTest::newRow("direction") << QString{"direction"} << QStringList{"inbound", "outbound"} << QByteArray{"peerDirectionFilters"} << 0;
    QTest::newRow("connection") << QString{"connectionType"} << QStringList{"full-relay", "block-relay", "manual"} << QByteArray{"peerConnectionTypeFilters"} << 0;
    QTest::newRow("network") << QString{"network"} << QStringList{"ipv4", "ipv6", "onion", "i2p"} << QByteArray{"peerNetworkFilters"} << 0;
    QTest::newRow("transport") << QString{"transport"} << QStringList{"v1", "v2"} << QByteArray{"peerTransportFilters"} << 0;
    for (int fault = 1; fault <= 6; ++fault) {
        const QByteArray name = QByteArray{"invalid-"} + QByteArray::number(fault);
        QTest::newRow(name.constData()) << QString{"network"} << QStringList{"ipv4", "ipv6", "onion", "i2p"} << QByteArray{"peerNetworkFilters"} << fault;
    }
}

void UiPreferencesTests::filterCommandsPreserveCanonicalSelectionAndIntentKeys()
{
    QFETCH(QString, group); QFETCH(QStringList, choices); QFETCH(QByteArray, key); QFETCH(int, fault);
    QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("filters.ini");
    { QSettings persisted{path, QSettings::IniFormat}; persisted.setValue("unrelated", 42); persisted.sync(); }
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(path)};
    const auto cleanup = qScopeGuard([&] { store.closeAdmission(); transport.drain(); Settle(transport, gui); });
    AppPreferences view;
    QCOMPARE(view.togglePeerFilter(group, choices.front()), quint64{0});
    QCOMPARE(view.clearPeerFilter(group), quint64{0}); QCOMPARE(view.clearPeerFilters(), quint64{0});
    view.configure(&store); store.start(); QVERIFY(Settle(transport, gui));
    QVERIFY(view.togglePeerFilter(group, choices.front())); QVERIFY(Settle(transport, gui));
    QCOMPARE(view.property(key.constData()).toStringList(), QStringList{choices.front()});
    if (fault != 0) {
        const auto previous = store.operationId();
        const QString bad_group = fault == 1 || fault == 4 ? QString{"unknown"}
            : fault == 2 || fault == 5 ? QString(33, QLatin1Char('x')) : group;
        const QString bad_id = fault == 3 ? QString{"invalid"} : fault == 6 ? QString(33, QLatin1Char('x')) : choices.front();
        const auto ticket = fault == 4 || fault == 5 ? view.clearPeerFilter(bad_group) : view.togglePeerFilter(bad_group, bad_id);
        QCOMPARE(bool(ticket), fault == 3);
        if (fault == 3) {
            const auto receipt = store.lastWriteReceipt(); QVERIFY(receipt); transport.drain();
            QCOMPARE(receipt->load(), UiPreferencesStore::Failed);
        } else QCOMPARE(store.operationId(), previous);
        QVERIFY(Settle(transport, gui)); QVERIFY(!store.error().isEmpty());
        QCOMPARE(view.property(key.constData()).toStringList(), QStringList{choices.front()});
    } else {
        QVERIFY(view.togglePeerFilter(group, choices.front())); QVERIFY(Settle(transport, gui));
        QVERIFY(view.property(key.constData()).toStringList().isEmpty());
        for (const auto& choice : choices) { QVERIFY(view.togglePeerFilter(group, choice)); QVERIFY(Settle(transport, gui)); }
        // Direction is exclusive. Selecting every other group's choices is
        // canonically the unfiltered state, rather than a redundant full list.
        QCOMPARE(view.property(key.constData()).toStringList(), group == "direction" ? QStringList{choices.back()} : QStringList{});
        QVERIFY(view.clearPeerFilter(group)); QVERIFY(Settle(transport, gui));
        QVERIFY(view.property(key.constData()).toStringList().isEmpty());
    }
    {
        QSettings persisted{path, QSettings::IniFormat};
        QCOMPARE(persisted.value(QString::fromLatin1(key)).toString(), fault ? choices.front() : QString{});
        QCOMPARE(persisted.value("unrelated").toInt(), 42);
    }
    QVERIFY(view.clearPeerFilters()); QVERIFY(Settle(transport, gui));
    QVERIFY(view.peerDirectionFilters().isEmpty()); QVERIFY(view.peerConnectionTypeFilters().isEmpty());
    QVERIFY(view.peerNetworkFilters().isEmpty()); QVERIFY(view.peerTransportFilters().isEmpty());
    {
        QSettings persisted{path, QSettings::IniFormat};
        for (const auto* filter_key : {"peerDirectionFilters", "peerConnectionTypeFilters", "peerNetworkFilters", "peerTransportFilters"})
            QVERIFY(persisted.value(filter_key).toString().isEmpty());
        QCOMPARE(persisted.value("unrelated").toInt(), 42);
    }
    const auto original_id = store.operationId(); store.closeAdmission();
    QCOMPARE(view.togglePeerFilter(group, choices.front()), quint64{0});
    QCOMPARE(view.clearPeerFilter(group), quint64{0}); QCOMPARE(view.clearPeerFilters(), quint64{0});
    QCOMPARE(store.operationId(), original_id);
    AppPreferences late; late.configure(&store); QVERIFY(!late.ready()); QVERIFY(!late.error().isEmpty());
    transport.drain(); QVERIFY(transport.quiescent());
}
void UiPreferencesTests::parserContextAndUnconfiguredValues_data()
{
    QTest::addColumn<int>("context_kind");
    QTest::newRow("no-context") << 0;
    QTest::newRow("foreign-object") << 1;
    QTest::newRow("preferences-store") << 2;
}

void UiPreferencesTests::parserContextAndUnconfiguredValues()
{
    QFETCH(int, context_kind);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(directory.filePath("parser.ini"))};
    const auto cleanup = qScopeGuard([&] { store.closeAdmission(); transport.drain(); Settle(transport, gui); });
    QQmlEngine engine; QQmlContext context{engine.rootContext()}; QObject foreign;
    AppPreferences view; QSignalSpy changed{&view, &AppPreferences::changed};
    view.classBegin(); view.configure(nullptr); view.retry();
    QVERIFY(!view.ready()); QVERIFY(!view.pending()); QVERIFY(!view.error().isEmpty());
    QVERIFY(view.dark()); QCOMPARE(view.blockclocksize(), 5.0 / 12.0);
    QCOMPARE(view.windowX(), 0.0); QCOMPARE(view.windowY(), 0.0);
    QCOMPARE(view.windowWidth(), 0.0); QCOMPARE(view.windowHeight(), 0.0);
    QCOMPARE(view.activityGrouping(), QStringLiteral("month"));
    QCOMPARE(view.activityDisplayDensity(), QStringLiteral("comfortable"));
    QCOMPARE(view.trafficGraphScale(), 300); QCOMPARE(view.peerListSortBy(), QStringLiteral("nodeId"));
    QVERIFY(view.peerListSortAscending()); QVERIFY(view.peerDirectionFilters().isEmpty());
    QVERIFY(view.peerConnectionTypeFilters().isEmpty()); QVERIFY(view.peerNetworkFilters().isEmpty());
    QVERIFY(view.peerTransportFilters().isEmpty());
    view.set_dark(false); view.set_blockclocksize(0.5); view.set_activityGrouping(QStringLiteral("day"));
    view.set_activityDisplayDensity(QStringLiteral("compact")); view.set_trafficGraphScale(3600);
    view.set_peerListSortBy(QStringLiteral("sent")); view.set_peerListSortAscending(false);
    QCOMPARE(view.requestGeometry(1, 2, 640, 480), quint64{0});
    QCOMPARE(store.operationId(), quint64{0}); QVERIFY(transport.queuedIdle());
    if (context_kind) {
        context.setContextProperty(QStringLiteral("uiPreferencesStore"), context_kind == 2 ? static_cast<QObject*>(&store) : &foreign);
        QQmlEngine::setContextForObject(&view, &context);
    }
    view.componentComplete();
    QCOMPARE(changed.size(), context_kind == 2 ? 1 : 0);
    if (context_kind == 2) {
        view.configure(&store); QCOMPARE(changed.size(), 1);
        view.retry(); QVERIFY(view.pending()); QVERIFY(Settle(transport, gui));
        QVERIFY(view.ready()); QVERIFY(!view.pending()); QVERIFY(view.error().isEmpty());
    } else {
        view.retry(); QVERIFY(!view.ready()); QVERIFY(!view.pending());
        QCOMPARE(view.error(), QStringLiteral("Preferences store is unavailable."));
        QCOMPARE(store.operationId(), quint64{0});
    }
}

void UiPreferencesTests::capturedNamespaceKeepsItsNativeStorageIdentity()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString original_org = QCoreApplication::organizationName(), original_app = QCoreApplication::applicationName();
    const QString original_domain = QCoreApplication::organizationDomain();
    const auto original_format = QSettings::defaultFormat();
    const auto restore = qScopeGuard([&] {
        QCoreApplication::setOrganizationName(original_org); QCoreApplication::setOrganizationDomain(original_domain);
        QCoreApplication::setApplicationName(original_app); QSettings::setDefaultFormat(original_format);
    });
    const QString identity = QStringLiteral("UiPreferences-") + QFileInfo(directory.path()).fileName();
    QCoreApplication::setOrganizationName(identity); QCoreApplication::setOrganizationDomain(identity + QStringLiteral(".example"));
    QCoreApplication::setApplicationName(QStringLiteral("original")); QSettings::setDefaultFormat(QSettings::IniFormat);
    const auto captured = UiPreferencesNamespace::Capture();
#ifdef Q_OS_MACOS
    QCOMPARE(captured.organization, identity + QStringLiteral(".example"));
#else
    QCOMPARE(captured.organization, identity);
#endif
    QCOMPARE(captured.application, QStringLiteral("original")); QCOMPARE(captured.format, QSettings::IniFormat);
    QVERIFY(captured.file.isEmpty());
    QString path;
    {
        QSettings persisted{captured.format, captured.scope, captured.organization, captured.application};
        path = persisted.fileName(); persisted.setValue("dark", false); persisted.setValue("unrelated", 91); persisted.sync();
        QCOMPARE(persisted.status(), QSettings::NoError);
    }
    const auto remove_file = qScopeGuard([&] { QFile::remove(path); QDir().rmdir(QFileInfo(path).absolutePath()); });
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, captured};
    const auto cleanup = qScopeGuard([&] { store.closeAdmission(); transport.drain(); Settle(transport, gui); });
    AppPreferences view; view.configure(&store);
    QCoreApplication::setOrganizationName(identity + QStringLiteral("-later"));
    QCoreApplication::setOrganizationDomain({}); QCoreApplication::setApplicationName(QStringLiteral("later"));
    store.start(); QVERIFY(Settle(transport, gui)); QVERIFY(view.ready()); QVERIFY(!view.dark());
    view.set_dark(true); const auto receipt = store.lastWriteReceipt(); QVERIFY(receipt);
    transport.drain(); QCOMPARE(receipt->load(), UiPreferencesStore::Applied); QVERIFY(!view.dark());
    QVERIFY(Settle(transport, gui)); QVERIFY(view.dark());
    {
        QSettings persisted{captured.format, captured.scope, captured.organization, captured.application};
        QVERIFY(persisted.value("dark").toBool()); QCOMPARE(persisted.value("unrelated").toInt(), 91);
        QSettings later{QSettings::IniFormat, captured.scope, QCoreApplication::organizationName(), QCoreApplication::applicationName()};
        QVERIFY(!QFileInfo::exists(later.fileName()));
    }
    QCoreApplication::setOrganizationDomain({});
    QCOMPARE(UiPreferencesNamespace::Capture().organization, QCoreApplication::organizationName());
}

void UiPreferencesTests::missingSchedulerRejectsConstruction()
{
    const auto construct = [] { UiPreferencesStore store{{}, {}}; };
    bool rejected{false};
    try { construct(); }
    catch (const std::invalid_argument&) { rejected = true; }
    QVERIFY(rejected);
}

void UiPreferencesTests::manualCapacityRefusals_data()
{
    QTest::addColumn<int>("capacity");
    QTest::newRow("native-owner") << 0;
    QTest::newRow("read-result") << 1;
    QTest::newRow("write-result") << 2;
    QTest::newRow("write-action") << 3;
}

void UiPreferencesTests::manualCapacityRefusals()
{
    QFETCH(int, capacity);
    QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("capacity.ini");
    auto limits = execution::Limits::Bounded();
    if (capacity < 2) limits.cleanup_reservations.tasks = static_cast<size_t>(capacity);
    else if (capacity == 2) limits.cleanup_reservations.tasks = 3;
    else limits.action_scopes.tasks = 1;
    ManualRoutedExecution transport{limits}; std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(path)};
    const auto cleanup = qScopeGuard([&] { store.closeAdmission(); transport.drain(); Settle(transport, gui); });
    AppPreferences view; view.configure(&store); store.start(); QVERIFY(Settle(transport, gui));
    if (capacity < 2) {
        QVERIFY(!store.ready()); QVERIFY(!store.pending());
        QCOMPARE(store.error(), capacity == 0 ? QStringLiteral("Preferences storage capacity is unavailable.")
                                             : QStringLiteral("Preferences result capacity is unavailable."));
        store.retry(); QVERIFY(Settle(transport, gui)); QVERIFY(!store.ready());
        const auto previous = store.operationId();
        QCOMPARE(view.requestGeometry(1, 2, 640, 480), quint64{0}); QCOMPARE(store.operationId(), previous);
    } else {
        QVERIFY(store.ready()); QVERIFY(store.error().isEmpty());
        std::shared_ptr<execution::Scheduler> held;
        std::shared_ptr<int> held_result_capacity;
        if (capacity == 3) { held = transport.scheduler->openActionScope(); QVERIFY(held); }
        else { held_result_capacity = execution::RetainSnapshot<int>(transport.scheduler, [] { return std::make_unique<int>(1); }); QVERIFY(held_result_capacity); }
        const auto previous = store.operationId();
        QCOMPARE(view.requestGeometry(1, 2, 640, 480), quint64{0});
        QCOMPARE(store.operationId(), previous); QVERIFY(!store.pending()); QVERIFY(!store.lastWriteReceipt());
        QCOMPARE(store.error(), capacity == 2 ? QStringLiteral("Preferences result capacity is unavailable.")
                                             : QStringLiteral("Preferences write could not be admitted."));
        held.reset(); held_result_capacity.reset(); QVERIFY(Settle(transport, gui));
    }
    QSettings persisted{path, QSettings::IniFormat}; QVERIFY(!persisted.contains("windowX"));
    QCOMPARE(view.windowWidth(), 0.0);
}

void UiPreferencesTests::manualStartRetryAndCloseKeepOneRead()
{
    QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("retry.ini");
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(path)};
    const auto cleanup = qScopeGuard([&] { store.closeAdmission(); transport.drain(); Settle(transport, gui); });
    AppPreferences view; view.configure(&store);
    store.start(); store.start(); store.retry(); QCOMPARE(store.operationId(), quint64{1});
    QVERIFY(view.pending()); QVERIFY(!view.ready()); QVERIFY(Settle(transport, gui));
    store.start(); QCOMPARE(store.operationId(), quint64{1}); QVERIFY(!store.pending());
    { QSettings persisted{path, QSettings::IniFormat}; persisted.setValue("dark", false); persisted.sync(); }
    view.retry(); store.retry(); QCOMPARE(store.operationId(), quint64{2}); QVERIFY(view.pending()); QVERIFY(view.dark());
    transport.drain(); QVERIFY(view.dark()); QVERIFY(Settle(transport, gui)); QVERIFY(!view.dark());
    const auto previous = store.operationId(); store.closeAdmission(); QSignalSpy notices{&view, &AppPreferences::changed};
    store.start(); store.retry(); view.retry(); view.set_dark(true);
    QCOMPARE(view.requestGeometry(1, 2, 640, 480), quint64{0});
    QCOMPARE(view.togglePeerFilter(QString(33, QChar{'x'}), {}), quint64{0});
    QCOMPARE(store.operationId(), previous); QVERIFY(Settle(transport, gui)); QCOMPARE(notices.size(), 0);
    QSettings persisted{path, QSettings::IniFormat}; QVERIFY(!persisted.value("dark").toBool());
}

void UiPreferencesTests::manualViewCapacityAndRemovalKeepBoundedFanout()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(directory.filePath("views.ini"))};
    const auto cleanup = qScopeGuard([&] { store.closeAdmission(); transport.drain(); Settle(transport, gui); });
    store.start(); QVERIFY(Settle(transport, gui));
    std::vector<std::unique_ptr<AppPreferences>> views; int notices{0};
    for (int row = 0; row < 128; ++row) {
        auto view = std::make_unique<AppPreferences>(); view->configure(&store); QVERIFY(view->ready());
        connect(view.get(), &AppPreferences::changed, this, [&] { ++notices; }); views.push_back(std::move(view));
    }
    AppPreferences overflow; overflow.configure(&store); QVERIFY(!overflow.ready()); QVERIFY(!overflow.pending());
    QCOMPARE(overflow.error(), QStringLiteral("Preferences view capacity is unavailable."));
    views[40].reset();
    AppPreferences replacement; replacement.configure(&store); QVERIFY(replacement.ready());
    connect(&replacement, &AppPreferences::changed, this, [&] { ++notices; });
    views.front()->set_dark(false); QVERIFY(Deliver(gui)); QCOMPARE(notices, 32);
    QVERIFY(Settle(transport, gui)); QVERIFY(!replacement.dark()); QVERIFY(overflow.dark());
    for (const auto& view : views) if (view) { QVERIFY(!view->dark()); QVERIFY(view->error().isEmpty()); }
    store.closeAdmission(); AppPreferences late; late.configure(&store);
    QCOMPARE(late.error(), QStringLiteral("Preferences view capacity is unavailable.")); QVERIFY(!late.ready());
}

void UiPreferencesTests::manualFanoutRetiresWithItsStore_data()
{
    QTest::addColumn<bool>("during_notification"); QTest::addColumn<bool>("destroy");
    QTest::newRow("queued-close") << false << false;
    QTest::newRow("queued-destroy") << false << true;
    QTest::newRow("reentrant-close") << true << false;
    QTest::newRow("reentrant-destroy") << true << true;
}

void UiPreferencesTests::manualFanoutRetiresWithItsStore()
{
    QFETCH(bool, during_notification); QFETCH(bool, destroy);
    QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("retired.ini");
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    auto store = std::make_unique<UiPreferencesStore>(execution::SnapshotServices{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(path));
    const auto cleanup = qScopeGuard([&] { if (store) store->closeAdmission(); transport.drain(); Settle(transport, gui); });
    store->start(); QVERIFY(Settle(transport, gui));
    std::vector<std::unique_ptr<AppPreferences>> views;
    for (int row = 0; row < 33; ++row) { auto view = std::make_unique<AppPreferences>(); view->configure(store.get()); views.push_back(std::move(view)); }
    const auto stop = [&] { if (destroy) store.reset(); else store->closeAdmission(); };
    int notices{0};
    for (const auto& view : views) connect(view.get(), &AppPreferences::changed, this, [&] {
        ++notices; if (during_notification && notices == 1) stop();
    });
    views.front()->set_dark(false); const auto receipt = store->lastWriteReceipt(); QVERIFY(receipt);
    transport.drain(); QCOMPARE(receipt->load(), UiPreferencesStore::Applied); QVERIFY(views.front()->dark());
    if (!during_notification) stop();
    QVERIFY(Settle(transport, gui)); QCOMPARE(notices, during_notification ? 1 : 0);
    QSettings persisted{path, QSettings::IniFormat}; QVERIFY(!persisted.value("dark").toBool());
    if (destroy) { QVERIFY(!store); QVERIFY(!views.front()->ready()); }
}

void UiPreferencesTests::manualOlderWriteFailureCannotReplaceTheLatestOutcome()
{
    QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("ordered.ini");
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(path)};
    const auto cleanup = qScopeGuard([&] { store.closeAdmission(); transport.drain(); Settle(transport, gui); });
    AppPreferences view; view.configure(&store); store.start(); QVERIFY(Settle(transport, gui));
    view.set_blockclocksize(0); const auto older = store.lastWriteReceipt(); QVERIFY(older);
    view.set_dark(false); const auto latest = store.lastWriteReceipt(); QVERIFY(latest); QVERIFY(older != latest);
    transport.drain(); QCOMPARE(older->load(), UiPreferencesStore::Failed); QCOMPARE(latest->load(), UiPreferencesStore::Applied);
    QVERIFY(view.dark()); QVERIFY(store.pending()); QVERIFY(gui.size() >= size_t{3});
    auto newest = std::move(gui.back()); gui.pop_back(); newest();
    QVERIFY(!view.dark()); QVERIFY(store.error().isEmpty());
    auto stale_failure = std::move(gui.back()); gui.pop_back(); stale_failure();
    QVERIFY(Settle(transport, gui)); QVERIFY(!store.pending()); QVERIFY(store.error().isEmpty());
    QCOMPARE(view.blockclocksize(), 5.0 / 12.0); QVERIFY(!view.dark());
    QSettings persisted{path, QSettings::IniFormat}; QVERIFY(!persisted.value("dark").toBool());
    QVERIFY(!persisted.contains("blockclocksize"));
}

void UiPreferencesTests::manualObserverLossKeepsOriginalEffects_data()
{
    QTest::addColumn<bool>("write"); QTest::addColumn<bool>("failure");
    QTest::newRow("read-success") << false << false;
    QTest::newRow("read-failure") << false << true;
    QTest::newRow("write-success") << true << false;
    QTest::newRow("write-failure") << true << true;
}

void UiPreferencesTests::manualObserverLossKeepsOriginalEffects()
{
    QFETCH(bool, write); QFETCH(bool, failure);
    QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("observer.ini");
    if (!write && failure) QVERIFY(QDir().mkdir(path));
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    auto store = std::make_unique<UiPreferencesStore>(execution::SnapshotServices{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(path));
    const auto cleanup = qScopeGuard([&] { if (store) store->closeAdmission(); transport.drain(); Settle(transport, gui); });
    AppPreferences view; view.configure(store.get()); store->start();
    std::shared_ptr<const std::atomic<UiPreferencesStore::Outcome>> receipt;
    if (write) {
        QVERIFY(Settle(transport, gui));
        if (failure) view.set_blockclocksize(0); else view.set_dark(false);
        receipt = store->lastWriteReceipt(); QVERIFY(receipt);
    }
    transport.drain(); QSignalSpy notices{&view, &AppPreferences::changed}; store.reset();
    QVERIFY(Settle(transport, gui)); QCOMPARE(notices.size(), 0); QVERIFY(!view.ready()); QVERIFY(!view.pending());
    if (receipt) QCOMPARE(receipt->load(), failure ? UiPreferencesStore::Failed : UiPreferencesStore::Applied);
    if (write) {
        QSettings persisted{path, QSettings::IniFormat}; QVERIFY(!persisted.contains("blockclocksize"));
        QCOMPARE(persisted.contains("dark"), !failure);
        if (!failure) QVERIFY(!persisted.value("dark").toBool());
    }
    QVERIFY(transport.quiescent());
}

void UiPreferencesTests::manualHeldOutcomesRefuseFurtherWritesWithoutEffects()
{
    QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("held.ini");
    auto limits = execution::Limits::Bounded(); limits.action_scopes = {256, 4 * 1024 * 1024};
    ManualRoutedExecution transport{limits}; std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(path)};
    const auto cleanup = qScopeGuard([&] { store.closeAdmission(); transport.drain(); Settle(transport, gui); });
    AppPreferences view; view.configure(&store); store.start(); QVERIFY(Settle(transport, gui));
    std::vector<std::shared_ptr<const std::atomic<UiPreferencesStore::Outcome>>> receipts;
    for (int item = 0; item < 128; ++item) {
        view.set_dark((item % 2) != 0); receipts.push_back(store.lastWriteReceipt()); QVERIFY(receipts.back());
        transport.drain(); QCOMPARE(receipts.back()->load(), UiPreferencesStore::Applied);
    }
    QVERIFY(store.pending()); const auto native_calls = transport.coreSubmissions();
    QCOMPARE(view.requestGeometry(4, 5, 640, 480), quint64{0}); const auto refused = store.lastWriteReceipt(); QVERIFY(refused);
    QCOMPARE(transport.coreSubmissions(), native_calls); QVERIFY(Settle(transport, gui));
    QCOMPARE(refused->load(), UiPreferencesStore::Failed); QVERIFY(!store.pending()); QVERIFY(!store.error().isEmpty());
    for (const auto& receipt : receipts) QCOMPARE(receipt->load(), UiPreferencesStore::Applied);
    QSettings persisted{path, QSettings::IniFormat}; QVERIFY(persisted.value("dark").toBool());
    QVERIFY(!persisted.contains("windowX")); QCOMPARE(view.windowWidth(), 0.0);
}

void UiPreferencesTests::legacyGeometryReadbackPreservesOnlyRepresentableValues_data()
{
    QTest::addColumn<double>("x"); QTest::addColumn<double>("y");
    QTest::addColumn<double>("width"); QTest::addColumn<double>("height"); QTest::addColumn<bool>("valid");
    const double low = std::numeric_limits<int>::min(), high = std::numeric_limits<int>::max();
    QTest::newRow("ordinary") << -20.5 << 30.25 << 640.5 << 480.25 << true;
    QTest::newRow("legacy-integer-boundaries") << low << high << low << high << true;
    QTest::newRow("nonfinite") << std::numeric_limits<double>::quiet_NaN() << std::numeric_limits<double>::infinity()
        << -std::numeric_limits<double>::infinity() << std::numeric_limits<double>::quiet_NaN() << false;
    QTest::newRow("below-integer-range") << low - 1 << low - 2 << low - 3 << low - 4 << false;
    QTest::newRow("above-integer-range") << high + 1 << high + 2 << high + 3 << high + 4 << false;
}

void UiPreferencesTests::legacyGeometryReadbackPreservesOnlyRepresentableValues()
{
    QFETCH(double, x); QFETCH(double, y); QFETCH(double, width); QFETCH(double, height); QFETCH(bool, valid);
    QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("legacy-geometry.ini");
    {
        QSettings persisted{path, QSettings::IniFormat};
        persisted.setValue("windowX", x); persisted.setValue("windowY", y);
        persisted.setValue("windowWidth", width); persisted.setValue("windowHeight", height); persisted.sync();
    }
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(path)};
    const auto cleanup = qScopeGuard([&] { store.closeAdmission(); transport.drain(); Settle(transport, gui); });
    AppPreferences view; view.configure(&store); store.start(); QVERIFY(Settle(transport, gui));
    QVERIFY(view.ready()); QVERIFY(view.error().isEmpty());
    QCOMPARE(view.windowX(), valid ? x : 0.0); QCOMPARE(view.windowY(), valid ? y : 0.0);
    QCOMPARE(view.windowWidth(), valid ? width : 0.0); QCOMPARE(view.windowHeight(), valid ? height : 0.0);
    QSettings persisted{path, QSettings::IniFormat};
    const auto same = [](double actual, double expected) { return std::isnan(expected) ? std::isnan(actual) : actual == expected; };
    QVERIFY(same(persisted.value("windowX").toDouble(), x)); QVERIFY(same(persisted.value("windowY").toDouble(), y));
    QVERIFY(same(persisted.value("windowWidth").toDouble(), width)); QVERIFY(same(persisted.value("windowHeight").toDouble(), height));
}

void UiPreferencesTests::manualOlderReadFailureCannotReplaceNewerConfirmedWrite()
{
    QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("read-write-order.ini");
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    UiPreferencesStore store{{transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}, File(path)};
    const auto cleanup = qScopeGuard([&] { store.closeAdmission(); transport.drain(); Settle(transport, gui); });
    AppPreferences view; view.configure(&store); store.start(); QVERIFY(Settle(transport, gui));
    QVERIFY(store.ready()); QVERIFY(view.dark()); QVERIFY(!store.pending());

    // The failed older read keeps its own GUI outcome while a newer command
    // independently owns a successful Native write and its confirmation.
    const auto core_calls = transport.coreSubmissions(); transport.rejectLane(execution::Lane::Core);
    store.retry(); const auto read_ticket = store.operationId();
    QCOMPARE(transport.coreSubmissions(), core_calls + 1); QVERIFY(store.pending()); QVERIFY(store.ready());
    view.set_dark(false); const auto write_ticket = store.operationId();
    const auto receipt = store.lastWriteReceipt(); QVERIFY(receipt); QVERIFY(write_ticket > read_ticket);
    transport.drain(); QCOMPARE(receipt->load(), UiPreferencesStore::Applied); QVERIFY(view.dark());
    {
        QSettings persisted{path, QSettings::IniFormat}; QVERIFY(persisted.contains("dark"));
        QVERIFY(!persisted.value("dark").toBool());
    }

    // Deliver the newer confirmation first, leaving the rejected read and the
    // bounded view-notification turn in the manual GUI mailbox.
    QVERIFY(gui.size() >= size_t{3}); auto confirmed = std::move(gui.back()); gui.pop_back(); confirmed();
    QVERIFY(!view.dark()); QVERIFY(store.error().isEmpty()); QVERIFY(store.pending());
    QCOMPARE(store.operationId(), write_ticket);

    QSignalSpy settlement{&store, &UiPreferencesStore::stateChanged};
    QVERIFY(Deliver(gui)); // Original read failure: settle it without replacing the newer result.
    QCOMPARE(settlement.size(), 1); QVERIFY(!store.pending());
    QVERIFY(store.error().isEmpty()); QVERIFY(view.error().isEmpty()); QVERIFY(!view.dark());
    QCOMPARE(store.operationId(), write_ticket); QCOMPARE(receipt->load(), UiPreferencesStore::Applied);
    QVERIFY(Settle(transport, gui)); QVERIFY(!store.pending()); QVERIFY(store.ready()); QVERIFY(store.error().isEmpty());
    QSettings persisted{path, QSettings::IniFormat}; QVERIFY(!persisted.value("dark").toBool());
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(UiPreferencesTests)
#else
QTEST_MAIN(UiPreferencesTests)
#endif
#include <test_uipreferences.moc>
