// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>
#include <QSemaphore>
#include <QTimer>
#include <qscopeguard.h>

#include <qml/models/walletqmlmodel.h>
#include <test/mocks/mockwallet.h>

#include <atomic>
#include <memory>

namespace {
struct BackupGate {
    QSemaphore entered;
    QSemaphore release;
    std::atomic<bool> on_worker{false};
    std::atomic<int> calls{0};
};

class GatedBackupWallet : public StubWallet
{
public:
    explicit GatedBackupWallet(std::shared_ptr<BackupGate> gate) : m_gate(std::move(gate)) {}
    std::string getWalletName() override { return "backup-test"; }
    bool backupWallet(const std::string&) override
    {
        ++m_gate->calls;
        m_gate->on_worker = QThread::currentThread() != QCoreApplication::instance()->thread();
        m_gate->entered.release();
        m_gate->release.acquire();
        return false;
    }

private:
    std::shared_ptr<BackupGate> m_gate;
};
} // namespace

class WalletSecurityAsyncTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void backupKeepsGuiResponsiveAndReportsFailure();
    void closeDrainsPendingBackupWithoutDeliveringCompletion();
};

void WalletSecurityAsyncTests::backupKeepsGuiResponsiveAndReportsFailure()
{
    auto gate = std::make_shared<BackupGate>();
    WalletQmlModel model(std::unique_ptr<interfaces::Wallet>{std::make_unique<GatedBackupWallet>(gate)});
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    QSignalSpy completed(&model, &WalletQmlModel::settingsOperationFinished);
    QVERIFY(model.backupWallet("/tmp/backup-test.bak"));
    QTRY_VERIFY(gate->entered.available() > 0);
    gate->entered.acquire();
    QVERIFY(gate->on_worker);
    QVERIFY(model.settingsBusy());
    QVERIFY(!model.backupWallet("/tmp/duplicate.bak"));
    bool heartbeat{false};
    QTimer::singleShot(0, &model, [&heartbeat] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QCOMPARE(completed.count(), 0);

    gate->release.release();
    QTRY_COMPARE(completed.count(), 1);
    QCOMPARE(gate->calls.load(), 1);
    QVERIFY(!model.settingsBusy());
    QCOMPARE(completed.at(0).at(0).toString(), QStringLiteral("backup"));
    QVERIFY(!completed.at(0).at(1).toBool());
    QVERIFY(!model.settingsError().isEmpty());
    QSignalSpy drained(&model, &WalletQmlModel::shutdownFinished);
    model.beginShutdown();
    QTRY_COMPARE(drained.count(), 1);
}

void WalletSecurityAsyncTests::closeDrainsPendingBackupWithoutDeliveringCompletion()
{
    auto gate = std::make_shared<BackupGate>();
    WalletQmlModel model(std::unique_ptr<interfaces::Wallet>{std::make_unique<GatedBackupWallet>(gate)});
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    QSignalSpy completed(&model, &WalletQmlModel::settingsOperationFinished);
    QSignalSpy drained(&model, &WalletQmlModel::shutdownFinished);
    QVERIFY(model.backupWallet("/tmp/backup-test.bak"));
    QTRY_VERIFY(gate->entered.available() > 0);
    gate->entered.acquire();
    model.beginShutdown();
    bool heartbeat{false};
    QTimer::singleShot(0, &model, [&heartbeat] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QCOMPARE(drained.count(), 0);
    gate->release.release();
    QTRY_COMPARE(drained.count(), 1);
    QCOMPARE(completed.count(), 0);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(WalletSecurityAsyncTests)
#else
QTEST_MAIN(WalletSecurityAsyncTests)
#endif
#include "test_walletsecurityasync.moc"
