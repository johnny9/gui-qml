// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/execution.h>
#include <qml/retainedsnapshot.h>
#include <qml/snapshotquery.h>
#include <qml/models/feereadstate.h>
#include <qml/walletlifecyclestate.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/manualfeereceipts.h>
#include <test/qt_test_registry.h>

#include <QtTest/QtTest>
#include <QScopeGuard>
#include <QSemaphore>

#include <atomic>
#include <thread>

#include <deque>
#include <functional>
#include <optional>
#include <stdexcept>

class ExecutionTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void feeReceiptKeepsConcurrentNativeFactsIndependent()
    {
        auto env = std::make_shared<ManualExecutionEnvironment>();
        auto receipts = std::make_shared<ManualFeeReceipts>(env);
        int reads{0}, fee_observed{0}, facts_observed{0}, outcome_value{0};
        QSemaphore start, posted;
        std::thread native{[&] {
            start.acquire();
            receipts->post([&] { ++facts_observed; });
            posted.release();
        }};
        const auto cleanup = qScopeGuard([&] {
            start.release();
            if (native.joinable()) native.join();
            receipts->drain(); receipts->publishAll(); env->publishAll();
        });
        const auto original = execution::Submit(*env->scheduler, [&] {
            receipts->readStarted(); ++reads; return 42;
        }, [&](int value) {
            outcome_value = value;
            start.release(); posted.acquire(); // Native posts inside the actual fee outcome window.
            receipts->post([&] { ++fee_observed; });
        });
        QVERIFY(original); QCOMPARE(reads, 0);
        QCOMPARE(receipts->startRead(), std::make_optional(original.id));
        QCOMPARE(reads, 1); QCOMPARE(fee_observed, 0); QCOMPARE(facts_observed, 0);
        QVERIFY(receipts->finishOutcome(original.id));
        start.release(); // Cleanup remains possible even if a broken outcome was not invoked.
        native.join();
        QCOMPARE(outcome_value, 42);
        QCOMPARE(receipts->publicationCount(), size_t{1});
        QCOMPARE(env->publicationCount(), size_t{1});
        QVERIFY(!env->scheduler->phase(original.id));
        env->publishAll(); QCOMPARE(facts_observed, 1); QCOMPARE(fee_observed, 0);
        QVERIFY(receipts->publish(original.id)); QVERIFY(!receipts->publish(original.id));
        QCOMPARE(fee_observed, 1); QCOMPARE(facts_observed, 1);
        QVERIFY(env->quiescent());
    }
    void manualDuplicatePublicationCopiesMutableCapture()
    {
        ManualExecutionEnvironment env;
        std::vector<int> observed;
        const auto cleanup = qScopeGuard([&] { env.stop(); env.drain(); env.publishAll(); });
        env.post([state = 0, &observed]() mutable { observed.push_back(++state); });
        QVERIFY(env.duplicateFirstPublication());
        QCOMPARE(env.publicationCount(), size_t{2});
        QVERIFY(observed.empty());
        env.publishAll();
        QCOMPARE(observed, std::vector<int>({1, 1}));
        QVERIFY(env.quiescent());
    }
    void routedNativeCleanupIngressKeepsOriginalManualOutcome()
    {
        using namespace execution;
        ManualRoutedExecution env;
        int retired{0}, outcomes{0};
        const auto key = env.scheduler->allocateKey();
        const auto original = Submit(*env.scheduler, [&] { ++retired; }, [&] { ++outcomes; }, {},
            {Lane::Cleanup, 32, key, WorkClass::Action}, true);
        QVERIFY(original);
        auto borrowed = env.scheduler->borrow(key);
        QVERIFY(borrowed);
        QVERIFY(env.scheduler->activateCleanup(original.id));
        QVERIFY(env.queuedIdle());
        QSemaphore start;
        std::atomic_bool finished{false};
        std::thread native{[&, lease = std::move(borrowed.lease)]() mutable {
            start.acquire();
            lease.reset(); // Actual Native final borrower dispatches Cleanup.
            finished = true;
        }};
        const auto cleanup = qScopeGuard([&] {
            start.release();
            if (native.joinable()) native.join();
            env.drain();
        });
        start.release();
        QVERIFY(QTest::qWaitFor([&] {
            (void)env.traceSnapshot();
            (void)env.queuedCount();
            (void)env.activeIdle();
            (void)env.quiescent();
            return finished.load();
        }, 3000));
        native.join();
        QCOMPARE(retired, 0);
        QCOMPARE(outcomes, 0);
        QCOMPARE(env.queuedCountIf([&](const auto& turn) {
            (void)env.traceSnapshot(); // Inspection predicate is outside mutex.
            return turn.id == original.id && turn.lane == Lane::Cleanup;
        }), 1);
        const auto dispatched = env.dispatchNext(Lane::Cleanup);
        QCOMPARE(dispatched, std::make_optional(original.id));
        QVERIFY(!env.activeIdle());
        QCOMPARE(retired, 0);
        QVERIFY(env.finishWork(original.id));
        QCOMPARE(retired, 1);
        QCOMPARE(outcomes, 0);
        const auto outcome = env.deliverOutcome();
        QCOMPARE(outcome, std::make_optional(original.id));
        QVERIFY(env.stepMailbox(original.id));
        QCOMPARE(outcomes, 1);
        QVERIFY(env.quiescent());
    }
    void manualTransportSmallCallableDestructionMayReenter()
    {
        ManualExecutionEnvironment env;
        struct Probe {
            ManualExecutionEnvironment* environment;
            bool armed{false}, queued_phase{false}, busy_during_queued_destruction{false};
            int copies{0}, destroyed{0}, callbacks{0}, observed{0};
            void reenter() {
                (void)environment->publicationsIdle();
                if (queued_phase && !environment->transportIdle()) busy_during_queued_destruction = true;
                environment->post([this] { ++observed; });
            }
        } probe{&env};
        const auto cleanup = qScopeGuard([&] {
            probe.armed = false;
            env.stop(); env.drain(); env.publishAll();
        });
        struct SmallCallable {
            Probe* probe;
            explicit SmallCallable(Probe* value) : probe(value) {}
            SmallCallable(const SmallCallable& other) noexcept : probe(other.probe) {
                if (probe->armed) { ++probe->copies; probe->reenter(); }
            }
            void operator()() const { ++probe->callbacks; }
            ~SmallCallable() { if (probe->armed) { ++probe->destroyed; probe->reenter(); } }
        };
        env.restart(SmallCallable{&probe});
        probe.armed = true; // Client copy/destructor hooks arm after registration.
        env.restart([] {}); // Replacement retires the boxed callable outside lock.
        QVERIFY(probe.destroyed > 0);
        env.stop();
        probe.armed = false;
        env.restart(SmallCallable{&probe});
        probe.armed = true;
        const auto before_stop = probe.destroyed;
        env.stop();
        QVERIFY(probe.destroyed > before_stop);
        env.publishAll();
        QCOMPARE(probe.callbacks, 0);
        QVERIFY(env.quiescent());
        probe.armed = false;
        env.post(SmallCallable{&probe});
        probe.armed = true;
        probe.queued_phase = true;
        QVERIFY(env.publishNext()); // Extract/invoke/destroy queued small callable.
        QCOMPARE(probe.callbacks, 1);
        QVERIFY(probe.busy_during_queued_destruction);
        probe.queued_phase = false;
        env.publishAll();
        QVERIFY(probe.observed > 0);
        QVERIFY(env.quiescent());
        env.restart({});
        QVERIFY(env.transportIdle());
        env.advance(std::chrono::milliseconds{250});
        QCOMPARE(probe.callbacks, 1);
        QVERIFY(env.quiescent());
    }
    void manualTransportNativeIngressKeepsDeliveryControlled()
    {
        ManualExecutionEnvironment env;
        std::atomic<int> effects{0}, outcomes{0}, publications{0};
        std::atomic_bool finished{false}, rejected{false};
        QSemaphore start;
        std::thread native{[&] {
            start.acquire();
            for (int index = 0; index < 8; ++index) {
                const auto admitted = execution::Submit(*env.scheduler,
                    [&] { ++effects; }, [&] { ++outcomes; });
                if (!admitted) rejected = true;
                env.post([&] { ++publications; });
            }
            finished = true;
        }};
        const auto cleanup = qScopeGuard([&] {
            start.release();
            if (native.joinable()) native.join();
            env.drain(); env.publishAll();
        });
        start.release();
        QVERIFY(QTest::qWaitFor([&] {
            (void)env.trace();
            (void)env.queuedTurnCount();
            (void)env.publicationCount();
            (void)env.transportIdle();
            (void)env.quiescent();
            return finished.load();
        }, 3000));
        native.join();
        QVERIFY(!rejected);
        QCOMPARE(effects.load(), 0);
        QCOMPARE(outcomes.load(), 0);
        QCOMPARE(publications.load(), 0);
        QCOMPARE(env.publicationCount(), size_t{8});
        QVERIFY(!env.quiescent());
        env.drain();
        QCOMPARE(effects.load(), 8);
        QCOMPARE(outcomes.load(), 8);
        QCOMPARE(publications.load(), 0);
        QVERIFY(env.turnsIdle());
        env.publishAll();
        QCOMPARE(publications.load(), 8);
        QVERIFY(env.quiescent());
    }
    void manualTransportRetiredTimerCaptureMayReenter()
    {
        ManualExecutionEnvironment env;
        int destroyed{0}, published{0};
        const auto cleanup = qScopeGuard([&] { env.stop(); env.drain(); env.publishAll(); });
        auto capture = std::shared_ptr<int>{new int{0}, [&](int* value) {
            delete value;
            ++destroyed;
            (void)env.trace();
            env.post([&] { ++published; });
        }};
        env.restart([capture] {});
        capture.reset();
        env.stop(); // Last capture destructor must run outside transport mutex.
        QCOMPARE(destroyed, 1);
        QCOMPARE(published, 0);
        QCOMPARE(env.publicationCount(), size_t{1});
        env.publishAll();
        QCOMPARE(published, 1);
        QVERIFY(env.quiescent());
        int expired{0};
        env.restart([&] { ++expired; env.post([&] { ++published; }); });
        env.advance(std::chrono::milliseconds{249});
        QCOMPARE(expired, 0);
        env.advance(std::chrono::milliseconds{1});
        QCOMPARE(expired, 1);
        QCOMPARE(published, 1);
        env.publishAll();
        QCOMPARE(published, 2);
        QVERIFY(env.quiescent());
    }
    void coalescedRequestDuringRejectedAdmissionDoesNotRetryInline()
    {
        using namespace execution;
        ManualRoutedExecution env;
        struct AdmissionPort final : Scheduler {
            std::shared_ptr<Scheduler> upstream;
            std::function<void()> while_rejecting;
            bool rejecting{true};
            int attempts{0};
            Admission submit(std::shared_ptr<Task> task, TaskSpec spec) override {
                ++attempts;
                if (rejecting) { while_rejecting(); return {{}, Rejection::Capacity}; }
                return upstream->submit(std::move(task), spec);
            }
            Admission reserveCleanup(std::shared_ptr<Task> task, TaskSpec spec) override { return upstream->reserveCleanup(std::move(task), spec); }
            bool activateCleanup(TaskId id) override { return upstream->activateCleanup(id); }
            BorrowAdmission borrow(ResourceKey key) override { return upstream->borrow(key); }
            ResourceKey allocateKey() override { return upstream->allocateKey(); }
            size_t pending() const override { return upstream->pending(); }
            void closeOrdinaryAdmission() override { upstream->closeOrdinaryAdmission(); }
            bool cancelQueued(TaskId id) override { return upstream->cancelQueued(id); }
        };
        auto port = std::make_shared<AdmissionPort>(); port->upstream = env.scheduler;
        int reads{0}, notices{0};
        auto query = std::make_shared<SnapshotQuery<int>>(port, [&] { ++reads; return 42; }, [&] { ++notices; });
        port->while_rejecting = [query] { query->request(); };
        query->request();
        QCOMPARE(port->attempts, 1); QCOMPARE(reads, 0); QCOMPARE(notices, 1);
        const auto rejected = query->take(); QVERIFY(rejected); QVERIFY(rejected->error); QVERIFY(!rejected->snapshot);
        QVERIFY(env.quiescent());
        port->rejecting = false;
        query->request(); QCOMPARE(port->attempts, 2); QCOMPARE(reads, 0);
        env.drain(); QCOMPARE(reads, 1); QCOMPARE(notices, 2);
        const auto retried = query->take(); QVERIFY(retried); QVERIFY(!retried->error); QVERIFY(retried->snapshot);
        QCOMPARE(*retried->snapshot, 42);
        query->close(); port->while_rejecting = {}; query.reset(); env.drain(); QVERIFY(env.quiescent());
    }

    void controlSnapshotUsesItsBoundedBudgetWhileOrdinaryObservationIsHeld()
    {
        using namespace execution;
        auto limits = Limits::Bounded();
        limits.ordinary = {1, 1024}; limits.observations = {1, 1024}; limits.control = {1, 1024};
        ManualRoutedExecution env{limits};
        int ordinary_reads{0}, control_reads{0};
        std::deque<std::function<void()>> gui;
        auto ordinary = std::make_shared<SnapshotQuery<int>>(env.scheduler,
            [&] { ++ordinary_reads; return 7; }, [] {});
        std::optional<SnapshotQuery<int>::Result> published;
        std::shared_ptr<SnapshotQuery<int>> control;
        control = std::make_shared<SnapshotQuery<int>>(env.scheduler,
            [&] { ++control_reads; return 42; },
            [&] { gui.emplace_back([&] { published = control->take(); }); }, Lane::Control);
        ordinary->request(); control->request();
        QCOMPARE(ordinary_reads, 0); QCOMPARE(control_reads, 0); QVERIFY(!published);
        // The lifecycle query consumes the existing Control budget. It cannot
        // admit a second packet merely because ordinary capacity is full.
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}, {}, {Lane::Control, 1}).rejection, Rejection::Capacity);
        QVERIFY(env.step(Lane::Control)); QCOMPARE(control_reads, 1); QCOMPARE(ordinary_reads, 0);
        QVERIFY(!published); QVERIFY(gui.empty());
        QVERIFY(env.step(Lane::Domain)); QVERIFY(!published); QCOMPARE(gui.size(), size_t{1});
        auto publication = std::move(gui.front()); gui.pop_front(); publication();
        QVERIFY(published); QVERIFY(published->snapshot); QVERIFY(!published->error); QCOMPARE(*published->snapshot, 42);
        QCOMPARE(ordinary_reads, 0);
        ordinary->close(); control->close(); env.drain();
        QCOMPARE(ordinary_reads, 0); QCOMPARE(control_reads, 1); QVERIFY(env.quiescent());
    }

    void admittedActionCompletesDependentStagesAfterFullClose()
    {
        using namespace execution;
        ManualRoutedExecution env;
        auto scope = env.scheduler->openActionScope(); QVERIFY(scope);
        int effects{0}; std::shared_ptr<int> observed;
        const auto first = Submit(*scope, [scope, &effects, &observed] {
            auto output = RetainSnapshot<int>(scope, [] { return std::make_unique<int>(0); });
            if (!output) throw std::runtime_error{"Original result ownership rejected"};
            const auto next = Submit(*scope, [output, &effects] { ++effects; *output = 42; return output; },
                [&observed](auto result) { observed = std::move(result); });
            if (!next) throw std::runtime_error{"Original Native stage rejected"};
        }, [] {}, {}, {Lane::Domain, 64});
        QVERIFY(first); env.scheduler->closeAdmission();
        QVERIFY(!env.scheduler->openActionScope());
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}).rejection, Rejection::Closed);
        QVERIFY(env.step(Lane::Domain)); QCOMPARE(effects, 0);
        QVERIFY(env.step(Lane::Core)); QCOMPARE(effects, 1); QVERIFY(!observed);
        env.drain(); QVERIFY(observed); QCOMPARE(*observed, 42);
        scope.reset(); env.drain(); QVERIFY(!env.quiescent()); // Original value owns its reserved cleanup.
        observed.reset(); env.drain(); QVERIFY(env.quiescent());
    }

    void scopedCleanupCannotRunBeforeItsOriginalBorrow()
    {
        using namespace execution;
        ManualRoutedExecution env;
        auto scope = env.scheduler->openActionScope(); QVERIFY(scope);
        env.scheduler->closeAdmission();
        const auto key = scope->allocateKey(); int retired{0};
        const auto cleanup = Submit(*scope, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, 8, key}, true);
        QVERIFY(cleanup); QVERIFY(!env.step(Lane::Cleanup));
        auto held = scope->borrow(key); QVERIFY(held);
        QVERIFY(scope->activateCleanup(cleanup.id));
        scope.reset(); QVERIFY(!env.step(Lane::Cleanup));
        held.lease.reset(); QVERIFY(env.step(Lane::Cleanup)); QCOMPARE(retired, 1);
        env.drain(); QVERIFY(env.quiescent());
    }

    void actionScopesKeepSharedBudgetsAndOriginalResourceIdentity()
    {
        using namespace execution;
        auto limits = Limits::Bounded(); limits.ordinary = {1, 8}; limits.action_scopes = {2, 32};
        ManualRoutedExecution env{limits};
        auto first = env.scheduler->openActionScope(16), second = env.scheduler->openActionScope(16);
        QVERIFY(first && second); QVERIFY(!env.scheduler->openActionScope(0));
        QVERIFY(!first->openActionScope());
        int effects{0}; const auto original = Submit(*first, [&] { ++effects; }, [] {}, {}, {Lane::Core, 8}); QVERIFY(original);
        QVERIFY(!second->cancelQueued(original.id));
        QCOMPARE(Submit(*second, [] {}, [] {}).rejection, Rejection::Capacity);
        QCOMPARE(Submit(*first, [] {}, [] {}, {}, {Lane::Control}).rejection, Rejection::Invalid);
        const auto key = first->allocateKey();
        const auto cleanup = Submit(*first, [] {}, [] {}, {}, {Lane::Cleanup, 8, key}, true); QVERIFY(cleanup);
        QCOMPARE(second->borrow(key).rejection, Rejection::Invalid);
        QVERIFY(!second->activateCleanup(cleanup.id));
        auto held = first->borrow(key); QVERIFY(held); QVERIFY(first->activateCleanup(cleanup.id));
        env.scheduler->closeOrdinaryAdmission();
        env.drain(); QCOMPARE(effects, 1);
        QVERIFY(Submit(*second, [&] { ++effects; }, [] {}, {}, {Lane::Core, 8}));
        first->closeOrdinaryAdmission(); QCOMPARE(Submit(*first, [] {}, [] {}).rejection, Rejection::Closed);
        held.lease.reset(); first.reset(); second.reset(); env.drain();
        QCOMPARE(effects, 2); QVERIFY(env.quiescent());
    }

    void releasingScopeRetiresUnrequestedCleanupInBoundedBatches()
    {
        using namespace execution;
        ManualRoutedExecution env;
        auto scope = env.scheduler->openActionScope(); QVERIFY(scope);
        int retired{0};
        for (int index = 0; index < 257; ++index) {
            QVERIFY(Submit(*scope, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, 8, scope->allocateKey()}, true));
        }
        env.scheduler->closeAdmission(); QVERIFY(env.queuedIdle());
        scope.reset(); QCOMPARE(env.queuedCount(), size_t{16}); QCOMPARE(retired, 0);
        env.drain(); QCOMPARE(retired, 257); QVERIFY(env.quiescent());
    }

    void implicitScopedRetirementRejectsLaterWorkForItsResource()
    {
        using namespace execution;
        ManualRoutedExecution env; auto scope = env.scheduler->openActionScope(); QVERIFY(scope);
        const auto key = scope->allocateKey(); int retired{0}, touched_after_retirement{0};
        QVERIFY(Submit(*scope, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, 8, key}, true));
        scope.reset(); QCOMPARE(env.queuedCount(), size_t{1});
        QCOMPARE(Submit(*env.scheduler, [&] { ++touched_after_retirement; }, [] {}, {}, {Lane::Core, 8, key}).rejection, Rejection::Closed);
        env.drain(); QCOMPARE(retired, 1); QCOMPARE(touched_after_retirement, 0); QVERIFY(env.quiescent());
    }
    void scopedCleanupIndexesRemainConsistentUnderRunnableBackpressure()
    {
        using namespace execution;
        auto limits = Limits::Bounded(); limits.cleanup.tasks = 1;
        ManualRoutedExecution env{limits}; int retired{0};
        QVERIFY(Submit(*env.scheduler, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, 8}));
        auto scope = env.scheduler->openActionScope(); QVERIFY(scope);
        for (int i = 0; i < 33; ++i) {
            const auto reservation = Submit(*scope, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, 8, scope->allocateKey()}, true); QVERIFY(reservation);
            QVERIFY(scope->activateCleanup(reservation.id)); // Explicit promotion must remain consistent while runnable cleanup is saturated.
        }
        env.scheduler->closeAdmission(); scope.reset(); env.drain();
        QCOMPARE(retired, 34); QVERIFY(env.quiescent());
    }

    void idleReservationsDoNotConsumeRunnableCleanupCapacity()
    {
        using namespace execution;
        auto limits = Limits::Bounded(); limits.cleanup.tasks = 2;
        ManualRoutedExecution env{limits};
        std::vector<ResourceLease> held;
        int retired{0}, ordinary{0};
        for (int index = 0; index < 48; ++index) {
            const auto key = env.scheduler->allocateKey();
            const auto reservation = Submit(*env.scheduler, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, 16, key}, true);
            QVERIFY(reservation);
            auto lease = env.scheduler->borrow(key); QVERIFY(lease);
            held.push_back(std::move(*lease.lease));
            QVERIFY(env.scheduler->activateCleanup(reservation.id));
            QCOMPARE(env.scheduler->phase(reservation.id), std::make_optional(Phase::Reserved));
        }
        QVERIFY(Submit(*env.scheduler, [&] { ++ordinary; }, [] {}, {}, {Lane::Cleanup, 16}));
        QVERIFY(Submit(*env.scheduler, [&] { ++ordinary; }, [] {}, {}, {Lane::Cleanup, 16}));
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}, {}, {Lane::Cleanup, 16}).rejection, Rejection::Capacity);
        env.drain(); QCOMPARE(ordinary, 2); QCOMPARE(retired, 0);
        env.scheduler->closeAdmission();
        held.clear();
        QCOMPARE(retired, 0);
        while (!env.quiescent()) {
            const auto queued = env.queuedCountIf([](const auto& turn) { return turn.lane == Lane::Cleanup; });
            QVERIFY(queued <= 2);
            QVERIFY(env.step(Lane::Cleanup) || env.step(Lane::Domain));
        }
        QCOMPARE(retired, 48);
    }
    void reservationInventoryHasItsOwnBound()
    {
        using namespace execution;
        auto limits = Limits::Bounded(); limits.cleanup.tasks = 1;
        limits.cleanup_reservations = {3, 24};
        ManualRoutedExecution env{limits};
        int retired{0};
        for (int index = 0; index < 3; ++index) {
            const auto key = env.scheduler->allocateKey();
            QVERIFY(Submit(*env.scheduler, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, 8, key}, true));
        }
        const auto rejected = Submit(*env.scheduler, [] {}, [] {}, {}, {Lane::Cleanup, 8, env.scheduler->allocateKey()}, true);
        QCOMPARE(rejected.rejection, Rejection::Capacity);
        QCOMPARE(env.scheduler->pending(), size_t{3});
        env.scheduler->closeAdmission();
        QCOMPARE(env.queuedCount(), size_t{1});
        env.drain(); QCOMPARE(retired, 3); QVERIFY(env.quiescent());
    }
    void closingLargeIdleInventoryDispatchesBoundedTurns()
    {
        using namespace execution;
        ManualRoutedExecution env{Limits{}};
        int retired{0};
        for (int index = 0; index < 257; ++index) {
            QVERIFY(Submit(*env.scheduler, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, 1, env.scheduler->allocateKey()}, true));
        }
        QVERIFY(env.queuedIdle());
        env.scheduler->closeAdmission();
        QCOMPARE(env.queuedCount(), size_t{16});
        QCOMPARE(retired, 0);
        env.drain(); QCOMPARE(retired, 257); QVERIFY(env.quiescent());
    }

    void invalidRoutedConfigurationIsRejected_data()
    {
        QTest::addColumn<int>("invalid");
        QTest::newRow("missing-transport") << 0;
        QTest::newRow("zero-core-slots") << 1;
        QTest::newRow("zero-action-burst") << 2;
    }
    void invalidRoutedConfigurationIsRejected()
    {
        QFETCH(int, invalid);
        using namespace execution;
        TaskQueue::RoutedDispatch dispatch = [](Lane, Phase, TaskId, auto) { return true; };
        auto limits = Limits::Bounded();
        if (invalid == 0) dispatch = {};
        if (invalid == 2) limits.actions_before_observation = 0;
        bool rejected{false};
        try { TaskQueue queue{dispatch, limits, invalid == 1 ? size_t{0} : size_t{1}}; }
        catch (const std::invalid_argument&) { rejected = true; }
        QVERIFY(rejected);
    }
    void observationByteQuotaLeavesActionsAvailable()
    {
        using namespace execution;
        auto limits = Limits::Bounded(); limits.observations = {3, 2};
        ManualRoutedExecution env{limits};
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        int observations{0}, actions{0};
        QVERIFY(Submit(*env.scheduler, [&] { ++observations; }, [] {}, {}, {Lane::Core, 2, {1, 1}, WorkClass::Observation}));
        QCOMPARE(Submit(*env.scheduler, [&] { ++observations; }, [] {}, {}, {Lane::Core, 1, {2, 1}, WorkClass::Observation}).rejection, Rejection::Capacity);
        for (int i{0}; i < 6; ++i) QVERIFY(Submit(*env.scheduler, [&] { ++actions; }, [] {}, {}, {Lane::Core, 0, {3, 1}}));
        env.drain();
        QCOMPARE(observations, 1); QCOMPARE(actions, 6);
        QVERIFY(env.quiescent());
    }
    void cancellingWaitingKeysAndControlOwnsEveryOutcome()
    {
        using namespace execution;
        ManualRoutedExecution env;
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        int effects{0}, cancelled{0};
        const auto failure = [&](std::exception_ptr error) {
            try { std::rethrow_exception(error); }
            catch (const NotStarted& stopped) { if (stopped.cause == NotStarted::Cause::Cancelled) ++cancelled; }
        };
        const auto first = Submit(*env.scheduler, [&] { ++effects; }, [] {}, {}, {Lane::Core, 0, {1, 1}});
        QVERIFY(first);
        const auto sibling = Submit(*env.scheduler, [&] { ++effects; }, [] {}, failure, {Lane::Core, 0, {1, 1}});
        const auto waiting = Submit(*env.scheduler, [&] { ++effects; }, [] {}, failure, {Lane::Core, 0, {2, 1}});
        const auto control = Submit(*env.scheduler, [&] { ++effects; }, [] {}, failure, {Lane::Control});
        QVERIFY(sibling && waiting && control);
        QVERIFY(env.scheduler->cancelQueued(sibling.id));
        QVERIFY(env.scheduler->cancelQueued(waiting.id));
        QVERIFY(env.scheduler->cancelQueued(control.id));
        QVERIFY(!env.scheduler->cancelQueued(control.id));
        env.drain();
        QCOMPARE(effects, 1); QCOMPARE(cancelled, 3);
        QVERIFY(env.quiescent());
        QVERIFY(Submit(*env.scheduler, [&] { ++effects; }, [] {}, {}, {Lane::Core, 0, {2, 1}}));
        env.drain(); QCOMPARE(effects, 2);
    }
    void compatibilityCancellationAndReservedCleanupRemainOwned()
    {
        using namespace execution;
        std::deque<std::function<void()>> turns;
        auto queue = std::make_shared<TaskQueue>([&](Phase, TaskId, std::function<void()> turn) {
            turns.push_back(std::move(turn)); return true;
        });
        const auto drain = [&] { while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); turn(); } };
        const auto cleanup = qScopeGuard([&] { queue->closeAdmission(); drain(); });
        int effects{0}, failures{0}, retired{0};
        const auto work = Submit(*queue, [&] { ++effects; }, [] {}, [&](std::exception_ptr error) {
            try { std::rethrow_exception(error); }
            catch (const NotStarted& stopped) {
                QCOMPARE(stopped.cause, NotStarted::Cause::Cancelled);
                QCOMPARE(QString::fromLatin1(stopped.what()), QStringLiteral("Task did not start"));
                ++failures;
            }
        });
        QVERIFY(work); QVERIFY(queue->cancelQueued(work.id));
        QVERIFY(!queue->cancelQueued(work.id));
        drain(); QCOMPARE(effects, 0); QCOMPARE(failures, 1);
        const auto key = queue->allocateKey();
        const auto reserved = Submit(*queue, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, 0, key}, true);
        QVERIFY(reserved); QVERIFY(queue->activateCleanup(reserved.id));
        drain(); QCOMPARE(retired, 1); QCOMPARE(queue->pending(), size_t{0});
    }
    void cancellingOneWaitingTaskPreservesItsSameKeySuccessor()
    {
        using namespace execution;
        ManualRoutedExecution env;
        const auto drain = qScopeGuard([&] { env.drain(); });
        std::vector<int> effects, outcomes;
        const auto first = Submit(*env.scheduler, [&] { effects.push_back(1); return 1; }, [&](int n) { outcomes.push_back(n); }, {}, {Lane::Core, 0, {1, 1}});
        const auto second = Submit(*env.scheduler, [&] { effects.push_back(2); }, [] {}, [&](auto) { outcomes.push_back(-2); }, {Lane::Core, 0, {1, 1}});
        const auto third = Submit(*env.scheduler, [&] { effects.push_back(3); return 3; }, [&](int n) { outcomes.push_back(n); }, {}, {Lane::Core, 0, {1, 1}});
        QVERIFY(first && second && third); QVERIFY(env.scheduler->cancelQueued(second.id));
        env.drain();
        QCOMPARE(effects, std::vector<int>({1, 3}));
        QCOMPARE(outcomes.size(), size_t{3});
        QCOMPARE(std::count(outcomes.begin(), outcomes.end(), -2), 1);
        QCOMPARE(std::count(outcomes.begin(), outcomes.end(), 1), 1);
        QCOMPARE(std::count(outcomes.begin(), outcomes.end(), 3), 1);
        QVERIFY(env.quiescent());
    }
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
        QVERIFY(env.queuedIdle());
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
        QVERIFY(!env.scheduler->activateCleanup(reserved.id));
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
        env.rejectLane(Lane::Core);
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
        env.rejectLane(Lane::Core);
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
        env.rejectLane(Lane::Core);
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}).rejection, Rejection::Transport);
        QVERIFY(env.quiescent());
        bool invalid_transport{false};
        try { (void)std::make_shared<TaskQueue>(TaskQueue::RoutedDispatch{}, Limits::Bounded(), 1); }
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
        env.rejectNext(true);
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
