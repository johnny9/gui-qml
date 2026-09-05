// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/applicationrouter.h>
#include <qml/wallet/walletmanager.h>
#include <qml/wallet/walletsendmodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/sendprepare.h>
#include <qml/wallet/sendsubmit.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/wallet_test_fixture.h>
#include <key_io.h>
#include <univalue.h>
#include <util/moneystr.h>
#include <wallet/types.h>
#include <QPointer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>
#include <atomic>

class SendSubmissionIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    static void fill(WalletSendModel& send)
    {
        send.recipients()->setAddress(0, QString::fromStdString(EncodeDestination(WitnessV0KeyHash(uint160{}))));
        send.recipients()->setAmount(0, "1");
        send.recipients()->setLabel(0, "send fixture recipient");
    }
public:
    explicit SendSubmissionIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void exactPreparedTransactionIsSubmittedOnceAndAppearsInCore()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        fixture.fund(*backend);
        WalletOperationExecutor executor;
        WalletSession session(backend, 801, executor);
        WalletSendModel send(session, m_app.node().getDustRelayFee());
        QTRY_VERIFY(!send.coins()->busy());
        fill(send);
        const auto own = backend->getNewDestination(OutputType::BECH32, "original receive label");
        QVERIFY(own);
        send.recipients()->add();
        send.recipients()->setAddress(1, QString::fromStdString(EncodeDestination(*own)));
        send.recipients()->setAmount(1, "1");
        send.recipients()->setLabel(1, "updated receive label");
        QSignalSpy submitted(&send, &WalletSendModel::submitted);
        QVERIFY(!send.submit());
        QVERIFY(send.prepare(""));
        QTRY_VERIFY_WITH_TIMEOUT(send.canSubmit(), 10'000);
        const auto reviewed = *send.review()->snapshot();
        const auto count = backend->getWalletTxs().size();
        QVERIFY(send.submit());
        QVERIFY(!send.submit());
        QVERIFY(!send.prepare(""));
        QTRY_COMPARE_WITH_TIMEOUT(submitted.count(), 1, 10'000);
        QVERIFY(!send.submit());
        QCOMPARE(backend->getWalletTxs().size(), count + 1);
        QCOMPARE(backend->getWalletTx(reviewed.transaction->GetHash()).tx->GetWitnessHash(), reviewed.transaction->GetWitnessHash());
        QCOMPARE(send.submittedTransactionId(), QString::fromStdString(reviewed.transaction->GetHash().ToString()));
        QVERIFY(send.submissionStatus().contains("local node"));
        QVERIFY(!send.review()->hasReview());
        QVERIFY(send.snapshot().recipients.empty());
        QVERIFY(!send.coins()->manual());
        UniValue entry(UniValue::VARR);
        entry.push_back(send.submittedTransactionId().toStdString());
        const auto mempool = m_app.node().executeRpc("getmempoolentry", entry, "");
        QCOMPARE(ParseMoney(mempool["fees"]["base"].getValStr()), std::optional<CAmount>{reviewed.fee});
        std::string core_label;
        QVERIFY(backend->getAddress(WitnessV0KeyHash(uint160{}), &core_label, nullptr));
        QCOMPARE(QString::fromStdString(core_label), QString("send fixture recipient"));
        wallet::AddressPurpose purpose;
        QVERIFY(backend->getAddress(*own, &core_label, &purpose));
        QCOMPARE(QString::fromStdString(core_label), QString("updated receive label"));
        QCOMPARE(purpose, wallet::AddressPurpose::RECEIVE);
    }

    void externalCoinLockRejectsStalePreparationAndKeepsDraft()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        fixture.fund(*backend);
        WalletOperationExecutor executor;
        WalletSession session(backend, 802, executor);
        WalletSendModel send(session, m_app.node().getDustRelayFee());
        QTRY_VERIFY(!send.coins()->busy());
        fill(send);
        QVERIFY(send.prepare(""));
        QTRY_VERIFY_WITH_TIMEOUT(send.canSubmit(), 10'000);
        const auto tx = send.review()->snapshot()->transaction;
        const auto count = backend->getWalletTxs().size();
        QVERIFY(backend->lockCoin(tx->vin[0].prevout, false));
        QVERIFY(send.submit()); // The worker rechecks Core, not just the cached UI.
        QTRY_VERIFY_WITH_TIMEOUT(!send.busy(), 10'000);
        QCOMPARE(backend->getWalletTxs().size(), count);
        QVERIFY(send.error().contains("locked"));
        QVERIFY(send.submittedTransactionId().isEmpty());
        QCOMPARE(send.snapshot().recipients[0].amount, COIN);
        QVERIFY(!send.canSubmit());
        QVERIFY(backend->unlockCoin(tx->vin[0].prevout));
        send.coins()->refresh();
        QTRY_VERIFY(!send.coins()->busy());
        QVERIFY(send.prepare(""));
        QTRY_VERIFY_WITH_TIMEOUT(send.canSubmit(), 10'000);
        session.invalidate();
        QVERIFY(!send.submit());
        QVERIFY(!send.review()->hasReview());
    }

    void walletRecordingIsNotReportedAsNodeAcceptance()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        fixture.fund(*backend);
        WalletOperationExecutor executor;
        WalletSession session(backend, 803, executor);
        WalletSendModel send(session, m_app.node().getDustRelayFee());
        QTRY_VERIFY(!send.coins()->busy());
        fill(send);
        QVERIFY(send.prepare(""));
        QTRY_VERIFY_WITH_TIMEOUT(send.canSubmit(), 10'000);
        auto review = *send.review()->snapshot();
        // At the adapter boundary, a deliberately invalid witness exercises
        // real Core rejection after commitTransaction's void wallet-record step.
        CMutableTransaction invalid(*review.transaction);
        QVERIFY(!invalid.vin[0].scriptWitness.stack.empty());
        QVERIFY(!invalid.vin[0].scriptWitness.stack[0].empty());
        invalid.vin[0].scriptWitness.stack[0][0] ^= 1;
        review.transaction = MakeTransactionRef(invalid);
        const auto result = std::make_shared<SendSubmissionResult>();
        bool finished{false};
        QVERIFY(session.runAction([result, review](interfaces::Wallet& wallet) {
            *result = SubmitWalletSend(wallet, review, {});
            return WalletOperationResult{};
        }, [&finished](WalletOperationResult) { finished = true; }));
        QTRY_VERIFY_WITH_TIMEOUT(finished, 10'000);
        QVERIFY(result->recorded);
        QVERIFY(!result->accepted);
        QVERIFY(!result->error.isEmpty());
        QVERIFY(backend->getWalletTx(review.transaction->GetHash()).tx);
        const auto mempool = m_app.node().executeRpc("getrawmempool", UniValue{UniValue::VARR}, "");
        for (const auto& txid : mempool.getValues()) QVERIFY(txid.get_str() != review.transaction->GetHash().ToString());
    }

    void walletSwitchInvalidatesReviewWithoutRetargetingDraft()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        fixture.fund(*backend);
        auto other = fixture.create();
        auto& manager = *m_app.walletManager();
        const auto name = QString::fromStdString(backend->getWalletName());
        const auto other_name = QString::fromStdString(other->getWalletName());
        QTRY_VERIFY_WITH_TIMEOUT(manager.selectedWallet(), 10'000);
        manager.selectWallet(name);
        QTRY_VERIFY_WITH_TIMEOUT(manager.selectedWallet() && manager.selectedWallet()->overview()->name() == name, 10'000);
        auto* wallet = manager.selectedWallet();
        const auto session_id = wallet->sessionId();
        auto& send = *wallet->send();
        QTRY_VERIFY(!send.coins()->busy());
        fill(send);
        QVERIFY(send.prepare(""));
        QTRY_VERIFY_WITH_TIMEOUT(send.canSubmit(), 10'000);
        manager.selectWallet(other_name);
        QTRY_VERIFY_WITH_TIMEOUT(manager.selectedWallet() && manager.selectedWallet()->overview()->name() == other_name, 10'000);
        QVERIFY(!send.canSubmit());
        QCOMPARE(send.snapshot().session_id, wallet->session().id());
        QCOMPARE(send.snapshot().recipients[0].amount, COIN);
        QCOMPARE(manager.walletBySession(session_id), wallet);
        manager.closeWallet(name);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.walletBySession(session_id), 10'000);
    }

    void acceptedSubmitRetainsOriginalBackendAcrossSwitchAndClose()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        fixture.fund(*backend);
        auto other_backend = fixture.create();
        const QString name = QString::fromStdString(backend->getWalletName());
        const QString other_name = QString::fromStdString(other_backend->getWalletName());
        auto& manager = *m_app.walletManager();
        const auto loaded = [&](const QString& expected) {
            for (int row = 0; row < manager.catalog()->rowCount(); ++row) {
                const auto index = manager.catalog()->index(row);
                if (manager.catalog()->data(index, WalletListModel::NameRole).toString() == expected)
                    return manager.catalog()->data(index, WalletListModel::LoadedRole).toBool();
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(loaded(name) && loaded(other_name), 10'000);
        manager.selectWallet(name);
        QTRY_VERIFY_WITH_TIMEOUT(manager.selectedWallet() && manager.selectedWallet()->overview()->name() == name, 10'000);
        QPointer<WalletViewModel> original = manager.selectedWallet();
        auto* send = original->send();
        const QString session_id = original->sessionId();
        QTRY_VERIFY_WITH_TIMEOUT(!send->coins()->busy(), 10'000);
        fill(*send);
        QVERIFY(send->prepare(""));
        QTRY_VERIFY_WITH_TIMEOUT(send->canSubmit(), 10'000);
        const auto reviewed = *send->review()->snapshot();
        const auto original_count = backend->getWalletTxs().size();
        backend.reset(); // Only production owners can keep the original backend alive.

        struct Gate { QSemaphore release; std::atomic<bool> entered{false}; };
        const auto gate = std::make_shared<Gate>();
        struct ReleaseOnExit {
            std::shared_ptr<Gate> gate;
            ~ReleaseOnExit() { gate->release.release(); }
        } release_on_exit{gate};
        QVERIFY(original->session().runRead([gate](interfaces::Wallet&) {
            gate->entered = true;
            gate->release.acquire();
            return WalletOperationResult{};
        }, [](WalletOperationResult) {}));
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.load(), 5'000);
        QSignalSpy submitted(send, &WalletSendModel::submitted);
        QVERIFY(send->submit()); // Accepted, but queued behind the deterministic read barrier.
        QVERIFY(!send->submit());
        manager.selectWallet(other_name);
        QTRY_VERIFY_WITH_TIMEOUT(manager.selectedWallet() && manager.selectedWallet()->overview()->name() == other_name, 5'000);
        QPointer<WalletViewModel> selected = manager.selectedWallet();
        QVERIFY(selected != original);
        QVERIFY(selected->send()->submittedTransactionId().isEmpty());
        manager.closeWallet(name);
        QVERIFY(!manager.walletBySession(session_id));
        QVERIFY(original); // Its accepted action prevents premature retirement.
        QVERIFY(!original->session().available());
        QVERIFY(original->session().actionBusy());
        QVERIFY(!send->submit());

        gate->release.release();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy() && !original, 10'000);
        QCOMPARE(submitted.count(), 0); // Invalidated session completion is not published.
        QCOMPARE(manager.selectedWallet(), selected.data());
        QVERIFY(selected->send()->submittedTransactionId().isEmpty());
        QVERIFY(selected->send()->submissionStatus().isEmpty());
        QVERIFY(selected->send()->snapshot().recipients.empty());
        QVERIFY(!manager.walletBySession(session_id));
        UniValue args(UniValue::VARR);
        args.push_back(reviewed.transaction->GetHash().ToString());
        const auto mempool = m_app.node().executeRpc("getmempoolentry", args, "");
        QCOMPARE(ParseMoney(mempool["fees"]["base"].getValStr()), std::optional<CAmount>{reviewed.fee});

        // Reopen only after the original action and close both completed. This
        // independently proves the original wallet, not the selection, recorded it.
        std::vector<bilingual_str> warnings;
        auto reopened = m_app.node().walletLoader().loadWallet(name.toStdString(), warnings);
        QVERIFY(reopened);
        QCOMPARE((*reopened)->getWalletTxs().size(), original_count + 1);
        const auto recorded = (*reopened)->getWalletTx(reviewed.transaction->GetHash()).tx;
        QVERIFY(recorded);
        QCOMPARE(recorded->GetWitnessHash(), reviewed.transaction->GetWitnessHash());
        (*reopened)->remove();
    }
};

BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(SendSubmissionIntegrationTests)

/** Run in isolated processes with Core's startup broadcast policies enabled. */
class SendNoBroadcastIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
public:
    explicit SendNoBroadcastIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void walletPolicyCannotBeOverriddenBySend()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        const auto address = backend->getNewDestination(OutputType::BECH32, "policy fixture");
        QVERIFY(address);
        UniValue mine(UniValue::VARR);
        mine.push_back(101);
        mine.push_back(EncodeDestination(*address));
        m_app.node().executeRpc("generatetoaddress", mine, "");
        QTRY_VERIFY(backend->getBalance() > COIN);
        WalletOperationExecutor executor;
        WalletSession session(backend, 805, executor);
        WalletSendModel send(session, m_app.node().getDustRelayFee());
        QTRY_VERIFY(!send.coins()->busy());
        send.recipients()->setAddress(0, QString::fromStdString(EncodeDestination(WitnessV0KeyHash(uint160{}))));
        send.recipients()->setAmount(0, "1");
        QSignalSpy submitted(&send, &WalletSendModel::submitted);
        QVERIFY(send.prepare(""));
        QTRY_VERIFY_WITH_TIMEOUT(send.canSubmit(), 10'000);
        const auto reviewed = *send.review()->snapshot();
        const auto count = backend->getWalletTxs().size();
        QVERIFY(send.submit());
        QTRY_VERIFY_WITH_TIMEOUT(!send.busy(), 10'000);
        QCOMPARE(backend->getWalletTxs().size(), count + 1);
        QCOMPARE(backend->getWalletTx(reviewed.transaction->GetHash()).tx->GetWitnessHash(), reviewed.transaction->GetWitnessHash());
        QCOMPARE(send.submittedTransactionId(), QString::fromStdString(reviewed.transaction->GetHash().ToString()));
        QCOMPARE(submitted.count(), 0);
        QVERIFY(!send.canSubmit());
        QVERIFY(send.submissionStatus().contains("not currently in the node mempool"));
        const auto mempool = m_app.node().executeRpc("getrawmempool", UniValue{UniValue::VARR}, "");
        QVERIFY(mempool.empty());
    }
};

BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(SendNoBroadcastIntegrationTests)
#include <test_sendsubmit_integration.moc>
