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
#include <QTemporaryDir>
#include <QThread>
#include <deque>
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
#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(UiPreferencesTests)
#else
QTEST_MAIN(UiPreferencesTests)
#endif
#include <test_uipreferences.moc>
