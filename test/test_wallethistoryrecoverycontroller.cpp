// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/wallethistoryrecoverycontroller.h>

#include <QElapsedTimer>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QtTest/QtTest>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
constexpr int TIMEOUT{5'000};
using wallet_history::Control;
using wallet_history::Outcome;
using wallet_history::Phase;
using wallet_history::Result;

struct Gate {
    QSemaphore entered;
    QSemaphore release;
    QSemaphore stop_watchdog;
    std::atomic_bool cancellation_seen{false};
    std::atomic_int calls{0};
    std::thread watchdog{[this] {
        if (!stop_watchdog.tryAcquire(1, TIMEOUT)) release.release();
    }};

    ~Gate()
    {
        stop_watchdog.release();
        watchdog.join();
    }
};
}

class WalletHistoryRecoveryControllerTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void cleanup();
    void unavailableBackendDoesNotStart();
    void terminalOutcomes_data();
    void terminalOutcomes();
    void waitingRetryKeepsGuiResponsive();
    void cancellationWinsOverLateSuccessAndCanRetry();
    void shutdownDrainsWithoutPublishingLateResult();
    void shutdownFromStartNotificationDoesNotPublishFailure();
    void destructionKeepsCompletionValidUntilWorkerExits();
    void failedWorkCanRetry();
    void unlockFailureKeepsPromptAndRetryClearsIt();
};

void WalletHistoryRecoveryControllerTests::cleanup()
{
    bool drained{false};
    BackendExecutor::shutdownAll(this, [&] { drained = true; });
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
}

void WalletHistoryRecoveryControllerTests::unavailableBackendDoesNotStart()
{
    WalletHistoryRecoveryController controller{WalletHistoryRecoveryController::Work{}};
    QSignalSpy finished{&controller, &WalletHistoryRecoveryController::finished};
    controller.start();
    QVERIFY(!controller.enabled());
    QVERIFY(!controller.active());
    QVERIFY(!controller.incomplete());
    QVERIFY(!controller.visible());
    QVERIFY(controller.isDrained());
    QCOMPARE(controller.state(), QStringLiteral("disabled"));
    QCOMPARE(finished.count(), 0);
    controller.shutdown();
    controller.start();
    QCOMPARE(finished.count(), 0);
}

void WalletHistoryRecoveryControllerTests::terminalOutcomes_data()
{
    QTest::addColumn<int>("outcome");
    QTest::addColumn<QString>("state");
    QTest::addColumn<bool>("enabled");
    QTest::addColumn<bool>("incomplete");
    QTest::addColumn<bool>("visible");
    QTest::newRow("complete") << int(Outcome::Complete) << QStringLiteral("complete") << true << false << true;
    QTest::newRow("disabled") << int(Outcome::Disabled) << QStringLiteral("disabled") << false << false << false;
    QTest::newRow("failed") << int(Outcome::Failed) << QStringLiteral("error") << true << true << true;
}

void WalletHistoryRecoveryControllerTests::terminalOutcomes()
{
    QFETCH(int, outcome);
    QFETCH(QString, state);
    QFETCH(bool, enabled);
    QFETCH(bool, incomplete);
    QFETCH(bool, visible);
    WalletHistoryRecoveryController controller{[outcome](const std::shared_ptr<Control>&, std::optional<SecureString>) {
        return Result{Outcome(outcome), outcome == int(Outcome::Failed) ? "Block unavailable" : ""};
    }};
    QSignalSpy finished{&controller, &WalletHistoryRecoveryController::finished};
    controller.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, TIMEOUT);
    QVERIFY(!controller.active());
    QCOMPARE(controller.state(), state);
    QCOMPARE(controller.enabled(), enabled);
    QCOMPARE(controller.incomplete(), incomplete);
    QCOMPARE(controller.visible(), visible);
    QCOMPARE(controller.errorText(), outcome == int(Outcome::Failed) ? QStringLiteral("Block unavailable") : QString{});
    if (outcome == int(Outcome::Complete)) {
        QCOMPARE(controller.progress(), 1.0);
        QVERIFY(!controller.indeterminate());
    }
    controller.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(controller.isDrained(), TIMEOUT);
}

void WalletHistoryRecoveryControllerTests::waitingRetryKeepsGuiResponsive()
{
    const auto gui_thread = QThread::currentThread();
    auto calls = std::make_shared<std::atomic_int>(0);
    auto off_gui = std::make_shared<std::atomic_bool>(true);
    WalletHistoryRecoveryController controller{[calls, off_gui, gui_thread](const std::shared_ptr<Control>& control, std::optional<SecureString>) {
        if (QThread::currentThread() == gui_thread) *off_gui = false;
        if (++*calls == 1) {
            control->update({Phase::WaitingForFilters});
            return Result{Outcome::Retry, {}};
        }
        return Result{Outcome::Complete, {}};
    }};
    QSignalSpy finished{&controller, &WalletHistoryRecoveryController::finished};
    controller.start();
    controller.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, TIMEOUT);
    QCOMPARE(calls->load(), 1);
    QCOMPARE(controller.state(), QStringLiteral("waiting-filters"));
    QVERIFY(!controller.active());
    QVERIFY(controller.enabled());
    QVERIFY(controller.incomplete());
    QVERIFY(controller.indeterminate());
    int heartbeats{0};
    QTimer heartbeat;
    connect(&heartbeat, &QTimer::timeout, this, [&] { ++heartbeats; });
    heartbeat.start(0);
    QTRY_VERIFY_WITH_TIMEOUT(heartbeats > 2, TIMEOUT);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, TIMEOUT);
    QCOMPARE(calls->load(), 2);
    QVERIFY(off_gui->load());
    QCOMPARE(controller.state(), QStringLiteral("complete"));
    QVERIFY(!controller.incomplete());
    controller.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(controller.isDrained(), TIMEOUT);
}

