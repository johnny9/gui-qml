// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <net_processing.h>
#include <qml/initexecutor.h>
#include <qml/shutdowncoordinator.h>
#include <test/mocks/mocknode.h>
#include <util/translation.h>

#include <QtTest/QtTest>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTimer>

#include <atomic>
#include <stdexcept>

Q_DECLARE_METATYPE(interfaces::BlockAndHeaderTipInfo)

namespace {
constexpr auto SIGNAL_TIMEOUT{5'000};
}

class ShutdownParticipant : public QObject
{
    Q_OBJECT
public:
    bool started{false};
    void finish() { Q_EMIT drained(); }
Q_SIGNALS:
    void drained();
};

class QmlInitExecutorApiTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void initializeEmitsResultAndRunsOffMainThread();
    void initializeEmitsRunawayExceptionOnFailure();
    void shutdownEmitsResultAndRunsOffMainThread();
    void shutdownEmitsRunawayExceptionOnFailure();
    void interruptionBypassesBlockedInitialization();
    void shutdownWaitsForInterruptionAndEveryParticipant();
    void portMappingDrainsBeforeInterruption();
};

void QmlInitExecutorApiTests::initTestCase()
{
    qRegisterMetaType<interfaces::BlockAndHeaderTipInfo>("interfaces::BlockAndHeaderTipInfo");
}

void QmlInitExecutorApiTests::initializeEmitsResultAndRunsOffMainThread()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    std::atomic_bool ran_off_main_thread{false};

    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo* tip_info) {
        ran_off_main_thread = QThread::currentThread() != QCoreApplication::instance()->thread();
        tip_info->block_height = 101;
        tip_info->block_time = 1'700'000'001;
        tip_info->header_height = 105;
        tip_info->header_time = 1'700'000'099;
        tip_info->verification_progress = 0.75;
        return true;
    };

    QmlInitExecutor executor{node};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);

    executor.initialize();

    QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(initialize_spy.count(), 1);
    QCOMPARE(runaway_spy.count(), 0);
    QVERIFY(ran_off_main_thread.load());
    QCOMPARE(node.calls.appInitMain.load(), 1);

    const QList<QVariant> arguments = initialize_spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);

    const auto tip_info = arguments.at(1).value<interfaces::BlockAndHeaderTipInfo>();
    QCOMPARE(tip_info.block_height, 101);
    QCOMPARE(tip_info.block_time, 1'700'000'001LL);
    QCOMPARE(tip_info.header_height, 105);
    QCOMPARE(tip_info.header_time, 1'700'000'099LL);
    QCOMPARE(tip_info.verification_progress, 0.75);
}

void QmlInitExecutorApiTests::initializeEmitsRunawayExceptionOnFailure()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();

    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) -> bool {
        throw std::runtime_error{"init failed"};
    };

    QmlInitExecutor executor{node};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);

    executor.initialize();

    QVERIFY(!runaway_spy.isEmpty() || runaway_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(runaway_spy.count(), 1);
    QCOMPARE(initialize_spy.count(), 0);
    QCOMPARE(runaway_spy.takeFirst().at(0).toString(), QString{"init failed"});
    QCOMPARE(node.calls.appInitMain.load(), 1);
}

void QmlInitExecutorApiTests::shutdownEmitsResultAndRunsOffMainThread()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    std::atomic_bool ran_off_main_thread{false};

    node.app_shutdown_fn = [&] {
        ran_off_main_thread = QThread::currentThread() != QCoreApplication::instance()->thread();
    };

    QmlInitExecutor executor{node};
    QSignalSpy shutdown_spy(&executor, &QmlInitExecutor::shutdownResult);
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);

    executor.shutdown();

    QVERIFY(!shutdown_spy.isEmpty() || shutdown_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(shutdown_spy.count(), 1);
    QCOMPARE(runaway_spy.count(), 0);
    QVERIFY(ran_off_main_thread.load());
    QCOMPARE(node.calls.appShutdown.load(), 1);
}

void QmlInitExecutorApiTests::shutdownEmitsRunawayExceptionOnFailure()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();

    node.app_shutdown_fn = [] {
        throw std::runtime_error{"shutdown failed"};
    };

    QmlInitExecutor executor{node};
    QSignalSpy shutdown_spy(&executor, &QmlInitExecutor::shutdownResult);
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);

    executor.shutdown();

    QVERIFY(!runaway_spy.isEmpty() || runaway_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(runaway_spy.count(), 1);
    QCOMPARE(shutdown_spy.count(), 0);
    QCOMPARE(runaway_spy.takeFirst().at(0).toString(), QString{"shutdown failed"});
    QCOMPARE(node.calls.appShutdown.load(), 1);
}

