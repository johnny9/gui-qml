// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/handler.h>
#include <net_processing.h>
#include <qml/initexecutor.h>
#include <test/mocks/mocknode.h>
#include <util/translation.h>

#include <QtTest/QtTest>

#include <atomic>
#include <stdexcept>

Q_DECLARE_METATYPE(interfaces::BlockAndHeaderTipInfo)

namespace {
constexpr auto SIGNAL_TIMEOUT{5'000};
}

class QmlInitExecutorApiTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void initializeEmitsResultAndRunsOffMainThread();
    void initializeRegistersBeforeResultOnWorker();
    void unsuccessfulInitializationDoesNotRegister();
    void cancelledInitializationDoesNotRegister();
    void initializeEmitsRunawayExceptionOnFailure();
    void registrationFailureCleansUpOnWorker();
    void shutdownEmitsResultAndRunsOffMainThread();
    void shutdownEmitsRunawayExceptionOnFailure();
    void destructorCleansUpOnWorker();
};

void QmlInitExecutorApiTests::initTestCase()
{
    qRegisterMetaType<interfaces::BlockAndHeaderTipInfo>("interfaces::BlockAndHeaderTipInfo");
}

void QmlInitExecutorApiTests::destructorCleansUpOnWorker()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return true; };
    node.shutdown_requested_fn = [] { return false; };
    node.ExpectNoCalls(node.calls.appShutdown);
    std::atomic<QThread*> registration_thread{nullptr};
    std::atomic<QThread*> cleanup_thread{nullptr};
    std::atomic_int cleanup_count{0};
    {
        QmlInitExecutor executor{node, [&] {
            registration_thread = QThread::currentThread();
            return interfaces::MakeCleanupHandler([&] {
                cleanup_thread = QThread::currentThread();
                ++cleanup_count;
            });
        }};
        QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
        executor.initialize();
        QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
        QCOMPARE(cleanup_count.load(), 0);
    }
    QCOMPARE(cleanup_count.load(), 1);
    QVERIFY(cleanup_thread.load() != QCoreApplication::instance()->thread());
    QCOMPARE(cleanup_thread.load(), registration_thread.load());
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

void QmlInitExecutorApiTests::initializeRegistersBeforeResultOnWorker()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    std::atomic_bool initialized{false};
    std::atomic_bool registered_after_initialization{false};
    std::atomic_bool result_after_registration{false};
    std::atomic<QThread*> initialization_thread{nullptr};
    std::atomic<QThread*> registration_thread{nullptr};
    std::atomic_int registration_count{0};
    std::atomic_int cleanup_count{0};
    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo*) {
        initialization_thread = QThread::currentThread();
        initialized = true;
        return true;
    };
    node.shutdown_requested_fn = [] { return false; };

    {
        QmlInitExecutor executor{node, [&] {
            registered_after_initialization = initialized.load();
            registration_thread = QThread::currentThread();
            ++registration_count;
            return interfaces::MakeCleanupHandler([&] { ++cleanup_count; });
        }};
        // A direct observer verifies ordering at emission, before queued delivery
        // could hide a registration that happened after initializeResult.
        QObject::connect(&executor, &QmlInitExecutor::initializeResult, &executor, [&] {
            result_after_registration = registration_count.load() == 1 && cleanup_count.load() == 0;
        }, Qt::DirectConnection);
        QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
        executor.initialize();

        QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
        QVERIFY(initialize_spy.takeFirst().at(0).toBool());
        QVERIFY(registered_after_initialization.load());
        QVERIFY(result_after_registration.load());
        QCOMPARE(registration_count.load(), 1);
        QCOMPARE(node.calls.shutdownRequested.load(), 1);
        QCOMPARE(registration_thread.load(), initialization_thread.load());
        QVERIFY(registration_thread.load() != QCoreApplication::instance()->thread());
        QCOMPARE(cleanup_count.load(), 0);
    }
    QCOMPARE(cleanup_count.load(), 1);
}

void QmlInitExecutorApiTests::unsuccessfulInitializationDoesNotRegister()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return false; };
    node.ExpectNoCalls(node.calls.shutdownRequested);
    std::atomic_int registration_count{0};
    QmlInitExecutor executor{node, [&] {
        ++registration_count;
        return std::unique_ptr<interfaces::Handler>{};
    }};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);
    executor.initialize();

    QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(initialize_spy.count(), 1);
    QVERIFY(!initialize_spy.takeFirst().at(0).toBool());
    QCOMPARE(runaway_spy.count(), 0);
    QCOMPARE(registration_count.load(), 0);
}

void QmlInitExecutorApiTests::cancelledInitializationDoesNotRegister()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    std::atomic_bool shutdown_requested{false};
    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo*) {
        shutdown_requested = true;
        return true;
    };
    node.shutdown_requested_fn = [&] { return shutdown_requested.load(); };
    std::atomic_int registration_count{0};
    QmlInitExecutor executor{node, [&] {
        ++registration_count;
        return std::unique_ptr<interfaces::Handler>{};
    }};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    executor.initialize();

    QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(node.calls.shutdownRequested.load(), 1);
    QCOMPARE(registration_count.load(), 0);
}

