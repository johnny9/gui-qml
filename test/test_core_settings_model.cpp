// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/core_settings_model.h>
#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <qml/retainedsnapshot.h>
#include <test/mocks/drainexecutors.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QThread>
#include <atomic>
#include <algorithm>
#include <deque>
namespace {
bool Publish(std::deque<std::function<void()>>& gui){if(gui.empty())return false;auto turn=std::move(gui.front());gui.pop_front();turn();return true;}
bool Settle(ManualRoutedExecution& transport,std::deque<std::function<void()>>& gui){
    for(int turn=0;turn<512;++turn){if(!transport.queuedIdle()){if(!transport.stepNext())return false;continue;}if(Publish(gui))continue;return true;}return false;
}
}
class CoreSettingsModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void nestedStatusProjectionCountsCompletePayloadWithoutRecursion() {
        constexpr size_t depth{2048};
        QVariant nested{QStringLiteral("tail")};
        for (size_t level = 0; level < depth; ++level) nested = QVariantList{nested};
        const QVariantMap values{{QStringLiteral("key"), nested}};
        const auto projection = QmlCoreSettings::CaptureStatuses(values);
        // Root map, key metadata, every nested list, and the complete UTF-16 leaf.
        QCOMPARE(projection.payload_bytes, size_t{64 + 64 + 3 * sizeof(QChar) + depth * 64 + 64 + 4 * sizeof(QChar)});
        QCOMPARE(projection.values, values); QVERIFY(projection.identity);
    }
    void manualDecisionOutcomeAndPresentation();
    void followupPreviewPreservesOriginalOutcomeTicket();
    void acceptedEffectContinuesAfterGlobalClose();
    void oldPreviewCannotReplaceNewUserIntent();
    void queuedDecisionsOwnEffectsBeforeOutcomeDelivery();
    void failedProjectionDoesNotCommitCandidateAndCanRecover();
    void queuedDuplicateRetriesFailedOriginalWithoutLosingOtherKeys();
    void heldTypingCoalescesAndOversizedLiteralRejects();
    void ownedWorkBoundsHeldCompletedOutcomes();
    void ownedWorkRetriesOutcomeAndDrainWithoutReplayingEffect();
    void ownedWorkDrainDropsNativeCaptureWhileClosedFacadeLives();
    void proxyDraftKeepsOriginalLiteralAndAtomicPair();
    void realThreadDecisionAndEffectStayOffGui();
};
void CoreSettingsModelTests::manualDecisionOutcomeAndPresentation()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial;initial.listen=false;
    CoreSettingsModel model{initial};model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});
    QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(model.pending());
    QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(model.pending());QVERIFY(Publish(gui));QVERIFY(!model.pending());
    bool before{false},after{false};
    model.setBeforeChangeHandler([&](auto origin){before=origin==CoreSettingsModel::ChangeOrigin::User&&!model.values().listen;});
    model.setAfterChangeHandler([&](const auto& change,auto origin){after=origin==CoreSettingsModel::ChangeOrigin::User&&change.accepted&&model.values().listen;});
    const auto ticket=model.changeListen(true);QVERIFY(ticket);QVERIFY(!model.values().listen);
    QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(!model.values().listen);QVERIFY(!before);
    QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(!model.values().listen);QVERIFY(!before);QVERIFY(!after);
    QVERIFY(Publish(gui));QVERIFY(before);QVERIFY(after);QVERIFY(model.values().listen);QVERIFY(model.isTouched("listen"));
    QCOMPARE(model.operationId(),ticket);QCOMPARE(model.operationState(),CoreSettingsModel::Applied);
    model.closeAdmission();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::followupPreviewPreservesOriginalOutcomeTicket()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial;initial.listen=false;
    CoreSettingsModel model{initial};model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});
    QVERIFY(Settle(transport,gui));
    QSignalSpy outcomes{&model,&CoreSettingsModel::operationFinished};
    model.setAfterChangeHandler([&](const auto&,auto origin){if(origin==CoreSettingsModel::ChangeOrigin::User)model.refresh();});
    const auto original=model.changeListen(true);QVERIFY(original);
    QVERIFY(Settle(transport,gui));QVERIFY(model.values().listen);QVERIFY(!model.pending());
    QVERIFY(model.operationId()>original);
    int original_outcomes{0};
    for(const auto& row:outcomes)if(row[0].toULongLong()==original){++original_outcomes;QCOMPARE(row[1].value<CoreSettingsModel::OperationState>(),CoreSettingsModel::Applied);}
    QCOMPARE(original_outcomes,1);
    model.closeAdmission();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::acceptedEffectContinuesAfterGlobalClose()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial;initial.natpmp=false;
    CoreSettingsModel model{initial};model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});QVERIFY(Settle(transport,gui));
    int effects{0};bool actual{false};
    model.setEffectPort([&](const auto& change,auto,std::shared_ptr<execution::Scheduler> action,auto done,auto failed){
        const auto admitted=execution::Submit(*action,[&,change]{++effects;actual=change.after.natpmp;
            return CoreSettingsModel::EffectResult{{},change.after,change.after_statuses,{}};},done,failed);
        if(!admitted)failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    },{},{});
    QVERIFY(model.changeNatpmp(true));
    // The decision ran, but its Domain outcome has not admitted the Core effect yet.
    QVERIFY(transport.step(execution::Lane::Domain));QCOMPARE(effects,0);
    transport.scheduler->closeOrdinaryAdmission();model.closeAdmission();
    QSignalSpy notices{&model,&CoreSettingsModel::operationChanged};
    QVERIFY(transport.step(execution::Lane::Domain));QCOMPARE(effects,0);
    QVERIFY(transport.step(execution::Lane::Core));QCOMPARE(effects,1);QVERIFY(actual);
    QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(Settle(transport,gui));
    QCOMPARE(notices.size(),0);QCOMPARE(effects,1);QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::oldPreviewCannotReplaceNewUserIntent()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial;initial.listen=false;initial.server=false;
    CoreSettingsModel model{initial};model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});QVERIFY(Settle(transport,gui));
    const auto original=model.publishedUserRevision();QVERIFY(model.changeListen(true));
    QmlCoreSettings::Values old_preview=initial;old_preview.server=true;bool adopted{true};
    QVERIFY(model.applyPreviewValuesPreservingTouched(old_preview,{},[&](bool value){adopted=value;},true,original));
    QVERIFY(Settle(transport,gui));QVERIFY(!adopted);QVERIFY(model.values().listen);QVERIFY(!model.values().server);QVERIFY(model.isTouched("listen"));
    QCOMPARE(model.operationState(),CoreSettingsModel::Stale);
    model.closeAdmission();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::queuedDecisionsOwnEffectsBeforeOutcomeDelivery()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;QmlCoreSettings::Values initial;initial.listen=false;initial.natpmp=false;
    CoreSettingsModel model{initial};model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});QVERIFY(Settle(transport,gui));
    auto actual=std::make_shared<QmlCoreSettings::Values>(initial);auto effects=std::make_shared<int>(0);
    model.setEffectPort([actual,effects](const auto& change,auto,std::shared_ptr<execution::Scheduler> action,auto done,auto failed){
        if(!change.accepted||(!QmlCoreSettings::ValuesChanged(change)&&!change.force_write)){done(CoreSettingsModel::EffectResult{});return;}
        const auto admitted=execution::Submit(*action,[change,actual,effects]{++*effects;
            if(change.setting_name=="listen")actual->listen=change.after.listen;
            if(change.setting_name=="natpmp")actual->natpmp=change.after.natpmp;
            return CoreSettingsModel::EffectResult{{},*actual,change.after_statuses,{}};},done,failed);
        if(!admitted)failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    },{},{});
    QVERIFY(model.changeListen(true));QVERIFY(model.changeNatpmp(true));bool preview_applied{true};
    QVERIFY(model.applyPreviewValuesPreservingTouched(initial,{},[&](bool applied){preview_applied=applied;},true,model.userRevision()));
    // Execute both decisions and the preview before delivering ANY outcome.
    QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(transport.step(execution::Lane::Domain));
    QCOMPARE(*effects,0);QVERIFY(model.changeListen(false));
    for(int turn=0;turn<4;++turn)QVERIFY(transport.step(execution::Lane::Domain));
    QCOMPARE(*effects,0);QVERIFY(Settle(transport,gui));QVERIFY(!preview_applied);
    QCOMPARE(*effects,3);QVERIFY(!actual->listen);QVERIFY(actual->natpmp);QVERIFY(!model.values().listen);QVERIFY(model.values().natpmp);
    model.closeAdmission();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::failedProjectionDoesNotCommitCandidateAndCanRecover()
{
    auto limits=execution::Limits::Bounded();limits.cleanup_reservations.tasks=6;
    ManualRoutedExecution transport{limits};std::deque<std::function<void()>> gui;QmlCoreSettings::Values initial;initial.listen=false;
    CoreSettingsModel model{initial};model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});QVERIFY(Settle(transport,gui));
    auto effects=std::make_shared<int>(0);
    model.setEffectPort([effects](const auto& change,auto,std::shared_ptr<execution::Scheduler> action,auto done,auto failed){
        if(!change.accepted||(!QmlCoreSettings::ValuesChanged(change)&&!change.force_write)){done(CoreSettingsModel::EffectResult{});return;}
        const auto admitted=execution::Submit(*action,[change,effects]{++*effects;return CoreSettingsModel::EffectResult{{},change.after,change.after_statuses,{}};},done,failed);
        if(!admitted)failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    },{},{});
    std::vector<std::shared_ptr<int>> holds;for(int item=0;item<3;++item){auto value=execution::RetainSnapshot<int>(transport.scheduler,[]{return std::make_unique<int>(1);});QVERIFY(value);holds.push_back(std::move(value));}
    QVERIFY(model.changeListen(true));QVERIFY(Settle(transport,gui));QVERIFY(!model.error().isEmpty());QCOMPARE(*effects,0);QVERIFY(!model.values().listen);
    const auto consumed=model.userRevision();holds.clear();transport.drain();
    bool refreshed{false};QVERIFY(model.applyPreviewValuesPreservingTouched(initial,{},[&](bool accepted){refreshed=accepted;},true,consumed));
    QVERIFY(Settle(transport,gui));QVERIFY(refreshed);QVERIFY(!model.values().listen);
    QVERIFY(model.changeListen(true));QVERIFY(Settle(transport,gui));QCOMPARE(*effects,1);QVERIFY(model.values().listen);
    model.closeAdmission();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::queuedDuplicateRetriesFailedOriginalWithoutLosingOtherKeys()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial;initial.listen=false;initial.natpmp=false;
    CoreSettingsModel model{initial};model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});QVERIFY(Settle(transport,gui));
    auto actual=std::make_shared<QmlCoreSettings::Values>(initial);auto effects=std::make_shared<int>(0);
    model.setEffectPort([actual,effects](const auto& change,auto,std::shared_ptr<execution::Scheduler> action,auto done,auto failed){
        if(!change.accepted||(!QmlCoreSettings::ValuesChanged(change)&&!change.force_write)){done(CoreSettingsModel::EffectResult{});return;}
        const auto admitted=execution::Submit(*action,[change,actual,effects]{
            ++*effects;const bool rejected=*effects==1;
            if(!rejected&&change.setting_name=="listen")actual->listen=change.after.listen;
            if(!rejected&&change.setting_name=="natpmp")actual->natpmp=change.after.natpmp;
            return CoreSettingsModel::EffectResult{rejected?QStringLiteral("original write failed"):QString{},*actual,change.after_statuses,{}};
        },done,failed);
        if(!admitted)failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    },{},{});
    QVERIFY(model.changeListen(true));QVERIFY(model.changeNatpmp(true));const auto retry=model.changeListen(true);QVERIFY(retry);
    // All decisions precede all Core writes: the equal duplicate remains an
    // independently owned write while the original same-key write is pending.
    for(int decision=0;decision<3;++decision)QVERIFY(transport.step(execution::Lane::Domain));
    QCOMPARE(*effects,0);QVERIFY(Settle(transport,gui));
    QCOMPARE(*effects,3);QVERIFY(actual->listen);QVERIFY(actual->natpmp);
    QVERIFY(model.values().listen);QVERIFY(model.values().natpmp);
    QCOMPARE(model.operationId(),retry);QCOMPARE(model.operationState(),CoreSettingsModel::Applied);
    model.closeAdmission();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::heldTypingCoalescesAndOversizedLiteralRejects()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;CoreSettingsModel model;
    model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});QVERIFY(Settle(transport,gui));
    std::unique_ptr<CoreProxyDraft> draft{model.createProxyDraft()};QVERIFY(Settle(transport,gui));
    draft->setProxyEnabled(true);transport.drain();QCOMPARE(gui.size(),size_t{1});
    for(int edit=0;edit<10'000;++edit)draft->setProxyAddress(QStringLiteral("192.0.2.%1:9050").arg(edit%200+1));
    QCOMPARE(gui.size(),size_t{1});QVERIFY(transport.queuedIdle());QVERIFY(draft->pending());QVERIFY(!draft->canApply());
    draft->setProxyAddress("127.0.0.1:9050");QVERIFY(Publish(gui));QVERIFY(draft->pending());
    // A stale completion queues one latest observation; it cannot resubmit on
    // the synchronous failure/completion stack, and the manual port owns it.
    // Releasing the old outcome may queue Native cleanup. It must not start
    // the successor decision before the independent GUI follow-up turn.
    QVERIFY((transport.queuedCountIf([](const auto& turn){
        return (turn.lane==execution::Lane::Core||turn.lane==execution::Lane::Domain)&&turn.phase==execution::Phase::Queued;
    }) == 0));QCOMPARE(gui.size(),size_t{1});
    QVERIFY(Settle(transport,gui));QVERIFY(draft->canApply());QVERIFY(draft->proxyError().isEmpty());
    const auto before=model.userRevision();const QString original(3*1024*1024,QChar{'x'});
    QCOMPARE(model.changeProxyLocation(original),quint64{0});QCOMPARE(model.userRevision(),before);QVERIFY(Settle(transport,gui));
    QVERIFY(!model.error().isEmpty());QVERIFY(model.values().proxy_address.isEmpty());
    model.closeAdmission();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::ownedWorkBoundsHeldCompletedOutcomes()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;int effects{0},rejected{0};
    execution::OwnedWork work{{transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}}};
    for(int item=0;item<128;++item){QVERIFY(work.submit([&]{return ++effects;},[](int){},[&](std::exception_ptr){++rejected;}));transport.drain();}
    QCOMPARE(effects,128);QCOMPARE(gui.size(),size_t{128});
    QVERIFY(!work.submit([&]{return ++effects;},[](int){},[&](std::exception_ptr){++rejected;}));QCOMPARE(rejected,1);QCOMPARE(effects,128);
    QVERIFY(!work.submitAsync<int>([&](auto,auto){++effects;return true;},[](int){},[&](std::exception_ptr){++rejected;}));QCOMPARE(rejected,2);QCOMPARE(effects,128);
    work.close();while(Publish(gui)){}transport.drain();QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::ownedWorkRetriesOutcomeAndDrainWithoutReplayingEffect()
{
    ManualRoutedExecution transport;int effects{0},publications{0};
    execution::OwnedWork work{{transport.scheduler,[](auto){throw std::runtime_error("primary post rejected");}}};
    QVERIFY(work.submit([&]{return ++effects;},[&](int result){QCOMPARE(result,1);++publications;},[](auto){QFAIL("Unexpected effect failure");}));
    transport.drain();QCOMPARE(effects,1);QCOMPARE(publications,0);QTRY_COMPARE(publications,1);
    bool drained{false};work.close([&]{drained=true;});QTRY_VERIFY(drained);
    transport.drain();QCOMPARE(effects,1);QVERIFY(transport.quiescent());

    int posted{0};std::deque<std::function<void()>> rejected;
    execution::OwnedWork duplicate{{transport.scheduler,[&](auto callback){++posted;rejected.push_back(std::move(callback));throw std::runtime_error("enqueued before rejection");}}};
    QVERIFY(duplicate.submit([&]{return ++effects;},[&](int){++publications;},[](auto){QFAIL("Unexpected effect failure");}));
    transport.drain();QTRY_COMPARE(publications,2);while(Publish(rejected)){}QCOMPARE(publications,2);QCOMPARE(effects,2);
    duplicate.close();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::ownedWorkDrainDropsNativeCaptureWhileClosedFacadeLives()
{
    int effects{0},published{0},drained{0},destroyed{0};
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    execution::OwnedWork work{{transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}}};
    struct Native {int& destroyed;~Native(){++destroyed;}};
    auto value=execution::RetainSnapshot<Native>(transport.scheduler,[&]{return std::unique_ptr<Native>{new Native{destroyed}};});
    QVERIFY(value);std::weak_ptr<Native> original{value};
    QVERIFY(work.submit([&]{++effects;return 42;},[&](int){++published;},[](std::exception_ptr){QFAIL("Accepted effect failed");}));
    work.close([value,&drained]{QVERIFY(value);++drained;});value.reset();
    transport.drain();QCOMPARE(effects,1);QCOMPARE(published,0);QCOMPARE(drained,0);
    QVERIFY(!original.expired());QCOMPARE(destroyed,0);
    while(Publish(gui)){}
    QCOMPARE(drained,1);QCOMPARE(published,0);QVERIFY(original.expired());
    // Release of the last GUI alias only activates reserved Native cleanup.
    QCOMPARE(destroyed,0);transport.drain();QCOMPARE(destroyed,1);
    work.close();transport.drain();while(Publish(gui)){}
    QCOMPARE(drained,1);QCOMPARE(effects,1);QCOMPARE(transport.scheduler->pending(),size_t{0});
    // work is intentionally still alive: no consumed callback capture may
    // require destroying its closed facade to finish the Native ledger.
}

