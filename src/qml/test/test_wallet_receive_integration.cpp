// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/wallet_test_fixture.h>
#include <qml/wallet/walletreceivemodel.h>
#include <qml/wallet/walletmanager.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/walletunlockcontext.h>
#include <key_io.h>
#include <univalue.h>
#include <QSignalSpy>
#include <QTest>

class WalletReceiveIntegrationTests : public QObject
{
    Q_OBJECT
public:
    explicit WalletReceiveIntegrationTests(BitcoinQmlApplication& app) : m_app{app}, m_node{app.node()} {}
private Q_SLOTS:
    void requestAndAddressDefaultSurviveWalletReload()
    {
        WalletTestFixture fixture{m_node};
        auto backend{fixture.create()};
        const QString name{QString::fromStdString(backend->getWalletName())};
        auto& manager{*m_app.walletManager()};
        QTRY_VERIFY_WITH_TIMEOUT(manager.selectedWallet(), 10'000);
        manager.selectWallet(name);
        QTRY_COMPARE_WITH_TIMEOUT(manager.selectedWallet()->overview()->name(), name, 10'000);
        auto* receive{manager.selectedWallet()->receive()};
        const QString session_id{manager.selectedWallet()->sessionId()};
        QTRY_VERIFY_WITH_TIMEOUT(receive->available() && !receive->busy(), 10'000);
        receive->setDefaultAddressType("legacy");
        receive->draft()->setLabel("Persisted");
        receive->draft()->setAmount("0.25");
        QVERIFY(receive->save());
        QTRY_VERIFY_WITH_TIMEOUT(!receive->busy() && receive->history()->count() == 1, 10'000);
        const QString request_id{receive->draft()->id()}, address{receive->draft()->address()};
        backend.reset();
        manager.closeWallet(name);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy() && !manager.walletBySession(session_id), 10'000);
        manager.selectWallet(name);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy() && manager.selectedWallet() && manager.selectedWallet()->overview()->name() == name, 10'000);
        auto* reloaded{manager.selectedWallet()->receive()};
        QTRY_VERIFY_WITH_TIMEOUT(reloaded->available() && !reloaded->busy() && reloaded->history()->count() == 1, 10'000);
        QCOMPARE(reloaded->defaultAddressType(), QStringLiteral("legacy"));
        const auto entry{reloaded->history()->entryById(request_id)};
        QVERIFY(entry);
        QCOMPARE(entry->recipient.address, address.toStdString());
        QCOMPARE(entry->recipient.amount, CAmount{25'000'000});
        QCOMPARE(entry->recipient.label, std::string{"Persisted"});
        QVERIFY(manager.selectedWallet()->sessionId() != session_id);
    }

    void importedWatchOnlyDescriptorCanReceiveWithoutLocalSigning()
    {
        WalletTestFixture fixture{m_node};
        auto owner{fixture.create()};
        auto watcher{fixture.create({}, wallet::WALLET_FLAG_DESCRIPTORS | wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS)};
        QVERIFY(watcher->getAvailableAddressTypes().empty());
        const auto descriptors{m_node.executeRpc("listdescriptors", UniValue{UniValue::VARR}, "/wallet/" + owner->getWalletName())};
        std::string descriptor;
        for (const auto& item : descriptors["descriptors"].getValues()) {
            if (item["desc"].get_str().starts_with("wpkh(") && !item["internal"].get_bool()) {
                descriptor = item["desc"].get_str();
                break;
            }
        }
        QVERIFY(!descriptor.empty());
        UniValue item{UniValue::VOBJ};
        item.pushKV("desc", descriptor);
        item.pushKV("active", true);
        item.pushKV("timestamp", "now");
        UniValue requests{UniValue::VARR};
        requests.push_back(item);
        UniValue parameters{UniValue::VARR};
        parameters.push_back(requests);
        const auto imported{m_node.executeRpc("importdescriptors", parameters, "/wallet/" + watcher->getWalletName())};
        QVERIFY(imported[0]["success"].get_bool());
        WalletOperationExecutor executor;
        WalletSession session{watcher, 4103, executor};
        WalletReceiveModel receive{session, QStringLiteral("regtest")};
        QTRY_VERIFY_WITH_TIMEOUT(receive.available() && !receive.busy(), 10'000);
        QCOMPARE(receive.addressTypes(), QStringList{QStringLiteral("bech32")});
        QVERIFY(receive.save());
        QTRY_VERIFY_WITH_TIMEOUT(!receive.busy() && receive.history()->count() == 1, 10'000);
        QVERIFY(IsValidDestination(DecodeDestination(receive.draft()->address().toStdString())));
        QVERIFY(watcher->privateKeysDisabled());
    }

    void requestCrudAndReloadUseCoreStorage()
    {
        WalletTestFixture fixture{m_node};
        WalletOperationExecutor executor;
        auto backend{fixture.create()};
        WalletSession session{backend, 4101, executor};
        WalletReceiveModel receive{session, QStringLiteral("regtest")};
        QTRY_VERIFY_WITH_TIMEOUT(receive.available() && !receive.busy(), 10'000);
        QCOMPARE(receive.addressTypes().size(), 4);
        receive.draft()->setAmount(QStringLiteral("0.001"));
        receive.draft()->setLabel(QStringLiteral("Original"));
        receive.draft()->setNoteSelf(QStringLiteral("Private note"));
        QVERIFY(receive.save());
        QTRY_VERIFY_WITH_TIMEOUT(!receive.draft()->id().isEmpty() && !receive.busy(), 10'000);
        const auto first_id{receive.draft()->id()};
        const auto address{receive.draft()->address()};
        QVERIFY(IsValidDestination(DecodeDestination(address.toStdString())));
        QVERIFY(!receive.draft()->uri().contains(QStringLiteral("Private")));
        receive.draft()->setLabel(QStringLiteral("Edited"));
        QVERIFY(receive.save());
        QTRY_VERIFY_WITH_TIMEOUT(!receive.busy(), 10'000);
        QCOMPARE(receive.draft()->id(), first_id);
        QVERIFY(receive.useAsTemplate(first_id, true));
        QVERIFY(receive.draft()->id().isEmpty());
        QVERIFY(receive.save());
        QTRY_VERIFY_WITH_TIMEOUT(!receive.busy() && receive.history()->count() == 2, 10'000);
        const auto second_id{receive.draft()->id()};
        QVERIFY(second_id != first_id);
        QCOMPARE(receive.draft()->address(), address);
        const auto stored{ReceiveRequestHistoryModel::DeserializeEntries(backend->getAddressReceiveRequests())};
        QCOMPARE(stored.size(), size_t{2});
        QSignalSpy reload{receive.history(), &ReceiveRequestHistoryModel::countChanged};
        receive.reload();
        QTRY_VERIFY_WITH_TIMEOUT(!reload.empty() && !receive.busy(), 10'000);
        QCOMPARE(receive.history()->matchingEntriesForAddress(address).size(), 2);
        QVERIFY(receive.remove(first_id));
        QTRY_VERIFY_WITH_TIMEOUT(!receive.busy() && receive.history()->count() == 1, 10'000);
        QVERIFY(receive.history()->entryById(second_id));
        QCOMPARE(backend->getAddressReceiveRequests().size(), size_t{1});
        QVERIFY(receive.remove(second_id));
        QTRY_VERIFY_WITH_TIMEOUT(!receive.busy() && receive.history()->count() == 0, 10'000);
        receive.clear();
        QVERIFY(receive.save());
        QTRY_VERIFY_WITH_TIMEOUT(!receive.busy() && receive.history()->count() == 1, 10'000);
        QVERIFY(receive.draft()->id().toLongLong() > second_id.toLongLong());
        session.invalidate();
        QVERIFY(!receive.save());
    }

    void encryptedReceiveAndUnlockContextRestoreLockState()
    {
        WalletTestFixture fixture{m_node};
        WalletOperationExecutor executor;
        SecureString password{"test-only-passphrase"};
        auto backend{fixture.create(password)};
        WalletSession session{backend, 4102, executor};
        WalletReceiveModel receive{session, QStringLiteral("regtest")};
        QTRY_VERIFY_WITH_TIMEOUT(receive.available() && !receive.busy(), 10'000);
        QVERIFY(receive.save(QStringLiteral("incorrect")));
        QTRY_VERIFY_WITH_TIMEOUT(!receive.busy() && !receive.error().isEmpty(), 10'000);
        QVERIFY(backend->isLocked());
        QVERIFY(receive.save(QStringLiteral("test-only-passphrase")));
        QTRY_VERIFY_WITH_TIMEOUT(!receive.busy() && receive.history()->count() == 1, 10'000);
        QVERIFY(backend->isLocked());
        bool complete{false};
        QVERIFY(session.runAction([password](interfaces::Wallet& wallet) {
            WalletUnlockContext unlock{wallet, password};
            if (!unlock.valid()) return WalletOperationResult::failure(WalletOperationResult::CoreError, unlock.error());
            return WalletOperationResult::failure(WalletOperationResult::InvalidInput, QStringLiteral("injected action failure"));
        }, [&complete](WalletOperationResult) { complete = true; }));
        QTRY_VERIFY_WITH_TIMEOUT(complete, 10'000);
        QVERIFY(backend->isLocked());
        QVERIFY(backend->unlock(password));
        complete = false;
        QVERIFY(session.runAction([](interfaces::Wallet& wallet) {
            WalletUnlockContext unlock{wallet, {}};
            return WalletOperationResult{};
        }, [&complete](WalletOperationResult) { complete = true; }));
        QTRY_VERIFY_WITH_TIMEOUT(complete, 10'000);
        QVERIFY(!backend->isLocked()); // The action did not acquire this unlock.
        QVERIFY(backend->lock());
    }
private:
    BitcoinQmlApplication& m_app;
    interfaces::Node& m_node;
};
BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(WalletReceiveIntegrationTests)
#include <test_wallet_receive_integration.moc>
