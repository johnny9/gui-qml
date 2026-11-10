// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/execution.h>
#include <qml/models/feereadstate.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>

#include <QtTest/QtTest>
#include <QScopeGuard>

#include <stdexcept>

class ExecutionTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
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