void QmlInitExecutorApiTests::initializeEmitsRunawayExceptionOnFailure()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();

    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) -> bool {
        throw std::runtime_error{"init failed"};
    };

    node.ExpectNoCalls(node.calls.shutdownRequested);
    std::atomic_int registration_count{0};
    QmlInitExecutor executor{node, [&] {
        ++registration_count;
        return std::unique_ptr<interfaces::Handler>{};
    }};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);

    executor.initialize();

    QVERIFY(!runaway_spy.isEmpty() || runaway_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(runaway_spy.count(), 1);
    QCOMPARE(initialize_spy.count(), 0);
    QCOMPARE(runaway_spy.takeFirst().at(0).toString(), QString{"init failed"});
    QCOMPARE(node.calls.appInitMain.load(), 1);
    QCOMPARE(registration_count.load(), 0);
}

void QmlInitExecutorApiTests::registrationFailureCleansUpOnWorker()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return true; };
    node.shutdown_requested_fn = [] { return false; };
    std::atomic<QThread*> registration_thread{nullptr};
    std::atomic<QThread*> cleanup_thread{nullptr};
    std::atomic_int cleanup_count{0};
    std::atomic_bool cleanup_preceded_exception{false};

    {
        QmlInitExecutor executor{node, [&]() -> std::unique_ptr<interfaces::Handler> {
            registration_thread = QThread::currentThread();
            auto registration = interfaces::MakeCleanupHandler([&] {
                cleanup_thread = QThread::currentThread();
                ++cleanup_count;
            });
            throw std::runtime_error{"registration failed"};
        }};
        QObject::connect(&executor, &QmlInitExecutor::runawayException, &executor, [&] {
            cleanup_preceded_exception = cleanup_count.load() == 1;
        }, Qt::DirectConnection);
        QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
        QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);
        executor.initialize();

        QVERIFY(!runaway_spy.isEmpty() || runaway_spy.wait(SIGNAL_TIMEOUT));
        QCOMPARE(runaway_spy.count(), 1);
        QCOMPARE(initialize_spy.count(), 0);
        QCOMPARE(runaway_spy.takeFirst().at(0).toString(), QString{"registration failed"});
        QVERIFY(cleanup_preceded_exception.load());
        QVERIFY(cleanup_thread.load() != QCoreApplication::instance()->thread());
        QCOMPARE(cleanup_thread.load(), registration_thread.load());
    }
    QCOMPARE(cleanup_count.load(), 1);
}

void QmlInitExecutorApiTests::shutdownEmitsResultAndRunsOffMainThread()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    std::atomic_bool ran_off_main_thread{false};
    std::atomic<QThread*> registration_thread{nullptr};
    std::atomic<QThread*> cleanup_thread{nullptr};
    std::atomic_int cleanup_count{0};
    std::atomic_bool cleanup_preceded_shutdown{false};

    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return true; };
    node.shutdown_requested_fn = [] { return false; };
    node.app_shutdown_fn = [&] {
        cleanup_preceded_shutdown = cleanup_count.load() == 1 && cleanup_thread.load() == QThread::currentThread();
        ran_off_main_thread = QThread::currentThread() != QCoreApplication::instance()->thread();
    };

    {
        QmlInitExecutor executor{node, [&] {
            registration_thread = QThread::currentThread();
            return interfaces::MakeCleanupHandler([&] {
                cleanup_thread = QThread::currentThread();
                ++cleanup_count;
            });
        }};
        QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
        QSignalSpy shutdown_spy(&executor, &QmlInitExecutor::shutdownResult);
        QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);
        executor.initialize();
        QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
        QCOMPARE(cleanup_count.load(), 0);
        executor.shutdown();

        QVERIFY(!shutdown_spy.isEmpty() || shutdown_spy.wait(SIGNAL_TIMEOUT));
        QCOMPARE(shutdown_spy.count(), 1);
        QCOMPARE(runaway_spy.count(), 0);
        QVERIFY(ran_off_main_thread.load());
        QVERIFY(cleanup_preceded_shutdown.load());
        QCOMPARE(cleanup_thread.load(), registration_thread.load());
        QCOMPARE(node.calls.appShutdown.load(), 1);
    }
    QCOMPARE(cleanup_count.load(), 1);
}

void QmlInitExecutorApiTests::shutdownEmitsRunawayExceptionOnFailure()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    std::atomic_int cleanup_count{0};
    std::atomic_bool cleanup_ran_off_main_thread{false};
    std::atomic_bool cleanup_preceded_shutdown{false};

    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return true; };
    node.shutdown_requested_fn = [] { return false; };
    node.app_shutdown_fn = [&] {
        cleanup_preceded_shutdown = cleanup_count.load() == 1;
        throw std::runtime_error{"shutdown failed"};
    };

    {
        QmlInitExecutor executor{node, [&] {
            return interfaces::MakeCleanupHandler([&] {
                cleanup_ran_off_main_thread = QThread::currentThread() != QCoreApplication::instance()->thread();
                ++cleanup_count;
            });
        }};
        QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
        QSignalSpy shutdown_spy(&executor, &QmlInitExecutor::shutdownResult);
        QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);
        executor.initialize();
        QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
        executor.shutdown();

        QVERIFY(!runaway_spy.isEmpty() || runaway_spy.wait(SIGNAL_TIMEOUT));
        QCOMPARE(runaway_spy.count(), 1);
        QCOMPARE(shutdown_spy.count(), 0);
        QCOMPARE(runaway_spy.takeFirst().at(0).toString(), QString{"shutdown failed"});
        QCOMPARE(node.calls.appShutdown.load(), 1);
        QVERIFY(cleanup_preceded_shutdown.load());
        QVERIFY(cleanup_ran_off_main_thread.load());
    }
    QCOMPARE(cleanup_count.load(), 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(QmlInitExecutorApiTests)
#else
QTEST_MAIN(QmlInitExecutorApiTests)
#endif
#include <test_qmlinitexecutor_api.moc>