void WalletHistoryRecoveryControllerTests::cancellationWinsOverLateSuccessAndCanRetry()
{
    auto gate = std::make_shared<Gate>();
    const auto release = qScopeGuard([gate] { gate->release.release(); });
    WalletHistoryRecoveryController controller{[gate](const std::shared_ptr<Control>& control, std::optional<SecureString>) {
        if (++gate->calls == 1) {
            control->update({Phase::Scanning, 5, 10});
            gate->entered.release();
            gate->release.acquire();
            gate->cancellation_seen = control->cancelled.load();
        }
        return Result{Outcome::Complete, {}};
    }};
    QSignalSpy finished{&controller, &WalletHistoryRecoveryController::finished};
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, TIMEOUT);
    QTRY_COMPARE_WITH_TIMEOUT(controller.progress(), 0.5, TIMEOUT);
    QElapsedTimer cancellation;
    cancellation.start();
    controller.cancel();
    QVERIFY(cancellation.elapsed() < TIMEOUT / 2);
    QCOMPARE(controller.state(), QStringLiteral("cancelled"));
    QVERIFY(controller.active());
    QVERIFY(controller.incomplete());
    controller.retry();
    QCOMPARE(gate->calls.load(), 1);
    bool responsive{false};
    QTimer::singleShot(0, &controller, [&] { responsive = true; });
    QTRY_VERIFY_WITH_TIMEOUT(responsive, TIMEOUT);
    gate->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, TIMEOUT);
    QVERIFY(gate->cancellation_seen.load());
    QVERIFY(!controller.active());
    QVERIFY(controller.incomplete());
    QCOMPARE(controller.state(), QStringLiteral("cancelled"));
    controller.retry();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, TIMEOUT);
    QCOMPARE(gate->calls.load(), 2);
    QCOMPARE(controller.state(), QStringLiteral("complete"));
    QVERIFY(!controller.incomplete());
    controller.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(controller.isDrained(), TIMEOUT);
}

void WalletHistoryRecoveryControllerTests::shutdownDrainsWithoutPublishingLateResult()
{
    auto gate = std::make_shared<Gate>();
    const auto release = qScopeGuard([gate] { gate->release.release(); });
    WalletHistoryRecoveryController controller{[gate](const std::shared_ptr<Control>& control, std::optional<SecureString>) {
        ++gate->calls;
        control->update({Phase::Downloading, 1, 3});
        gate->entered.release();
        gate->release.acquire();
        gate->cancellation_seen = control->cancelled.load();
        return Result{Outcome::Complete, {}};
    }};
    QSignalSpy finished{&controller, &WalletHistoryRecoveryController::finished};
    QSignalSpy drained{&controller, &WalletHistoryRecoveryController::drained};
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, TIMEOUT);
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(), QStringLiteral("downloading"), TIMEOUT);
    QElapsedTimer stopping;
    stopping.start();
    controller.shutdown();
    QVERIFY(stopping.elapsed() < TIMEOUT / 2);
    controller.shutdown();
    bool responsive{false};
    QTimer::singleShot(0, &controller, [&] { responsive = true; });
    QTRY_VERIFY_WITH_TIMEOUT(responsive, TIMEOUT);
    QVERIFY(!controller.isDrained());
    QCOMPARE(drained.count(), 0);
    QCOMPARE(finished.count(), 0);
    gate->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, TIMEOUT);
    QVERIFY(controller.isDrained());
    QVERIFY(gate->cancellation_seen.load());
    QVERIFY(!controller.active());
    QCOMPARE(finished.count(), 0);
    QVERIFY(controller.state() != QStringLiteral("complete"));
    controller.start();
    controller.retry();
    controller.unlockAndRetry(QStringLiteral("unused"));
    QCOMPARE(gate->calls.load(), 1);
}

