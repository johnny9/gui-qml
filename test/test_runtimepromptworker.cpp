// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/runtimepromptworker.h>
#include <qml/models/runtimepromptmodel.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <node/interface_ui.h>
#include <QtTest/QtTest>
#include <QSemaphore>
#include <QScopeGuard>
#include <thread>
#include <deque>

namespace {
using namespace runtimeprompt;
constexpr uint32_t OK{CClientUIInterface::BTN_OK},CANCEL{CClientUIInterface::BTN_CANCEL};
struct Fixture {
    ManualRoutedExecution execution;
    std::deque<std::function<void()>> gui;
    int publications{0},drained{0};bool reject_projection{false};
    std::shared_ptr<Service> service;
    explicit Fixture(execution::Limits limits=execution::Limits::Bounded()):execution(limits){service=std::make_shared<Service>(execution::SnapshotServices{execution.scheduler,{}},[this]{
        if(reject_projection)throw std::runtime_error{"presentation rejected"};
        gui.push_back([this]{++publications;});},[this]{++drained;});}
    ~Fixture(){service->close();execution.drain();service.reset();execution.drain();}
    std::shared_ptr<const Request> request(QString summary=QStringLiteral("Original question"),uint32_t buttons=OK|CANCEL){
        return std::make_shared<const Request>(Origin::Runtime,buttons|CClientUIInterface::MODAL,true,std::move(summary));}
    void settle(){execution.drain();while(!gui.empty()){auto turn=std::move(gui.front());gui.pop_front();turn();}}
};
}
class RuntimePromptWorkerTests:public QObject {
    Q_OBJECT
private Q_SLOTS:
    void acceptedAnswerSurvivesCloseBeforeNativeReturn(){
        Fixture f;const auto first=f.request(),second=f.request(QStringLiteral("Second question"));
        std::shared_ptr<Continuation> one,two;QVERIFY(f.service->enqueue(first,true,one));QVERIFY(f.service->enqueue(second,true,two));
        f.settle();const auto id=f.service->projection().active->descriptor.id;
        QVERIFY(f.service->visible(id));f.settle();QVERIFY(f.service->answer(id,OK));f.execution.drain();
        f.service->close(); // Native has not reacquired its answer or acknowledged return.
        QVERIFY(one->wait().affirmative());QVERIFY(!two->wait().affirmative());f.execution.drain();
        QCOMPARE(f.service->projection().state,State::Stopped);QCOMPARE(f.service->projection().pending,size_t{0});
        one.reset();two.reset();f.execution.drain();QCOMPARE(f.drained,1);QCOMPARE(f.execution.scheduler->pending(),size_t{0});
    }
    void staleQtHandleCannotAnswerItsSuccessor(){
        Fixture f;std::shared_ptr<Continuation> one,two;const auto first=f.request(),second=f.request(QStringLiteral("Second question"));
        QVERIFY(f.service->enqueue(first,true,one));QVERIFY(f.service->enqueue(second,true,two));f.settle();
        RuntimePromptModel old{f.service->projection().active,f.service,nullptr};QVERIFY(old.presented());f.settle();
        QVERIFY(old.answer(OK));f.execution.drain();QVERIFY(one->wait().affirmative());f.settle();
        const auto current=f.service->projection().active;QVERIFY(current);QVERIFY(current->descriptor.id!=old.id());
        RuntimePromptModel next{current,f.service,nullptr};QVERIFY(next.presented());f.settle();
        QVERIFY(old.answer(OK));f.settle();QVERIFY(!second->answer->result());QCOMPARE(f.service->projection().last,Disposition::StaleToken);
        QVERIFY(next.answer(CANCEL));f.execution.drain();QVERIFY(!two->wait().affirmative());f.settle();
        QCOMPARE(f.service->projection().state,State::Idle);
    }
    void invalidCompositeAndEarlyAnswersDoNotConsumeQuestion(){
        Fixture f;std::shared_ptr<Continuation> completion;const auto request=f.request();QVERIFY(f.service->enqueue(request,true,completion));f.settle();
        const auto id=f.service->projection().active->descriptor.id;QVERIFY(f.service->answer(id,OK));f.settle();
        QCOMPARE(f.service->projection().last,Disposition::NotReady);QVERIFY(!request->answer->result());
        QVERIFY(f.service->visible(id));f.settle();QVERIFY(f.service->answer(id,OK|CANCEL));f.settle();
        QCOMPARE(f.service->projection().last,Disposition::InvalidButton);QVERIFY(!request->answer->result());
        QVERIFY(f.service->answer(id,CClientUIInterface::BTN_YES));f.settle();QVERIFY(!request->answer->result());
        QVERIFY(f.service->answer(id,OK));f.execution.drain();QVERIFY(completion->wait().affirmative());f.settle();
    }
    void heldGuiDeliveryDoesNotBlockNativeRetirement(){
        Fixture f;std::shared_ptr<Continuation> continuation;const auto request=f.request();QVERIFY(f.service->enqueue(request,true,continuation));f.execution.drain();
        QVERIFY(!f.gui.empty());f.service->close();f.execution.drain();QCOMPARE(f.drained,0);
        QCOMPARE(f.service->projection().state,State::Stopping);QVERIFY(!continuation->wait().affirmative());continuation.reset();f.execution.drain();
        QCOMPARE(f.drained,1);QCOMPARE(f.execution.scheduler->pending(),size_t{0});QVERIFY(!f.gui.empty());
    }
    void nonblockingMessageRetiresOnlyAfterExplicitDisposition(){
        Fixture f;const auto request=std::make_shared<const Request>(Origin::Runtime,OK,false,QStringLiteral("Original information"));
        std::shared_ptr<Continuation> unused;QVERIFY(f.service->enqueue(request,false,unused));QVERIFY(!unused);f.settle();
        const auto id=f.service->projection().active->descriptor.id;QVERIFY(!request->answer->result());
        QVERIFY(f.service->unavailable(id));f.settle();QVERIFY(request->answer->result());
        QCOMPARE(f.service->projection().state,State::Idle);QCOMPARE(f.service->projection().pending,size_t{0});
    }
    void presentationRetirementCancelsOnlyItsOriginalWaitingQuestion(){
        Fixture f;const auto first=f.request(),second=f.request(QStringLiteral("Successor"));std::shared_ptr<Continuation> one,two;
        QVERIFY(f.service->enqueue(first,true,one));QVERIFY(f.service->enqueue(second,true,two));f.settle();
        RuntimePromptModel retired{f.service->projection().active,f.service,nullptr};QVERIFY(retired.presented());f.settle();
        QVERIFY(retired.unavailable());f.execution.drain();QVERIFY(!one->wait().affirmative());f.settle();
        const auto successor=f.service->projection().active;QVERIFY(successor);QVERIFY(successor->descriptor.id!=retired.id());
        QVERIFY(retired.unavailable());f.settle();QVERIFY(!second->answer->result());
        f.service->close();QVERIFY(!two->wait().affirmative());f.execution.drain();QCOMPARE(f.drained,1);
    }
    void fixedIngressBudgetRejectsWithoutWaitingOrReplacingTheFirst(){
        Fixture f;std::vector<std::shared_ptr<Continuation>> continuations;continuations.reserve(32);
        for(int i=0;i<32;++i){std::shared_ptr<Continuation> continuation;QVERIFY(f.service->enqueue(f.request(QString::number(i)),true,continuation));continuations.push_back(std::move(continuation));}
        const auto overflow=f.request(QStringLiteral("Overflow"));std::shared_ptr<Continuation> rejected;
        QVERIFY(!f.service->enqueue(overflow,true,rejected));QVERIFY(!rejected);QVERIFY(overflow->answer->result());f.settle();
        QCOMPARE(f.service->projection().pending,size_t{32});QCOMPARE(f.service->projection().active->request->summary,QStringLiteral("0"));
        f.service->close();for(const auto& continuation:continuations)QVERIFY(!continuation->wait().affirmative());continuations.clear();f.execution.drain();
        QCOMPARE(f.drained,1);QCOMPARE(f.execution.scheduler->pending(),size_t{0});
    }
    void rejectedPublicationRetainsOriginalPromptAndNativeAnswer(){
        Fixture f;f.reject_projection=true;std::shared_ptr<Continuation> continuation;const auto request=f.request();
        QVERIFY(f.service->enqueue(request,true,continuation));f.execution.drain();const auto id=f.service->projection().active->descriptor.id;
        QVERIFY(f.service->visible(id));f.execution.drain();QVERIFY(f.service->answer(id,OK));f.execution.drain();
        QVERIFY(continuation->wait().affirmative());f.execution.drain();QCOMPARE(f.service->projection().state,State::Idle);QCOMPARE(f.publications,0);
    }
    void nativeWaitUsesOnlyItsOriginalAnswerCell(){
        Fixture f;const auto request=f.request();std::shared_ptr<Continuation> continuation;QVERIFY(f.service->enqueue(request,true,continuation));f.settle();
        QSemaphore entered,returned;bool affirmative{false};std::thread producer{[&]{entered.release();affirmative=continuation->wait().affirmative();returned.release();}};
        const auto cleanup=qScopeGuard([&]{f.service->close();f.execution.drain();producer.join();});
        QVERIFY(entered.tryAcquire(1,5000));QCOMPARE(returned.available(),0);
        const auto id=f.service->projection().active->descriptor.id;QVERIFY(f.service->visible(id));f.settle();QVERIFY(f.service->answer(id,OK));f.execution.drain();
        QVERIFY(returned.tryAcquire(1,5000));f.settle();QVERIFY(affirmative);QCOMPARE(f.service->projection().state,State::Idle);
    }
    void saturatedOrdinaryAdmissionCannotStrandTheOriginalQuestionReturn(){
        auto limits=execution::Limits::Bounded();limits.ordinary.tasks=1;Fixture f{limits};
        const auto request=f.request();std::shared_ptr<Continuation> continuation;QVERIFY(f.service->enqueue(request,true,continuation));f.settle();
        const auto id=f.service->projection().active->descriptor.id;QVERIFY(f.service->visible(id));f.settle();
        int effects{0};QVERIFY(execution::Submit(*f.execution.scheduler,[&]{++effects;},[]{}));
        const auto held=f.execution.dispatchNext(execution::Lane::Core);QVERIFY(held);
        QVERIFY(!f.service->answer(id,OK));QVERIFY(!continuation->wait().affirmative());continuation.reset();
        for(int turn=0;turn<16;++turn){const bool cleanup=f.execution.step(execution::Lane::Cleanup);const bool domain=f.execution.step(execution::Lane::Domain);if(!cleanup&&!domain)break;}
        QCOMPARE(effects,0);QCOMPARE(f.service->projection().pending,size_t{0});QVERIFY(f.service->projection().rejected>0);
        f.service->close();
        for(int turn=0;turn<16;++turn){const bool cleanup=f.execution.step(execution::Lane::Cleanup);const bool domain=f.execution.step(execution::Lane::Domain);if(!cleanup&&!domain)break;}
        QCOMPARE(f.drained,1);QCOMPARE(effects,0);QVERIFY(f.execution.finishWork(*held));f.settle();QCOMPARE(effects,1);
        QCOMPARE(f.execution.scheduler->pending(),size_t{0});
    }
    void closedRootRejectsConstructionWithoutAnEmptySuccess(){
        ManualRoutedExecution execution;execution.scheduler->closeOrdinaryAdmission();
        Service service{{execution.scheduler,{}},[] {}};std::shared_ptr<Continuation> continuation;
        const auto request=std::make_shared<const Request>(Origin::Runtime,OK,true,QStringLiteral("Unadmitted"));
        QVERIFY(!service.enqueue(request,true,continuation));QVERIFY(!continuation);QVERIFY(request->answer->result());
        QCOMPARE(service.projection().state,State::Stopped);QCOMPARE(service.projection().last,Disposition::NotReady);execution.drain();
    }
};
#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(RuntimePromptWorkerTests)
#else
QTEST_MAIN(RuntimePromptWorkerTests)
#endif
#include <test_runtimepromptworker.moc>
