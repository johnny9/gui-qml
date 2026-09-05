// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/wallet_test_fixture.h>
#include <qml/wallet/addresslistmodel.h>
#include <qml/wallet/signverifymessagemodel.h>
#include <qml/wallet/transactionhistorymodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/walletreceivemodel.h>
#include <key_io.h>
#include <univalue.h>
#include <QTest>

class WalletAddressIntegrationTests : public QObject
{
    Q_OBJECT
public:
    explicit WalletAddressIntegrationTests(BitcoinQmlApplication& app) : m_node{app.node()} {}
private Q_SLOTS:
    void labelsPropagateFromCoreToHistory()
    {
        WalletTestFixture fixture{m_node};
        WalletOperationExecutor executor;
        auto backend{fixture.create()};
        WalletSession session{backend, 4201, executor};
        AddressListModel addresses{session};
        TransactionHistoryModel history{session};
        const auto destination{backend->getNewDestination(OutputType::LEGACY, "before")};
        QVERIFY(destination);
        const QString address{QString::fromStdString(EncodeDestination(*destination))};
        UniValue params{UniValue::VARR};
        params.push_back(1);
        params.push_back(address.toStdString());
        m_node.executeRpc("generatetoaddress", params, "");
        QTRY_VERIFY_WITH_TIMEOUT(!addresses.busy() && !addresses.details(address).isEmpty() && history.count() == 1, 10'000);
        QCOMPARE(addresses.details(address).value("scriptType").toString(), QStringLiteral("P2PKH"));
        QVERIFY(addresses.details(address).value("canSign").toBool());
        QVERIFY(addresses.details(address).value("isUsed").toBool());
        QVERIFY(addresses.details(address).value("balanceSat").toLongLong() > 0);
        QVERIFY(addresses.setLabel(address, "after"));
        QTRY_VERIFY_WITH_TIMEOUT(!addresses.busy() && addresses.details(address).value("label").toString() == "after" &&
                                history.records().front().label == "after", 10'000);
        std::string label;
        QVERIFY(backend->getAddress(*destination, &label, nullptr));
        QCOMPARE(label, std::string{"after"});
        WalletReceiveModel receive{session, QStringLiteral("regtest")};
        QTRY_VERIFY_WITH_TIMEOUT(receive.available() && !receive.busy(), 10'000);
        QVERIFY(receive.useAddress(address));
        QTRY_VERIFY_WITH_TIMEOUT(!receive.busy() && receive.draft()->address() == address, 10'000);
        QCOMPARE(receive.draft()->label(), QStringLiteral("after"));
        QVERIFY(receive.save());
        QTRY_VERIFY_WITH_TIMEOUT(!receive.busy() && receive.history()->count() == 1, 10'000);
        QCOMPARE(receive.draft()->address(), address);
        QVERIFY(receive.useAddress("unknown"));
        QTRY_VERIFY_WITH_TIMEOUT(!receive.busy() && !receive.error().isEmpty(), 10'000);
        QVERIFY(!addresses.setLabel("unknown", "not saved"));
        session.invalidate();
        QVERIFY(!addresses.setLabel(address, "closed"));
    }
    void signingUsesLocalKeysAndRestoresLockState()
    {
        WalletTestFixture fixture{m_node};
        WalletOperationExecutor executor;
        SecureString password{"test-only-passphrase"};
        auto backend{fixture.create(password)};
        const auto destination{backend->getNewDestination(OutputType::LEGACY, "signing")};
        QVERIFY(destination);
        const QString address{QString::fromStdString(EncodeDestination(*destination))};
        WalletSession session{backend, 4202, executor};
        SignVerifyMessageModel tool{session};
        QVERIFY(tool.sign(address, "exact message"));
        QTRY_VERIFY_WITH_TIMEOUT(!tool.busy() && tool.needsUnlock(), 10'000);
        QVERIFY(backend->isLocked());
        QVERIFY(tool.sign(address, "exact message", "incorrect"));
        QTRY_VERIFY_WITH_TIMEOUT(!tool.busy() && !tool.signingError().isEmpty(), 10'000);
        QVERIFY(tool.signature().isEmpty());
        QVERIFY(backend->isLocked());
        QVERIFY(tool.sign(address, "exact message", "test-only-passphrase"));
        QTRY_VERIFY_WITH_TIMEOUT(!tool.busy() && !tool.signature().isEmpty(), 10'000);
        QVERIFY(backend->isLocked());
        QVERIFY(tool.verify(address, "exact message", tool.signature()));
        QVERIFY(!tool.verify(address, "changed", tool.signature()));
        QVERIFY(backend->unlock(password));
        QVERIFY(tool.sign(address, "already unlocked"));
        QTRY_VERIFY_WITH_TIMEOUT(!tool.busy() && !tool.signature().isEmpty(), 10'000);
        QVERIFY(!backend->isLocked());
        QVERIFY(backend->lock());
        const auto witness{backend->getNewDestination(OutputType::BECH32, "")};
        QVERIFY(witness);
        QVERIFY(!tool.sign(QString::fromStdString(EncodeDestination(*witness)), "unsupported"));
        auto other{fixture.create()};
        const auto foreign{other->getNewDestination(OutputType::LEGACY, "")};
        QVERIFY(foreign);
        QVERIFY(tool.sign(QString::fromStdString(EncodeDestination(*foreign)), "not our key"));
        QTRY_VERIFY_WITH_TIMEOUT(!tool.busy() && !tool.signingError().isEmpty(), 10'000);
        QVERIFY(tool.signature().isEmpty());
        QVERIFY(backend->isLocked());
        session.invalidate();
        QVERIFY(!tool.sign(address, "closed"));
    }
    void privateKeyDisabledWalletCannotSign()
    {
        WalletTestFixture fixture{m_node};
        WalletOperationExecutor executor;
        auto owner{fixture.create()};
        const auto destination{owner->getNewDestination(OutputType::LEGACY, "")};
        QVERIFY(destination);
        auto backend{fixture.create({}, wallet::WALLET_FLAG_DESCRIPTORS | wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS)};
        WalletSession session{backend, 4203, executor};
        SignVerifyMessageModel tool{session};
        QVERIFY(tool.sign(QString::fromStdString(EncodeDestination(*destination)), "no local key"));
        QTRY_VERIFY_WITH_TIMEOUT(!tool.busy() && !tool.signingError().isEmpty(), 10'000);
        QVERIFY(tool.signature().isEmpty());
    }
private:
    interfaces::Node& m_node;
};
BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(WalletAddressIntegrationTests)
#include <test_wallet_address_integration.moc>