void CoreSettingsModelTests::proxyDraftKeepsOriginalLiteralAndAtomicPair()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    CoreSettingsModel model;model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});QVERIFY(Settle(transport,gui));
    std::unique_ptr<CoreProxyDraft> draft{model.createProxyDraft()};QVERIFY(draft);QVERIFY(Settle(transport,gui));
    draft->setProxyEnabled(true);draft->setProxyAddress("not a proxy");transport.drain();
    draft->setProxyAddress("127.0.0.1:9050");while(Publish(gui)){}QVERIFY(draft->pending());QVERIFY(!draft->canApply());
    QVERIFY(Settle(transport,gui));QVERIFY(draft->proxyError().isEmpty());QVERIFY(draft->canApply());QVERIFY(!model.values().proxy_enabled);
    draft->setTorEnabled(true);draft->setTorAddress("");QVERIFY(Settle(transport,gui));QVERIFY(!draft->torError().isEmpty());
    QCOMPARE(draft->requestApply(),quint64{0});QVERIFY(!model.values().proxy_enabled);QVERIFY(!model.values().tor_enabled);
    draft->setTorAddress("[::1]:9050");QVERIFY(Settle(transport,gui));
    const auto ticket=draft->requestApply();QVERIFY(ticket);draft->setProxyAddress("192.0.2.9:9050");
    QVERIFY(Settle(transport,gui));QCOMPARE(draft->operationId(),ticket);QCOMPARE(draft->operationState(),CoreSettingsModel::Applied);
    QVERIFY(model.values().proxy_enabled);QVERIFY(model.values().tor_enabled);
    QCOMPARE(model.values().proxy_address,QStringLiteral("127.0.0.1:9050"));QCOMPARE(model.values().tor_address,QStringLiteral("[::1]:9050"));
    QCOMPARE(draft->proxyAddress(),QStringLiteral("192.0.2.9:9050"));
    model.closeAdmission();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::realThreadDecisionAndEffectStayOffGui()
{
    const auto drain_native=DrainExecutorsBeforeNativeOwnerDestruction();
    BackendExecutor executor{execution::Limits::Bounded()};CoreSettingsModel model;
    model.configureServices({executor.scheduler(),presentation::QueuedPost()});QTRY_VERIFY(!model.pending());
    const auto* gui=QThread::currentThread();std::atomic<bool> decision_native{false},effect_native{false};
    model.setEffectPort([&](const auto& change,auto,std::shared_ptr<execution::Scheduler> action,auto done,auto failed){
        decision_native=QThread::currentThread()!=gui;
        const auto admitted=execution::Submit(*action,[&,change]{effect_native=QThread::currentThread()!=gui;
            return CoreSettingsModel::EffectResult{{},change.after,change.after_statuses,{}};},done,failed);
        if(!admitted)failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    },{},{});
    QVERIFY(model.changeServer(true));QTRY_VERIFY(!model.pending());QVERIFY(model.values().server);QVERIFY(decision_native.load());QVERIFY(effect_native.load());
    model.closeAdmission();executor.shutdown();
}
#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(CoreSettingsModelTests)
#else
QTEST_MAIN(CoreSettingsModelTests)
#endif
#include <test_core_settings_model.moc>