void QmlInitExecutorApiTests::interruptionBypassesBlockedInitialization()
{
    StrictMockNode node;
    QSemaphore release;
    const auto unblock = qScopeGuard([&] { release.release(); });
    std::atomic_bool entered{false};
    std::atomic_bool off_gui{false};
    const auto gui_thread = QThread::currentThread();
    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo*) {
        entered = true;
        release.acquire();
        return false;
    };
    node.start_shutdown_fn = [&] {
        off_gui = QThread::currentThread() != gui_thread;
        release.release();
    };
    node.app_shutdown_fn = [] {};
    QmlInitExecutor executor{node};
    QSignalSpy initialized{&executor, &QmlInitExecutor::initializeResult};
    QSignalSpy interrupted{&executor, &QmlInitExecutor::interruptResult};
    QSignalSpy finished{&executor, &QmlInitExecutor::shutdownResult};
    executor.initialize();
    QTRY_VERIFY_WITH_TIMEOUT(entered.load(), SIGNAL_TIMEOUT);
    executor.interrupt();
    executor.interrupt();
    QTRY_COMPARE_WITH_TIMEOUT(interrupted.count(), 1, SIGNAL_TIMEOUT);
    QTRY_COMPARE_WITH_TIMEOUT(initialized.count(), 1, SIGNAL_TIMEOUT);
    QVERIFY(off_gui.load());
    QCOMPARE(node.calls.startShutdown.load(), 1);
    executor.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, SIGNAL_TIMEOUT);
}

void QmlInitExecutorApiTests::shutdownWaitsForInterruptionAndEveryParticipant()
{
    StrictMockNode node;
    QSemaphore release_hook;
    const auto unblock = qScopeGuard([&] { release_hook.release(); });
    std::atomic_bool hook_entered{false};
    node.start_shutdown_fn = [&] {
        hook_entered = true;
        release_hook.acquire();
    };
    node.app_shutdown_fn = [] {};
    QmlInitExecutor executor{node};
    QmlShutdownCoordinator coordinator{executor};
    ShutdownParticipant first, second;
    coordinator.addParticipant(&first, &ShutdownParticipant::drained, [&] { first.started = true; });
    coordinator.addParticipant(&second, &ShutdownParticipant::drained, [&] { second.started = true; });
    QSignalSpy finished{&executor, &QmlInitExecutor::shutdownResult};
    coordinator.requestShutdown();
    coordinator.requestShutdown();
    QTRY_VERIFY_WITH_TIMEOUT(hook_entered.load(), SIGNAL_TIMEOUT);
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
    QTRY_VERIFY_WITH_TIMEOUT(gui_progress, SIGNAL_TIMEOUT);
    QVERIFY(!first.started);
    QCOMPARE(node.calls.appShutdown.load(), 0);
    release_hook.release();
    QTRY_VERIFY_WITH_TIMEOUT(first.started && second.started, SIGNAL_TIMEOUT);
    first.finish();
    first.finish();
    QCOMPARE(node.calls.appShutdown.load(), 0);
    second.finish();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, SIGNAL_TIMEOUT);
    QCOMPARE(node.calls.startShutdown.load(), 1);
    QCOMPARE(node.calls.appShutdown.load(), 1);
}

void QmlInitExecutorApiTests::portMappingDrainsBeforeInterruption()
{
    StrictMockNode node;
    node.start_shutdown_fn = [] {};
    node.app_shutdown_fn = [] {};
    QmlInitExecutor executor{node};
    QmlShutdownCoordinator coordinator{executor};
    ShutdownParticipant settings;
    coordinator.addBeforeInterruptParticipant(&settings, &ShutdownParticipant::drained, [&] { settings.started = true; });
    QSignalSpy finished{&executor, &QmlInitExecutor::shutdownResult};
    coordinator.requestShutdown();
    QVERIFY(settings.started);
    // The final queued mapping change must precede InterruptMapPort. No second
    // startShutdown call may be used to repair that order (it reruns hooks).
    QCOMPARE(node.calls.startShutdown.load(), 0);
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
    QTRY_VERIFY_WITH_TIMEOUT(gui_progress, SIGNAL_TIMEOUT);
    settings.finish();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, SIGNAL_TIMEOUT);
    QCOMPARE(node.calls.startShutdown.load(), 1);
    QCOMPARE(node.calls.appShutdown.load(), 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(QmlInitExecutorApiTests)
#else
QTEST_MAIN(QmlInitExecutorApiTests)
#endif
#include <test_qmlinitexecutor_api.moc>
