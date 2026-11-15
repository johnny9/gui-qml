// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/onboardingflow.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/guiargs.h>
#include <common/args.h>
#include <init.h>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <qml/backendexecutor.h>
#include <test/mocks/drainexecutors.h>
#include <QThread>
#include <QScopeGuard>
#include <test/mocks/manualexecutionenvironment.h>
#include <QtTest/QtTest>

#include <deque>
#include <stdexcept>

namespace {
QmlOnboardingSettings::ApplyRequest Request()
{
    QmlOnboardingSettings::ApplyRequest request;
    request.data_dir.path=QStringLiteral("/original/profile");
    request.touched_settings={QStringLiteral("listen")};
    request.values.listen=false;
    return request;
}
QmlOnboardingSettings::ProfileResult Saved()
{
    QmlOnboardingSettings::ProfileResult result;
    result.completed=true;result.replacement_written=true;result.directory_saved=true;result.cleanup_completed=true;
    return result;
}
void Ready(onboarding::Flow& flow,uint64_t revision=1)
{
    flow.edit(revision);
    flow.checked({revision,true,true,true,{}});
}
struct ManualOnboarding {
    QTemporaryDir directory;
    ArgsManager args;
    ManualRoutedExecution execution;
    std::deque<std::function<void()>> gui;
    std::unique_ptr<OnboardingOptionsModel> model;
    ManualOnboarding() {
        SetupServerArgs(args,false);SetupQmlGuiArgs(args);
        args.ForceSetArg("-regtest","1");args.ForceSetArg("-datadir",directory.path().toStdString());
        model=std::make_unique<OnboardingOptionsModel>(std::vector<std::string>{"bitcoin-qt","-regtest","-datadir="+directory.path().toStdString()},false,nullptr,
            execution::SnapshotServices{execution.scheduler,[this](auto turn){gui.push_back(std::move(turn));}});
    }
    bool publish() {if(gui.empty())return false;auto turn=std::move(gui.front());gui.pop_front();turn();return true;}
    bool settle() {for(int i=0;i<1000;++i){if(execution.stepNext())continue;if(publish())continue;return true;}return false;}
    ~ManualOnboarding(){model.reset();execution.drain();while(publish())execution.drain();}
};

}
class OnboardingFlowTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void positiveExactChecksRequired_data() {
        QTest::addColumn<int>("missing");
        for(int missing=0;missing<4;++missing)QTest::newRow(qPrintable(QString::number(missing)))<<missing;
    }
    void positiveExactChecksRequired() {
        QFETCH(int,missing);onboarding::Flow flow;flow.edit(2);
        QVERIFY(!flow.checked({1,true,true,true,{}}));
        QVERIFY(!flow.canFinish());
        QVERIFY(flow.checked({2,missing!=0,missing!=1,missing!=2,missing==3?QStringLiteral("invalid effective policy"):QString{}}));
        QVERIFY(!flow.canFinish());QVERIFY(!flow.finish(2,Request()));
        Ready(flow,3);QVERIFY(flow.canFinish());
        QVERIFY(!flow.finish(2,Request()));QVERIFY(flow.finish(3,Request()));
    }
    void sameAnswersStillOwnNewRevision() {
        onboarding::Flow flow;Ready(flow,1);flow.edit(2);
        QVERIFY(!flow.canFinish());QVERIFY(!flow.checked({1,true,true,true,{}}));
        QVERIFY(flow.checked({2,true,true,true,{}}));
        const auto attempt=flow.finish(2,Request());QVERIFY(attempt);QCOMPARE(attempt->revision,uint64_t{2});
        QCOMPARE(attempt->request.data_dir.path,QStringLiteral("/original/profile"));
        flow.edit(3);QCOMPARE(flow.revision(),uint64_t{2});QVERIFY(!flow.finish(2,Request()));
    }
    void knownPartialCannotRepeatWholeWrite() {
        onboarding::Flow flow;Ready(flow);const auto attempt=flow.finish(1,Request());QVERIFY(attempt);
        auto partial=Saved();partial.completed=false;partial.cleanup_completed=false;partial.error=QStringLiteral("source conflict");
        QVERIFY(flow.persisted(attempt->id,partial));QCOMPARE(flow.phase(),onboarding::Phase::Failed);
        flow.edit(2);QCOMPARE(flow.revision(),uint64_t{1});QVERIFY(!flow.finish(1,Request()));
        QVERIFY(flow.beginCompletion());QVERIFY(!flow.beginCompletion());
        QVERIFY(!flow.persisted(attempt->id+1,Saved()));QVERIFY(flow.pending());
        QVERIFY(flow.persisted(attempt->id,Saved()));QCOMPARE(flow.phase(),onboarding::Phase::PreparingNode);
    }
    void unknownOutcomeOnlyQueriesOriginalAttempt() {
        onboarding::Flow flow;Ready(flow);const auto attempt=flow.finish(1,Request());QVERIFY(attempt);
        QmlOnboardingSettings::ProfileResult unknown;unknown.uncertain=true;unknown.error=QStringLiteral("lost outcome");
        QVERIFY(flow.persisted(attempt->id,unknown));QCOMPARE(flow.phase(),onboarding::Phase::Reconciling);
        flow.edit(2);QVERIFY(!flow.finish(1,Request()));QVERIFY(!flow.beginCompletion());
        QVERIFY(flow.beginQuery());QVERIFY(!flow.beginQuery());
        flow.queryUnavailable(QStringLiteral("query rejected"));QCOMPARE(flow.phase(),onboarding::Phase::Reconciling);
        QVERIFY(!flow.pending());QCOMPARE(flow.attempt()->id,attempt->id);
        QVERIFY(flow.beginQuery());QVERIFY(flow.reconciled(attempt->id,Saved()));
        QCOMPARE(flow.phase(),onboarding::Phase::PreparingNode);QVERIFY(flow.beginPreparation());
        QVERIFY(!flow.beginPreparation());flow.prepared(true,false,{});QCOMPARE(flow.phase(),onboarding::Phase::Completed);
        QVERIFY(!flow.beginPreparation());
    }
    void closeAccountsActualEffects_data() {
        QTest::addColumn<bool>("preparing");QTest::newRow("persisting")<<false;QTest::newRow("preparing")<<true;
    }
    void closeAccountsActualEffects() {
        QFETCH(bool,preparing);onboarding::Flow flow;Ready(flow);auto attempt=flow.finish(1,Request());QVERIFY(attempt);
        if(preparing){QVERIFY(flow.persisted(attempt->id,Saved()));QVERIFY(flow.beginPreparation());}
        flow.close();QCOMPARE(flow.phase(),onboarding::Phase::Closing);QVERIFY(!flow.finish(1,Request()));
        if(preparing)flow.prepared(true,false,{});else QVERIFY(flow.persisted(attempt->id,Saved()));
        QCOMPARE(flow.phase(),onboarding::Phase::Closed);QVERIFY(flow.result().replacement_written);
    }
    void rejectedBeforeWriteCanRecheckButPreparationCannotReplay() {
        onboarding::Flow flow;Ready(flow);auto attempt=flow.finish(1,Request());QVERIFY(attempt);
        QmlOnboardingSettings::ProfileResult rejected;rejected.error=QStringLiteral("not admitted");
        QVERIFY(flow.persisted(attempt->id,rejected));Ready(flow,2);QVERIFY(flow.canFinish());
        attempt=flow.finish(2,Request());QVERIFY(attempt);QVERIFY(flow.persisted(attempt->id,Saved()));
        QVERIFY(flow.beginPreparation());flow.prepared(false,false,QStringLiteral("base initialization rejected"));
        QVERIFY(!flow.beginPreparation());QVERIFY(!flow.beginCompletion());
        flow.edit(3);QCOMPARE(flow.revision(),uint64_t{2});
    }
    void boundedIntentRejectsBeforeOwnership() {
        onboarding::Flow flow;Ready(flow);auto request=Request();request.data_dir.path=QString(32769,QLatin1Char('x'));
        QVERIFY(!flow.finish(1,request));QVERIFY(!flow.attempt());QCOMPARE(flow.phase(),onboarding::Phase::Failed);
        QVERIFY(!flow.error().isEmpty());
    }
    void manualChecksCoalesceAndRejectStalePermission() {
        ManualRoutedExecution transport;QList<onboarding::Projection> frames;int validations{0};
        onboarding::Coordinator owner{{transport.scheduler,[](auto turn){turn();}},[&](auto frame){frames.push_back(std::move(frame));}};
        owner.check({1,true,true,true,{}},[&]{++validations;return QString{};});
        QVERIFY(transport.step(execution::Lane::Domain));QCOMPARE(validations,0);
        owner.check({2,true,true,true,{}},[&]{++validations;return QString{};});
        owner.check({3,true,true,true,{}},[&]{++validations;return QString{};});
        transport.drain();QCOMPARE(validations,2);QVERIFY(!frames.empty());
        QCOMPARE(frames.back().revision,uint64_t{3});QCOMPARE(frames.back().phase,onboarding::Phase::Ready);
        int writes{0};onboarding::Ports ports;ports.write=[&](const auto&){++writes;return Saved();};
        QVERIFY(owner.finish(2,Request(),ports));transport.drain();QCOMPARE(writes,0);
        QVERIFY(owner.finish(3,Request(),ports));transport.drain();QCOMPARE(writes,1);
        owner.close();transport.drain();QVERIFY(transport.quiescent());
    }
    void manualPartialCompletionNeverReplaysMutation() {
        ManualRoutedExecution transport;QList<onboarding::Projection> frames;int writes{0},queries{0},completions{0};bool original_path=false,known_saved=false;
        onboarding::Coordinator owner{{transport.scheduler,[](auto turn){turn();}},[&](auto frame){frames.push_back(std::move(frame));}};
        owner.check({1,true,true,true,{}},[]{return QString{};});transport.drain();
        onboarding::Ports ports;
        ports.write=[&](const auto& request){++writes;original_path=request.data_dir.path==QStringLiteral("/original/profile");auto result=Saved();result.completed=false;result.cleanup_completed=false;return result;};
        ports.query=[&](const auto& original){++queries;return original;};
        ports.complete=[&](const auto& original){++completions;known_saved=original.replacement_written;return Saved();};
        QVERIFY(owner.finish(1,Request(),ports));transport.drain();QCOMPARE(writes,1);QCOMPARE(frames.back().phase,onboarding::Phase::Failed);
        QVERIFY(owner.recover(true));transport.drain();QCOMPARE(completions,1);QCOMPARE(writes,1);QCOMPARE(queries,0);QVERIFY(original_path);QVERIFY(known_saved);
        QCOMPARE(frames.back().phase,onboarding::Phase::PreparingNode);
        owner.close();transport.drain();QVERIFY(transport.quiescent());
    }
    void manualUnknownQueryFailureRetainsOwnedOutcome() {
        ManualRoutedExecution transport;QList<onboarding::Projection> frames;int writes{0},queries{0};bool fail=true,original_unknown=false;
        onboarding::Coordinator owner{{transport.scheduler,[](auto turn){turn();}},[&](auto frame){frames.push_back(std::move(frame));}};
        owner.check({1,true,true,true,{}},[]{return QString{};});transport.drain();
        onboarding::Ports ports;
        ports.write=[&](const auto&)->QmlOnboardingSettings::ProfileResult{++writes;throw std::runtime_error("unknown original write");};
        ports.query=[&](const auto& original){++queries;if(fail)throw std::runtime_error("query unavailable");original_unknown=original.uncertain;return Saved();};
        QVERIFY(owner.finish(1,Request(),ports));transport.drain();QCOMPARE(writes,1);QCOMPARE(frames.back().phase,onboarding::Phase::Reconciling);
        const auto attempt=frames.back().attempt;QVERIFY(owner.recover(false));transport.drain();QCOMPARE(queries,1);QCOMPARE(frames.back().phase,onboarding::Phase::Reconciling);
        fail=false;QVERIFY(owner.recover(false));transport.drain();QCOMPARE(queries,2);QCOMPARE(writes,1);QVERIFY(original_unknown);QCOMPARE(frames.back().attempt,attempt);
        QCOMPARE(frames.back().phase,onboarding::Phase::PreparingNode);owner.close();transport.drain();QVERIFY(transport.quiescent());
    }
    void realWorkersKeepChecksWritesAndDecisionsOffGui() {
        auto drain=DrainExecutorsBeforeNativeOwnerDestruction();
        BackendExecutor executor{execution::Limits::Bounded()};
        std::atomic<int> phase{static_cast<int>(onboarding::Phase::Drafting)};
        std::atomic_bool worker_only{true},drained{false};std::atomic<int> writes{0};
        const auto* gui=QThread::currentThread();
        onboarding::Coordinator owner{{executor.scheduler(),[](auto turn){QMetaObject::invokeMethod(QCoreApplication::instance(),std::move(turn),Qt::QueuedConnection);}},
            [&](const auto& frame){if(QThread::currentThread()==gui)worker_only=false;phase=static_cast<int>(frame.phase);},[&]{drained=true;}};
        owner.check({1,true,true,true,{}},[&]{if(QThread::currentThread()==gui)worker_only=false;return QString{};});
        QTRY_COMPARE(phase.load(),static_cast<int>(onboarding::Phase::Ready));
        onboarding::Ports ports;ports.write=[&](const auto&){if(QThread::currentThread()==gui)worker_only=false;++writes;return Saved();};
        QVERIFY(owner.finish(1,Request(),ports));QTRY_COMPARE(phase.load(),static_cast<int>(onboarding::Phase::PreparingNode));
        QCOMPARE(writes.load(),1);QVERIFY(worker_only.load());owner.close();QTRY_VERIFY(drained.load());
    }

    void facadeCapturesOriginalWriteAndRejectsSuccessors() {
        ManualOnboarding f;QVERIFY(f.directory.isValid());QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        f.model->setListen(false);QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        int writes{0};QString written_path;bool written_listen=true;
        onboarding::Ports ports;ports.write=[&](const auto& request){++writes;written_path=request.data_dir.path;written_listen=request.values.listen;return Saved();};
        f.model->setProfilePorts(ports);QSignalSpy committed{f.model.get(),&OnboardingOptionsModel::settingsCommitted};
        f.model->commitSettings();QVERIFY(f.model->setupCommitPending());QVERIFY(!f.model->canFinish());
        f.model->commitSettings();QVERIFY(!f.model->selectCustomDataDir(QStringLiteral("/another-profile")));
        f.model->setListen(true);QCOMPARE(writes,0);
        QVERIFY(f.execution.step(execution::Lane::Domain));QVERIFY(f.execution.step(execution::Lane::Core));
        QCOMPARE(writes,1);QCOMPARE(written_path,f.directory.path());QVERIFY(!written_listen);QVERIFY(committed.empty());
        QVERIFY(!f.model->setupCommitted());QVERIFY(f.model->setupCommitPending());
        QVERIFY(f.settle());QCOMPARE(committed.size(),1);QVERIFY(f.model->setupCommitted());
        f.model->commitSettings();QVERIFY(f.settle());QCOMPARE(writes,1);
        f.model->beginShutdown();QVERIFY(f.settle());QVERIFY(f.execution.quiescent());
    }
    void facadePartialAndUnknownKeepOriginalOwnership_data() {
        QTest::addColumn<bool>("unknown");QTest::newRow("known-partial")<<false;QTest::newRow("unknown-effect")<<true;
    }
    void facadePartialAndUnknownKeepOriginalOwnership() {
        QFETCH(bool,unknown);ManualOnboarding f;QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        int writes{0},queries{0},completions{0},preparations{0};bool query_fails=true,original_fact=false;
        onboarding::Ports ports;
        ports.write=[&](const auto&)->QmlOnboardingSettings::ProfileResult{++writes;if(unknown)throw std::runtime_error("lost completion");auto result=Saved();result.completed=false;result.cleanup_completed=false;result.error=QStringLiteral("legacy source changed");return result;};
        ports.complete=[&](const auto& original){++completions;original_fact=original.replacement_written;return Saved();};
        ports.query=[&](const auto& original){++queries;original_fact=original.uncertain;if(query_fails)throw std::runtime_error("read failed");return Saved();};
        f.model->setProfilePorts(ports);QSignalSpy committed{f.model.get(),&OnboardingOptionsModel::settingsCommitted};
        f.model->commitSettings();QVERIFY(f.settle());QCOMPARE(f.model->setupUncertain(),unknown);QVERIFY(!f.model->canFinish());
        const auto attempt=f.model->setupAttemptId();
        const auto original_path=f.model->dataDir();QVERIFY(!f.model->selectCustomDataDir(QStringLiteral("/another-profile")));
        f.model->useDefaultDataDir();QVERIFY(f.settle());QCOMPARE(f.model->dataDir(),original_path);
        f.model->commitSettings();f.model->prepareNode(f.args,[&]{++preparations;return true;});QVERIFY(f.settle());
        QCOMPARE(writes,1);QCOMPARE(preparations,0);
        if(unknown){
            f.model->querySetupOutcome();QVERIFY(f.settle());QCOMPARE(queries,1);QVERIFY(f.model->setupUncertain());
            query_fails=false;f.model->querySetupOutcome();
        }else{QVERIFY(f.model->setupPartial());f.model->completeSavedSetup();}
        QVERIFY(f.settle());QCOMPARE(writes,1);QVERIFY(original_fact);QCOMPARE(queries,unknown?2:0);QCOMPARE(completions,unknown?0:1);
        QVERIFY(f.model->setupCommitted());QCOMPARE(committed.size(),1);QCOMPARE(f.model->setupAttemptId(),attempt);
        f.model->beginShutdown();QVERIFY(f.settle());QVERIFY(f.execution.quiescent());
    }
    void facadeDeletionRetainsNativeWriteReceipt() {
        ManualOnboarding f;QVERIFY(f.settle());QVERIFY(f.model->canFinish());int writes{0};
        onboarding::Ports ports;ports.write=[&](const auto&){++writes;return Saved();};f.model->setProfilePorts(ports);
        const auto receipt=f.model->setupCommitReceipt();f.model->commitSettings();
        QVERIFY(f.execution.step(execution::Lane::Domain));QVERIFY(f.execution.step(execution::Lane::Core));QCOMPARE(writes,1);
        QCOMPARE(receipt->load(),int(OnboardingOptionsModel::SetupCommitted));
        f.model.reset();QVERIFY(f.settle());QCOMPARE(writes,1);QCOMPARE(receipt->load(),int(OnboardingOptionsModel::SetupCommitted));QVERIFY(f.execution.quiescent());
    }
    void facadePreparationCloseAccountsOutcome_data() {
        QTest::addColumn<QString>("outcome");QTest::addColumn<bool>("close_before_core");
        for(const auto* outcome:{"success","failure","throw"})for(bool before:{false,true})
            QTest::newRow(qPrintable(QString::fromLatin1(outcome)+(before?QStringLiteral("-close-before-core"):QStringLiteral("-close-after-core"))))<<QString::fromLatin1(outcome)<<before;
    }
    void facadePreparationCloseAccountsOutcome() {
        QFETCH(QString,outcome);QFETCH(bool,close_before_core);ManualOnboarding f;QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        f.model->configureSettingsCommit(f.args);f.model->commitSettings();QVERIFY(f.settle());QVERIFY(f.model->setupCommitted());
        QSignalSpy prepared{f.model.get(),&OnboardingOptionsModel::nodePrepared},shutdown{f.model.get(),&OnboardingOptionsModel::shutdownFinished};QStringList order;int calls{0};
        connect(f.model.get(),&OnboardingOptionsModel::nodePrepared,this,[&]{order<<QStringLiteral("prepared");});
        connect(f.model.get(),&OnboardingOptionsModel::shutdownFinished,this,[&]{order<<QStringLiteral("shutdown");});
        const auto disconnect_before_captures_die=qScopeGuard([&]{if(f.model)disconnect(f.model.get(),nullptr,this,nullptr);});
        f.model->prepareNode(f.args,[&]{++calls;if(outcome=="throw")throw std::runtime_error("original preparation failed");return outcome=="success";});
        QVERIFY(f.execution.step(execution::Lane::Domain));
        if(close_before_core){f.model->beginShutdown();while(f.execution.step(execution::Lane::Domain)){};}
        QVERIFY(f.execution.step(execution::Lane::Core));QCOMPARE(calls,1);
        if(!close_before_core)f.model->beginShutdown();
        QVERIFY(prepared.empty());QVERIFY(shutdown.empty());QVERIFY(f.settle());
        QCOMPARE(prepared.size(),1);QCOMPARE(shutdown.size(),1);QCOMPARE(order,QStringList({QStringLiteral("prepared"),QStringLiteral("shutdown")}));
        QCOMPARE(prepared.front().at(0).toBool(),outcome=="success");
        if(outcome=="throw"){QCOMPARE(f.model->preparationState(),OnboardingOptionsModel::PreparationUncertain);QCOMPARE(prepared.front().at(1).toString(),QStringLiteral("original preparation failed"));}
        QVERIFY(f.execution.quiescent());
    }
    void facadePreparationSignalWaitsForFlowCleanup() {
        ManualOnboarding f;QVERIFY(f.settle());QVERIFY(f.model->canFinish());f.model->configureSettingsCommit(f.args);f.model->commitSettings();QVERIFY(f.settle());
        QSignalSpy prepared{f.model.get(),&OnboardingOptionsModel::nodePrepared},shutdown{f.model.get(),&OnboardingOptionsModel::shutdownFinished};
        f.model->prepareNode(f.args,[]{return true;});QVERIFY(f.execution.step(execution::Lane::Domain));QVERIFY(f.execution.step(execution::Lane::Core));
        while(f.execution.step(execution::Lane::Domain)){};
        while(f.publish()){};
        QVERIFY(prepared.empty());QVERIFY(shutdown.empty());f.model->beginShutdown();
        QVERIFY(f.settle());QCOMPARE(prepared.size(),1);QCOMPARE(shutdown.size(),1);QVERIFY(f.execution.quiescent());
    }
    void facadeRejectedChecksPermitExplicitRetry() {
        ManualOnboarding f;QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        f.execution.rejectLane(execution::Lane::Domain);f.model->retrySetupChecks();QVERIFY(!f.model->canFinish());
        QVERIFY(!f.model->setupCommitError().isEmpty());QVERIFY(f.settle());QVERIFY(!f.model->canFinish());
        f.model->retrySetupChecks();QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        f.model->beginShutdown();QVERIFY(f.settle());QVERIFY(f.execution.quiescent());
    }
    void facadeRejectedPreparationIsKnownNotStarted_data() {
        QTest::addColumn<bool>("ordinary_closed");QTest::newRow("native-transport-rejected")<<false;QTest::newRow("root-ordinary-closed")<<true;
    }
    void facadeRejectedPreparationIsKnownNotStarted() {
        QFETCH(bool,ordinary_closed);ManualOnboarding f;QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        f.model->configureSettingsCommit(f.args);f.model->commitSettings();QVERIFY(f.settle());int calls{0};
        QSignalSpy prepared{f.model.get(),&OnboardingOptionsModel::nodePrepared};
        if(ordinary_closed)f.execution.scheduler->closeOrdinaryAdmission();else f.execution.rejectLane(execution::Lane::Core);
        f.model->prepareNode(f.args,[&]{++calls;return true;});QVERIFY(f.settle());
        QCOMPARE(calls,0);QCOMPARE(prepared.size(),1);QVERIFY(!prepared.front().at(0).toBool());
        QCOMPARE(f.model->preparationState(),OnboardingOptionsModel::PreparationFailed);QVERIFY(f.model->setupCommitted());
        f.model->beginShutdown();QVERIFY(f.settle());QVERIFY(f.execution.quiescent());
    }

    void rejectedRecoveryOwnerHasNoOutstandingDrain() {
        auto limits=execution::Limits::Bounded();limits.cleanup_reservations.tasks=0;
        ManualRoutedExecution transport{limits};int drained{0};QList<onboarding::Projection> frames;
        onboarding::Coordinator owner{{transport.scheduler,[](auto turn){turn();}},[&](auto frame){frames.push_back(std::move(frame));},[&]{++drained;}};
        QCOMPARE(drained,1);QCOMPARE(frames.size(),1);QCOMPARE(frames.front().phase,onboarding::Phase::Failed);
        QVERIFY(!owner.finish(1,Request(),{}));owner.close();transport.drain();QCOMPARE(drained,1);QVERIFY(transport.quiescent());
    }
    void facadeRejectedRecoveryOwnerStillFinishesShutdown() {
        auto limits=execution::Limits::Bounded();limits.cleanup_reservations.tasks=0;
        ManualRoutedExecution transport{limits};std::deque<std::function<void()>> gui;QTemporaryDir directory;
        OnboardingOptionsModel model{{"bitcoin-qt","-regtest","-datadir="+directory.path().toStdString()},false,nullptr,{transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}}};
        QSignalSpy shutdown{&model,&OnboardingOptionsModel::shutdownFinished};model.beginShutdown();
        for(int i=0;i<100;++i){if(transport.stepNext())continue;if(gui.empty())break;auto turn=std::move(gui.front());gui.pop_front();turn();}
        QCOMPARE(shutdown.size(),1);QVERIFY(transport.quiescent());
    }

};
#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(OnboardingFlowTests)
#else
QTEST_MAIN(OnboardingFlowTests)
#endif
#include <test_onboardingflow.moc>
