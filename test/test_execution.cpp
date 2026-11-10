// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/execution.h>
#include <qml/models/feereadstate.h>
#include <qml/walletlifecyclestate.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>

#include <QtTest/QtTest>
#include <QScopeGuard>

#include <stdexcept>

class ExecutionTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void cancelledTransportStaysBoundedWhileResourceCleanupProgresses()
    {
        using namespace execution;
        auto limits = Limits::Bounded(); limits.ordinary.tasks = 1;
        ManualRoutedExecution env{limits};
        int effects{0}, destroyed{0}, failures{0}, retired{0};
        const auto cleanup = qScopeGuard([&] { env.scheduler->closeAdmission(); env.drain(); });
        const auto key = env.scheduler->allocateKey();
        const auto reservation = Submit(*env.scheduler, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, 0, key}, true);
        QVERIFY(reservation);
        struct Resource {
            int& destroyed;
            explicit Resource(int& count) : destroyed(count) {}
            ~Resource() { ++destroyed; }
        };
        const auto work = Submit(*env.scheduler, [owned = std::make_unique<Resource>(destroyed), &effects] { ++effects; }, [] {},
            [&](std::exception_ptr error) {
                try { std::rethrow_exception(error); }
                catch (const NotStarted& failure) { if (failure.cause == NotStarted::Cause::Cancelled) ++failures; }
            }, {Lane::Core, 0, key});
        QVERIFY(work); QVERIFY(env.scheduler->cancelQueued(work.id));
        QVERIFY(env.step(Lane::Cleanup));
        QCOMPARE(destroyed, 1); QCOMPARE(effects, 0);
        QVERIFY(env.step(Lane::Domain));
        QCOMPARE(failures, 1);
        QCOMPARE(env.scheduler->phase(work.id), std::make_optional(Phase::AwaitingTransport));
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}).rejection, Rejection::Capacity);
        QVERIFY(env.scheduler->activateCleanup(reservation.id));
        QVERIFY(env.step(Lane::Cleanup)); QVERIFY(env.step(Lane::Domain));
        QCOMPARE(retired, 1); QCOMPARE(env.scheduler->pending(), size_t{1});
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}).rejection, Rejection::Capacity);
        QVERIFY(!env.scheduler->cancelQueued(work.id));
        QVERIFY(env.step(Lane::Core)); // Dequeue the original now-empty turn.
        QCOMPARE(effects, 0); QVERIFY(env.quiescent());
        QVERIFY(Submit(*env.scheduler, [] {}, [] {})); env.drain();
        QVERIFY(env.quiescent());
    }
    void reservedCleanupWaitsForOutcomesAndNativeLeases()
    {
        using namespace execution;
        auto limits = Limits::Bounded(); limits.resource_borrowers = 1;
        ManualRoutedExecution env{limits};
        int effects{0}, outcomes{0}, cleanup_effects{0}, cleanup_outcomes{0};
        const auto cleanup = qScopeGuard([&] { env.scheduler->closeAdmission(); env.drain(); });
        const auto key = env.scheduler->allocateKey();
        const auto reserved = Submit(*env.scheduler, [&] { ++cleanup_effects; }, [&] { ++cleanup_outcomes; }, {}, {Lane::Cleanup, 0, key}, true);
        QVERIFY(reserved);
        QCOMPARE(env.scheduler->phase(reserved.id), std::make_optional(Phase::Reserved));
        QVERIFY(env.queued.empty());
        QVERIFY(!BorrowAdmission{});
        QCOMPARE(env.scheduler->borrow({999, 1}).rejection, Rejection::Invalid);
        auto borrowed = env.scheduler->borrow(key);
        QVERIFY(borrowed);
        QCOMPARE(env.scheduler->borrow(key).rejection, Rejection::Capacity);
        std::optional<ResourceLease> moved{std::move(*borrowed.lease)};
        borrowed.lease.reset();
        QCOMPARE(env.scheduler->pending(), size_t{2});
        const auto work = Submit(*env.scheduler, [&] { ++effects; }, [&] { ++outcomes; }, {}, {Lane::Core, 0, key});
        QVERIFY(work);
        QVERIFY(env.scheduler->activateCleanup(reserved.id));
        QVERIFY(!env.scheduler->activateCleanup(reserved.id));
        QVERIFY(!env.scheduler->cancelQueued(reserved.id));
        QCOMPARE(env.scheduler->borrow(key).rejection, Rejection::Closed);
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}, {}, {Lane::Core, 0, key}).rejection, Rejection::Closed);
        QVERIFY(env.step(Lane::Core));
        QCOMPARE(effects, 1); QCOMPARE(outcomes, 0);
        moved.reset();
        QVERIFY(!env.step(Lane::Cleanup)); // Produced result is not yet consumed.
        QVERIFY(env.step(Lane::Domain));
        QCOMPARE(outcomes, 1);
        QVERIFY(!env.scheduler->cancelQueued(reserved.id));
        QVERIFY(env.step(Lane::Cleanup));
        QCOMPARE(cleanup_effects, 1); QCOMPARE(cleanup_outcomes, 0);
        QVERIFY(env.step(Lane::Domain));
        QCOMPARE(cleanup_outcomes, 1);
        QVERIFY(env.quiescent());
        QCOMPARE(env.scheduler->borrow(key).rejection, Rejection::Invalid);
    }
    void parentClosureActivatesCleanupWithoutAnotherAdmission()
    {
        using namespace execution;
        ManualRoutedExecution env;
        int retired{0};
        const auto cleanup = qScopeGuard([&] { env.scheduler->closeAdmission(); env.drain(); });
        const auto first = env.scheduler->allocateKey(), second = env.scheduler->allocateKey();
        QVERIFY(first != second);
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}, {}, {Lane::Core, 0, first}, true).rejection, Rejection::Invalid);
        const auto reserved = Submit(*env.scheduler, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, 0, first}, true);
        QVERIFY(reserved);
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}, {}, {Lane::Cleanup, 0, first}, true).rejection, Rejection::Invalid);
        auto borrowed = env.scheduler->borrow(first);
        QVERIFY(borrowed);
        env.scheduler->closeOrdinaryAdmission();
        QCOMPARE(env.scheduler->borrow(first).rejection, Rejection::Closed);
        env.scheduler->closeAdmission();
        QCOMPARE(env.scheduler->borrow(first).rejection, Rejection::Closed);
        QVERIFY(!env.step(Lane::Cleanup));
        borrowed.lease.reset();
        env.drain();
        QCOMPARE(retired, 1);
        QVERIFY(env.quiescent());
        QVERIFY(!env.scheduler->activateCleanup(reserved.id));

        auto limits = Limits::Bounded();
        limits.first_resource_owner = std::numeric_limits<uint64_t>::max();
        ManualRoutedExecution exhausted{limits};
        QCOMPARE(exhausted.scheduler->allocateKey().owner, std::numeric_limits<uint64_t>::max());
        bool rejected{false};
        try { exhausted.scheduler->allocateKey(); }
        catch (const std::overflow_error&) { rejected = true; }
        QVERIFY(rejected);
        QVERIFY(exhausted.quiescent());
    }
    void subscriptionDecisionsDoNotResurrectAfterRetirement()
    {
        using namespace walletlife;
        State state;
        QCOMPARE(state.phase(), Phase::Registering);
        QVERIFY(!state.apply(Event::Notification));
        QVERIFY(!state.apply(Event::CleanupFinished));
        QCOMPARE(state.apply(Event::Registered), std::make_optional(Publication::Ready));
        QCOMPARE(state.phase(), Phase::Active);
        QVERIFY(!state.apply(Event::Registered));
        QCOMPARE(state.apply(Event::Notification), std::make_optional(Publication::Changed));
        QCOMPARE(state.apply(Event::RegistrationFailed), std::make_optional(Publication::Failed));
        QCOMPARE(state.phase(), Phase::Failed);
        QVERIFY(!state.apply(Event::Notification));
        QVERIFY(!state.apply(Event::RegistrationFailed));
        state.apply(Event::Close);
        QCOMPARE(state.phase(), Phase::Closing);
        QVERIFY(!state.apply(Event::Registered));
        QVERIFY(!state.apply(Event::RegistrationFailed));
        QCOMPARE(state.apply(Event::CleanupFinished), std::make_optional(Publication::Retired));
        QCOMPARE(state.phase(), Phase::Closed);
        for (auto event : {Event::Registered, Event::RegistrationFailed, Event::Notification, Event::Close, Event::CleanupFinished, static_cast<Event>(255)}) {
            QVERIFY(!state.apply(event));
            QCOMPARE(state.phase(), Phase::Closed);
        }
        State closed_during_registration;
        closed_during_registration.apply(Event::Close);
        QVERIFY(!closed_during_registration.apply(Event::Registered));
        QVERIFY(!closed_during_registration.apply(Event::Close));
        QCOMPARE(closed_during_registration.apply(Event::CleanupFinished), std::make_optional(Publication::Retired));
        State failed_registration;
        QCOMPARE(failed_registration.apply(Event::RegistrationFailed), std::make_optional(Publication::Failed));
        QCOMPARE(failed_registration.phase(), Phase::Failed);
        State closed_active;
        closed_active.apply(Event::Registered); closed_active.apply(Event::Close);
        QCOMPARE(closed_active.phase(), Phase::Closing);
    }
    void observationSaturationKeepsActionCapacityAndBothClassesProgress()
    {
        using namespace execution;
        ManualRoutedExecution env{Limits{{8, 8}, {1, 0}, {1, 0}, {1, 1}, 2}};
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        std::vector<int> effects;
        const auto submit = [&](int value, WorkClass kind, size_t bytes) {
            return Submit(*env.scheduler, [&, value] { effects.push_back(value); }, [] {}, {},
                          {Lane::Core, bytes, {1, static_cast<uint64_t>(value)}, kind});
        };
        QVERIFY(submit(1, WorkClass::Action, 0));
        QVERIFY(submit(9, WorkClass::Observation, 1));
        QCOMPARE(submit(10, WorkClass::Observation, 0).rejection, Rejection::Capacity);
        QVERIFY(submit(2, WorkClass::Action, 0));
        QVERIFY(submit(3, WorkClass::Action, 0));
        QVERIFY(submit(4, WorkClass::Action, 0));
        while (env.step(Lane::Core)) {}
        QCOMPARE(effects, std::vector<int>({1, 2, 9, 3, 4}));
        env.drain();
        QVERIFY(env.quiescent());
        env.reject_lane = Lane::Core;
        QCOMPARE(submit(11, WorkClass::Observation, 1).rejection, Rejection::Transport);
        QVERIFY(submit(12, WorkClass::Observation, 1));
        env.drain();
        QCOMPARE(effects.back(), 12);
        QVERIFY(env.quiescent());
        QCOMPARE(submit(13, static_cast<WorkClass>(255), 0).rejection, Rejection::Invalid);
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}, {}, {Lane::Control, 0, {}, WorkClass::Observation}).rejection, Rejection::Invalid);
    }
    void keyedWorkIsFairAndOutcomeConsumptionIsSeparate()
    {
        using namespace execution;
        ManualRoutedExecution env;
        std::vector<int> effects, outcomes;
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        const auto submit = [&](ResourceKey key, int value) {
            return Submit(*env.scheduler, [&, value] { effects.push_back(value); return value * 10; },
                          [&](int result) { outcomes.push_back(result); }, {}, {Lane::Core, 0, key});
        };
        QVERIFY(submit({1, 1}, 1));
        QVERIFY(submit({1, 1}, 2));
        QVERIFY(submit({2, 1}, 3));
        QVERIFY(submit({3, 1}, 4));
        const auto first = env.dispatchNext(Lane::Core);
        QVERIFY(first);
        QVERIFY(effects.empty());
        QVERIFY(!env.dispatchNext(Lane::Core));
        QVERIFY(env.finishWork(*first));
        while (env.step(Lane::Core)) {}
        QCOMPARE(effects, std::vector<int>({1, 3, 4, 2}));
        QVERIFY(outcomes.empty());
        QCOMPARE(env.scheduler->pending(), size_t{4});
        const auto outcome = env.deliverOutcome();
        QVERIFY(outcome);
        QVERIFY(outcomes.empty());
        QVERIFY(env.stepMailbox(*outcome));
        QCOMPARE(outcomes, std::vector<int>({10}));
        env.drain();
        QCOMPARE(outcomes, std::vector<int>({10, 30, 40, 20}));
        QVERIFY(env.quiescent());
        QVERIFY(!env.finishWork({999}));
        QVERIFY(!env.stepMailbox({999}));
        QVERIFY(!env.deliverOutcome());
    }
    void independentKeysCanFinishOutOfOrderWhileControlRemainsAvailable()
    {
        using namespace execution;
        ManualRoutedExecution env{Limits{{3, 9}, {1, 1}, {1, 1}}, 2};
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        std::vector<int> effects;
        int outcomes{0}, control{0}, retired{0};
        const auto submit = [&](ResourceKey key, int value) {
            return Submit(*env.scheduler, [&, value] { effects.push_back(value); }, [&] { ++outcomes; }, {}, {Lane::Core, 3, key});
        };
        const auto a = submit({1, 1}, 1), a2 = submit({1, 1}, 2), b = submit({2, 1}, 3);
        QVERIFY(a && a2 && b);
        QCOMPARE(submit({3, 1}, 4).rejection, Rejection::Capacity);
        QCOMPARE(env.dispatchNext(Lane::Core), std::make_optional(a.id));
        QCOMPARE(env.dispatchNext(Lane::Core), std::make_optional(b.id));
        QVERIFY(!env.dispatchNext(Lane::Core));
        env.scheduler->closeOrdinaryAdmission();
        QVERIFY(Submit(*env.scheduler, [&] { ++control; }, [] {}, {}, {Lane::Control, 1}));
        QVERIFY(Submit(*env.scheduler, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, 1}));
        QVERIFY(env.step(Lane::Control));
        QVERIFY(env.step(Lane::Cleanup));
        QCOMPARE(control, 1); QCOMPARE(retired, 1);
        QVERIFY(effects.empty());
        QVERIFY(env.finishWork(b.id));
        QVERIFY(!env.dispatchNext(Lane::Core)); // Same key still held by A.
        QVERIFY(env.finishWork(a.id));
        QCOMPARE(env.dispatchNext(Lane::Core), std::make_optional(a2.id));
        QVERIFY(env.finishWork(a2.id));
        QCOMPARE(effects, std::vector<int>({3, 1, 2}));
        QCOMPARE(outcomes, 0);
        env.drain();
        QCOMPARE(outcomes, 3);
        QVERIFY(env.quiescent());
    }
    void queuedCancellationAndLaterTransportFailureHaveOwnedOutcomes()
    {
        using namespace execution;
        ManualRoutedExecution env;
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        int effects{0}, outcomes{0};
        std::vector<NotStarted::Cause> failures;
        const auto failed = [&](std::exception_ptr error) {
            try { std::rethrow_exception(error); }
            catch (const NotStarted& result) { failures.push_back(result.cause); }
        };
        const auto a = Submit(*env.scheduler, [&] { ++effects; }, [&] { ++outcomes; }, failed);
        const auto b = Submit(*env.scheduler, [&] { ++effects; }, [&] { ++outcomes; }, failed);
        QVERIFY(a && b);
        env.reject_lane = Lane::Core;
        QVERIFY(env.scheduler->cancelQueued(a.id));
        QVERIFY(!env.scheduler->cancelQueued(a.id));
        QVERIFY(!env.scheduler->cancelQueued({999}));
        QVERIFY(env.step(Lane::Core));
        QCOMPARE(effects, 0);
        QCOMPARE(env.scheduler->phase(b.id), std::make_optional(Phase::OutcomeReady));
        QVERIFY(!env.scheduler->cancelQueued(b.id));
        QVERIFY(env.step(Lane::Cleanup));
        QVERIFY(env.step(Lane::Domain));
        QCOMPARE(failures, std::vector<NotStarted::Cause>({NotStarted::Cause::Cancelled}));
        QVERIFY(env.step(Lane::Cleanup));
        QVERIFY(env.step(Lane::Domain));
        QCOMPARE(failures, std::vector<NotStarted::Cause>({NotStarted::Cause::Cancelled, NotStarted::Cause::Transport}));
        QCOMPARE(outcomes, 0);
        QVERIFY(env.quiescent());
        env.reject_lane = Lane::Core;
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}).rejection, Rejection::Transport);
        QVERIFY(env.quiescent());
        bool invalid_transport{false};
        try { std::make_shared<TaskQueue>(TaskQueue::RoutedDispatch{}, Limits::Bounded(), 1); }
        catch (const std::invalid_argument&) { invalid_transport = true; }
        QVERIFY(invalid_transport);
    }
    void boundedAdmissionReservesOutcomeAndCleanupCapacity()
    {
        using namespace execution;
        std::deque<std::function<void()>> turns;
        auto queue = std::make_shared<TaskQueue>([&](Phase, TaskId, std::function<void()> turn) {
            turns.push_back(std::move(turn));
            return true;
        }, Limits{{1, 8}, {1, 2}, {1, 3}});
        int effects{0}, outcomes{0};
        const auto submit = [&](Lane lane, size_t bytes) {
            return Submit(*queue, [&] { ++effects; }, [&] { ++outcomes; }, {}, {lane, bytes});
        };
        QVERIFY(submit(Lane::Core, 8));
        QCOMPARE(submit(Lane::Domain, 0).rejection, Rejection::Capacity);
        queue->closeOrdinaryAdmission();
        QCOMPARE(submit(Lane::Core, 0).rejection, Rejection::Closed);
        QVERIFY(submit(Lane::Control, 2));
        QVERIFY(submit(Lane::Cleanup, 3));
        QCOMPARE(submit(Lane::Control, 0).rejection, Rejection::Capacity);
        QCOMPARE(queue->pending(), size_t{3});
        // Execution cannot surrender a record's reserved outcome capacity.
        auto first = std::move(turns.front()); turns.pop_front(); first();
        QCOMPARE(effects, 1);
        QCOMPARE(outcomes, 0);
        QCOMPARE(queue->pending(), size_t{3});
        queue->closeAdmission();
        QCOMPARE(submit(Lane::Cleanup, 0).rejection, Rejection::Closed);
        while (!turns.empty()) {
            auto turn = std::move(turns.front()); turns.pop_front(); turn();
        }
        QCOMPARE(effects, 3);
        QCOMPARE(outcomes, 3);
        QCOMPARE(queue->pending(), size_t{0});
    }
    void boundedBytesAndTransportRejectionReleaseReservations()
    {
        using namespace execution;
        std::deque<std::function<void()>> turns;
        bool reject{true};
        auto queue = std::make_shared<TaskQueue>([&](Phase, TaskId, std::function<void()> turn) {
            if (std::exchange(reject, false)) return false;
            turns.push_back(std::move(turn));
            return true;
        }, Limits{{3, 8}, {1, 0}, {1, 0}});
        int effects{0}, outcomes{0};
        const auto submit = [&](size_t bytes) {
            return Submit(*queue, [&] { ++effects; }, [&] { ++outcomes; }, {}, {Lane::Core, bytes});
        };
        QCOMPARE(submit(8).rejection, Rejection::Transport);
        QCOMPARE(queue->pending(), size_t{0});
        QVERIFY(submit(7));
        QCOMPARE(submit(2).rejection, Rejection::Capacity);
        QCOMPARE(submit(std::numeric_limits<size_t>::max()).rejection, Rejection::Capacity);
        QVERIFY(submit(1));
        QCOMPARE(submit(1).rejection, Rejection::Capacity);
        while (!turns.empty()) {
            auto turn = std::move(turns.front()); turns.pop_front(); turn();
        }
        QCOMPARE(effects, 2);
        QCOMPARE(outcomes, 2);
        QVERIFY(submit(8));
        while (!turns.empty()) {
            auto turn = std::move(turns.front()); turns.pop_front(); turn();
        }
        QCOMPARE(effects, 3);
        QCOMPARE(outcomes, 3);
        QCOMPARE(queue->pending(), size_t{0});
        QCOMPARE(queue->submit({}, {}).rejection, Rejection::Invalid);
        QCOMPARE(Submit(*queue, [] {}, [] {}, {}, {static_cast<Lane>(255), 0}).rejection, Rejection::Invalid);
        auto bounded = Limits::Bounded();
        QVERIFY(bounded.ordinary.tasks > bounded.control.tasks);
        QVERIFY(bounded.cleanup.payload_bytes > 0);
    }
    void executionOutcomeAndObservationAreSeparate()
    {
        ManualExecutionEnvironment env;
        int effect{0}, domain{0}, observed{0};
        std::function<void()> publication;
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        execution::Admission admitted;
        admitted = execution::Submit(*env.scheduler, [&] {
            if (env.scheduler->phase(admitted.id) == execution::Phase::Executing) effect = 7;
            return 9;
        }, [&](int result) {
            QCOMPARE(env.scheduler->phase(admitted.id), std::make_optional(execution::Phase::Delivering));
            QCOMPARE(env.scheduler->pending(), size_t{1});
            domain = result;
            publication = [&] { observed = domain; };
        });
        QVERIFY(admitted);
        QCOMPARE(effect, 0);
        QCOMPARE(env.scheduler->phase(admitted.id), std::make_optional(execution::Phase::Queued));
        QVERIFY(env.step());
        QCOMPARE(effect, 7);
        QCOMPARE(domain, 0);
        QCOMPARE(env.scheduler->phase(admitted.id), std::make_optional(execution::Phase::OutcomeReady));
        env.scheduler->closeAdmission();
        QCOMPARE(execution::Submit(*env.scheduler, [] {}, [] {}).rejection, execution::Rejection::Closed);
        QVERIFY(env.step());
        QCOMPARE(domain, 9);
        QCOMPARE(observed, 0);
        publication();
        QCOMPARE(observed, 9);
        QVERIFY(!env.scheduler->phase(admitted.id));
        QVERIFY(env.quiescent());
    }
    void rejectionExceptionAndFifo()
    {
        ManualExecutionEnvironment env;
        int effects{0}, results{0}, failures{0};
        std::vector<int> order;
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        env.reject_next = true;
        const auto rejected = execution::Submit(*env.scheduler, [&] { ++effects; }, [&] { ++results; });
        QCOMPARE(rejected.rejection, execution::Rejection::Transport);
        QCOMPARE(env.scheduler->pending(), size_t{0});
        execution::Submit(*env.scheduler, [&]() -> int { ++effects; order.push_back(1); throw std::runtime_error("synthetic"); },
                          [&](int) { ++results; }, [&](std::exception_ptr error) {
                              QVERIFY(error);
                              ++failures;
                              order.push_back(3);
                          });
        execution::Submit(*env.scheduler, [&] { ++effects; order.push_back(2); }, [&] { ++results; order.push_back(4); });
        env.drain();
        QCOMPARE(effects, 2);
        QCOMPARE(results, 1);
        QCOMPARE(failures, 1);
        QCOMPARE(order, std::vector<int>({1, 2, 3, 4}));
        QVERIFY(env.quiescent());
        execution::Submit(*env.scheduler, [] { throw 1; }, [] {});
        env.drain();
        QVERIFY(env.quiescent());
    }
    void completionCanSubmitAnotherOwnedCommand()
    {
        ManualExecutionEnvironment env;
        int effects{0}, outcomes{0};
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        execution::Submit(*env.scheduler, [&] { ++effects; }, [&] {
            ++outcomes;
            QVERIFY(execution::Submit(*env.scheduler, [&] { ++effects; }, [&] { ++outcomes; }));
        });
        env.drain();
        QCOMPARE(effects, 2);
        QCOMPARE(outcomes, 2);
        QVERIFY(env.quiescent());
    }
    void ownedResultsAreReleasedAfterAccounting()
    {
        ManualExecutionEnvironment env;
        int released{0};
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        struct Value { int& count; ~Value() { ++count; } };
        execution::Submit(*env.scheduler, [&] { return std::make_unique<Value>(released); },
                          [](std::unique_ptr<Value>) {});
        QVERIFY(env.step());
        QCOMPARE(released, 0);
        QVERIFY(env.step());
        QCOMPARE(released, 1);
        QVERIFY(env.quiescent());
    }
    void feeDecisionGuardsAndTerminalClose()
    {
        FeeReadState state;
        using K = FeeReadState::Kind;
        using P = FeeReadState::Phase;
        QCOMPARE(state.phase(), P::Idle);
        QVERIFY(!state.apply({K::Complete, {1}}));
        QVERIFY(state.apply({K::Invalidate, {1}}));
        QVERIFY(!state.apply({K::Invalidate, {1}}));
        QVERIFY(!state.apply({K::Dispatch, {1}}));
        QVERIFY(state.apply({K::Dispatch, {2}}));
        QCOMPARE(state.phase(), P::Reading);
        QVERIFY(!state.apply({K::Complete, {1}}));
        QVERIFY(state.apply({K::Invalidate, {3}}));
        QCOMPARE(state.phase(), P::Idle);
        QVERIFY(!state.apply({K::Complete, {2}}));
        QVERIFY(state.apply({K::Dispatch, {4}}));
        QVERIFY(state.apply({K::Complete, {4}}));
        QVERIFY(!state.apply({K::Complete, {4}}));
        QVERIFY(!state.apply({static_cast<K>(255), {5}}));
        QVERIFY(state.apply({K::Close, {}}));
        QCOMPARE(state.phase(), P::Closed);
        for (auto kind : {K::Invalidate, K::Dispatch, K::Complete, K::Close}) QVERIFY(!state.apply({kind, {6}}));
    }
};

BITCOINQML_REGISTER_QT_TEST(ExecutionTests)
#include <test_execution.moc>