void WalletHistoryRecoveryControllerTests::shutdownFromStartNotificationDoesNotPublishFailure()
{
    auto calls = std::make_shared<std::atomic_int>(0);
    WalletHistoryRecoveryController controller{[calls](const std::shared_ptr<Control>&, std::optional<SecureString>) {
        ++*calls;
        return Result{Outcome::Complete, {}};
    }};
    QSignalSpy finished{&controller, &WalletHistoryRecoveryController::finished};
    bool stopping{false};
    connect(&controller, &WalletHistoryRecoveryController::changed, this, [&] {
        if (!stopping && controller.active()) {
            stopping = true;
            controller.shutdown();
        }
    });
    controller.start();
    QVERIFY(stopping);
    QTRY_VERIFY_WITH_TIMEOUT(controller.isDrained(), TIMEOUT);
    QCOMPARE(calls->load(), 0);
    QCOMPARE(finished.count(), 0);
    QVERIFY(controller.completion().valid());
    QVERIFY(controller.completion().wait_for(std::chrono::milliseconds{0}) == std::future_status::ready);
}

void WalletHistoryRecoveryControllerTests::destructionKeepsCompletionValidUntilWorkerExits()
{
    auto gate = std::make_shared<Gate>();
    const auto release = qScopeGuard([gate] { gate->release.release(); });
    auto controller = std::make_unique<WalletHistoryRecoveryController>(
        [gate](const std::shared_ptr<Control>& control, std::optional<SecureString>) -> Result {
            gate->entered.release();
            gate->release.acquire();
            gate->cancellation_seen = control->cancelled.load();
            throw std::runtime_error{"Worker exited after cancellation"};
        });
    bool published{false};
    connect(controller.get(), &WalletHistoryRecoveryController::finished, this, [&] { published = true; });
    controller->start();
    const auto completion = controller->completion();
    QVERIFY(completion.valid());
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, TIMEOUT);
    QVERIFY(completion.wait_for(std::chrono::milliseconds{0}) == std::future_status::timeout);
    QElapsedTimer destroying;
    destroying.start();
    controller.reset();
    QVERIFY(destroying.elapsed() < TIMEOUT / 2);
    bool responsive{false};
    QTimer::singleShot(0, this, [&] { responsive = true; });
    QTRY_VERIFY_WITH_TIMEOUT(responsive, TIMEOUT);
    QVERIFY(completion.wait_for(std::chrono::milliseconds{0}) == std::future_status::timeout);
    QVERIFY(!published);
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(completion.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready, TIMEOUT);
    QVERIFY(gate->cancellation_seen.load());
    completion.get();
    bool drained{false};
    BackendExecutor::shutdownAll(this, [&] { drained = true; });
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    QVERIFY(!published);
}

void WalletHistoryRecoveryControllerTests::failedWorkCanRetry()
{
    auto calls = std::make_shared<std::atomic_int>(0);
    WalletHistoryRecoveryController controller{[calls](const std::shared_ptr<Control>&, std::optional<SecureString>) {
        if (++*calls == 1) throw std::runtime_error{"Peer unavailable"};
        return Result{Outcome::Complete, {}};
    }};
    QSignalSpy finished{&controller, &WalletHistoryRecoveryController::finished};
    controller.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, TIMEOUT);
    QCOMPARE(controller.state(), QStringLiteral("error"));
    QCOMPARE(controller.errorText(), QStringLiteral("Peer unavailable"));
    QVERIFY(controller.incomplete());
    controller.retry();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, TIMEOUT);
    QCOMPARE(controller.state(), QStringLiteral("complete"));
    QVERIFY(controller.errorText().isEmpty());
    QVERIFY(!controller.incomplete());
    controller.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(controller.isDrained(), TIMEOUT);
}

void WalletHistoryRecoveryControllerTests::unlockFailureKeepsPromptAndRetryClearsIt()
{
    WalletHistoryRecoveryController controller{[](const std::shared_ptr<Control>& control, std::optional<SecureString> passphrase) {
        if (!passphrase) {
            control->update({Phase::WaitingForWallet, 0, 0, {}, true});
            return Result{Outcome::Retry, {}};
        }
        if (*passphrase != SecureString{"correct"}) {
            control->update({Phase::WaitingForWallet, 0, 0, {}, true});
            return Result{Outcome::Failed, "The wallet password is incorrect."};
        }
        control->update({Phase::Rescanning, 1, 1});
        return Result{Outcome::Complete, {}};
    }};
    QSignalSpy finished{&controller, &WalletHistoryRecoveryController::finished};
    controller.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, TIMEOUT);
    QCOMPARE(controller.state(), QStringLiteral("waiting-wallet"));
    QVERIFY(controller.needsUnlock());
    QVERIFY(controller.incomplete());
    controller.unlockAndRetry(QStringLiteral("incorrect"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, TIMEOUT);
    QCOMPARE(controller.state(), QStringLiteral("error"));
    QVERIFY(controller.needsUnlock());
    QVERIFY(!controller.errorText().isEmpty());
    controller.unlockAndRetry(QStringLiteral("correct"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, TIMEOUT);
    QCOMPARE(controller.state(), QStringLiteral("complete"));
    QVERIFY(!controller.needsUnlock());
    QVERIFY(!controller.incomplete());
    QVERIFY(controller.errorText().isEmpty());
    controller.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(controller.isDrained(), TIMEOUT);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(WalletHistoryRecoveryControllerTests)
#else
QTEST_MAIN(WalletHistoryRecoveryControllerTests)
#endif
#include "test_wallethistoryrecoverycontroller.moc"
