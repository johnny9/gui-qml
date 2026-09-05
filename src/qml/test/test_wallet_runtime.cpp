// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/wallet_test_fixture.h>
#include <qml/bitcoinqmlapplication.h>
#include <qml/test/integration_test_registry.h>
#include <qml/wallet/walletoperationexecutor.h>
#include <qml/wallet/walletsession.h>

#include <QSignalSpy>
#include <QTest>

class WalletRuntimeTests : public QObject
{
    Q_OBJECT
public:
    explicit WalletRuntimeTests(BitcoinQmlApplication& app) : m_node(app.node()) {}
private Q_SLOTS:
    void fundedFixturesReuseAMatureFaucet()
    {
        WalletTestFixture fixture(m_node);
        auto first = fixture.create();
        auto second = fixture.create();
        fixture.fund(*first, 3 * COIN, 2);
        const int first_height = m_node.getNumBlocks();
        fixture.fund(*second, 4 * COIN);
        QCOMPARE(m_node.getNumBlocks(), first_height + 1);
        QTRY_COMPARE(first->getBalances().balance, 3 * COIN);
        QTRY_COMPARE(second->getBalances().balance, 4 * COIN);
        for (const auto& loaded : m_node.walletLoader().getWallets())
            QVERIFY(!QString::fromStdString(loaded->getWalletName()).startsWith(QStringLiteral("qml-faucet-")));
    }
    void workerExceptionsAreSanitizedAndDrained()
    {
        WalletOperationExecutor executor;
        int completed{0};
        QStringList errors;
        auto completion = [&](WalletOperationResult result) {
            QCOMPARE(result.code, WalletOperationResult::CoreError);
            errors.push_back(result.error);
            ++completed;
        };
        QVERIFY(executor.submit([]() -> WalletOperationResult { throw std::runtime_error("private test credential"); }, completion));
        QVERIFY(executor.submit([]() -> WalletOperationResult { throw QStringLiteral("non-standard private test credential"); }, completion));
        QSignalSpy drained(&executor, &WalletOperationExecutor::drained);
        executor.drain();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 5'000);
        QCOMPARE(completed, 2);
        for (const auto& error : errors) {
            QVERIFY(!error.isEmpty());
            QVERIFY(!error.contains("credential"));
            QVERIFY(!error.contains("private"));
        }
    }
    void realWalletSession()
    {
        WalletTestFixture fixture(m_node);
        WalletOperationExecutor executor;
        WalletSession session(fixture.create(), 17, executor);
        QCOMPARE(session.id(), quint64{17});
        QVERIFY(session.available());
        bool completed{false};
        const QString backup = fixture.backupPath();
        QVERIFY(session.runAction([backup](interfaces::Wallet& wallet) {
            return wallet.backupWallet(backup.toStdString()) ? WalletOperationResult{} :
                WalletOperationResult::failure(WalletOperationResult::CoreError, QStringLiteral("backup failed"));
        }, [&completed](WalletOperationResult result) { completed = result.code == WalletOperationResult::Success; }));
        QVERIFY(!session.runAction([](interfaces::Wallet&) { return WalletOperationResult{}; }, [](WalletOperationResult) {}));
        QTRY_VERIFY_WITH_TIMEOUT(completed, 10'000);
        QVERIFY(!session.actionBusy());
        QCOMPARE(session.generation(), quint64{1});
        session.invalidate();
        QVERIFY(!session.available());
        QCOMPARE(session.generation(), quint64{2});
        QVERIFY(!session.runAction([](interfaces::Wallet&) { return WalletOperationResult{}; }, [](WalletOperationResult) {}));
        QSignalSpy drained(&executor, &WalletOperationExecutor::drained);
        executor.drain();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 10'000);
    }
    void encryptedFixture()
    {
        WalletTestFixture fixture(m_node);
        SecureString password;
        password.assign("test-only-passphrase");
        auto wallet = fixture.create(password);
        QVERIFY(wallet->isCrypted());
        QVERIFY(wallet->isLocked());
        QVERIFY(wallet->unlock(password));
        QVERIFY(wallet->lock());
    }
private:
    interfaces::Node& m_node;
};

BITCOINQML_REGISTER_INTEGRATION_TEST(WalletRuntimeTests)

#include <test_wallet_runtime.moc>
