// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mockwallet.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletqmlmodel.h>

#include <chainparams.h>
#include <key_io.h>
#include <primitives/transaction.h>

#include <QSemaphore>

#include <atomic>
#include <memory>
#include <vector>

namespace {
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

constexpr auto FEE_ESTIMATE_TIMEOUT_MS{3'000};
const auto VALID_MAINNET_ADDRESS = QStringLiteral("1BoatSLRHtKNngkdXEeobR76b53LETtpyT");

std::unique_ptr<WalletQmlModel> MakeWalletModel(NiceMock<MockWallet>*& wallet_out)
{
    auto wallet = std::make_unique<NiceMock<MockWallet>>();
    wallet_out = wallet.get();

    ON_CALL(*wallet_out, getWalletTxs()).WillByDefault(Return(std::set<interfaces::WalletTx>{}));
    ON_CALL(*wallet_out, listCoins()).WillByDefault(Return(interfaces::Wallet::CoinsList{}));
    ON_CALL(*wallet_out, getBalance()).WillByDefault(Return(10 * COIN));
    ON_CALL(*wallet_out, getDefaultAddressType()).WillByDefault(Return(OutputType::BECH32));
    ON_CALL(*wallet_out, handleTransactionChanged(testing::_)).WillByDefault(Invoke([](interfaces::Wallet::TransactionChangedFn) {
        return std::unique_ptr<interfaces::Handler>{};
    }));
    ON_CALL(*wallet_out, getNewDestinationValue(testing::_, testing::_)).WillByDefault(Invoke([](OutputType, const std::string&) {
        return DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
    }));

    return std::make_unique<WalletQmlModel>(std::move(wallet));
}

void SetValidRecipient(WalletQmlModel& model)
{
    auto* recipient = model.sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);

    recipient->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
    recipient->amount()->setSatoshi(50'000);

    QVERIFY2(recipient->isValid(), "Recipient must be valid before scheduling fee estimates");
}
} // namespace

class WalletQmlModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void feeTargetIndex_mapsStandardTargets();
    void estimatedFeeForTarget_returnsDashWhenUnavailable();
    void importDescriptors_mapsBatchRequestsAndResults();
    void importDescriptors_keepsLocalValidationErrorsIndexed();
    void scheduleFeeEstimates_populatesFormattedEstimates();
    void scheduleFeeEstimates_usesSelectedCoinsInCoinControl();
    void scheduleFeeEstimates_debouncesRapidRestarts();
};

void WalletQmlModelTests::initTestCase()
{
    SelectParams(ChainType::MAIN);
}

void WalletQmlModelTests::feeTargetIndex_mapsStandardTargets()
{
    WalletQmlModel model;

    QCOMPARE(model.feeTargetIndex(1), 0);
    QCOMPARE(model.feeTargetIndex(2), 1);
    QCOMPARE(model.feeTargetIndex(6), 2);
    QCOMPARE(model.feeTargetIndex(42), 1);
}

void WalletQmlModelTests::estimatedFeeForTarget_returnsDashWhenUnavailable()
{
    WalletQmlModel model;

    QCOMPARE(model.estimatedFeeForTarget(1), QStringLiteral("—"));
    QCOMPARE(model.estimatedFeeForTarget(2), QStringLiteral("—"));
    QCOMPARE(model.estimatedFee(), QStringLiteral("—"));
}

void WalletQmlModelTests::importDescriptors_mapsBatchRequestsAndResults()
{
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);

    EXPECT_CALL(*wallet, importDescriptors(testing::_)).WillOnce(Invoke([](const std::vector<interfaces::ImportDescriptorRequest>& requests) -> std::vector<wallet::ImportDescriptorResult> {
        EXPECT_EQ(requests.size(), 2U);
        EXPECT_EQ(requests.at(0).descriptor, std::string{"wpkh([deadbeef/84h/0h/0h]xpub661MyMwAqRbcF7example/0/*)#qqqqqqqq"});
        EXPECT_TRUE(requests.at(0).active.has_value());
        if (requests.at(0).active.has_value()) EXPECT_TRUE(requests.at(0).active.value());
        EXPECT_TRUE(requests.at(0).range.has_value());
        if (requests.at(0).range.has_value()) {
            EXPECT_EQ(requests.at(0).range->first, 0);
            EXPECT_EQ(requests.at(0).range->second, 100);
        }
        EXPECT_TRUE(requests.at(0).next_index.has_value());
        if (requests.at(0).next_index.has_value()) EXPECT_EQ(requests.at(0).next_index.value(), 5);

        EXPECT_EQ(requests.at(1).descriptor, std::string{"tr([f00dbabe/86h/0h/0h]xpub661MyMwAqRbcF7example/0/*)#pppppppp"});
        EXPECT_TRUE(requests.at(1).internal.has_value());
        if (requests.at(1).internal.has_value()) EXPECT_TRUE(requests.at(1).internal.value());
        EXPECT_TRUE(requests.at(1).label.has_value());
        if (requests.at(1).label.has_value()) EXPECT_EQ(requests.at(1).label.value(), std::string{"cold storage"});

        wallet::ImportDescriptorResult success;
        success.success = true;
        success.used_default_range = true;
        success.warnings = {"Range not given, using default keypool range"};

        wallet::ImportDescriptorResult failure;
        failure.success = false;
        failure.error = "Descriptor import failed";
        failure.reason = wallet::ImportDescriptorResult::FailureReason::WALLET_ERROR;

        return std::vector<wallet::ImportDescriptorResult>{success, failure};
    }));

    const QVariantList requests{
        QVariantMap{
            {QStringLiteral("desc"), QStringLiteral("wpkh([deadbeef/84h/0h/0h]xpub661MyMwAqRbcF7example/0/*)#qqqqqqqq")},
            {QStringLiteral("timestamp"), 123LL},
            {QStringLiteral("active"), true},
            {QStringLiteral("range"), QVariantList{0, 100}},
            {QStringLiteral("nextIndex"), 5LL},
        },
        QVariantMap{
            {QStringLiteral("descriptor"), QStringLiteral("tr([f00dbabe/86h/0h/0h]xpub661MyMwAqRbcF7example/0/*)#pppppppp")},
            {QStringLiteral("timestamp"), 456LL},
            {QStringLiteral("internal"), true},
            {QStringLiteral("label"), QStringLiteral("cold storage")},
        },
    };

    const QVariantList results = model->importDescriptors(requests);
    QCOMPARE(results.size(), 2);

    const QVariantMap first_result = results.at(0).toMap();
    QCOMPARE(first_result.value(QStringLiteral("success")).toBool(), true);
    QCOMPARE(first_result.value(QStringLiteral("usedDefaultRange")).toBool(), true);
    QCOMPARE(first_result.value(QStringLiteral("reason")).toString(), QStringLiteral("none"));
    const QStringList expected_warnings{QStringLiteral("Range not given, using default keypool range")};
    QCOMPARE(first_result.value(QStringLiteral("warnings")).toStringList(), expected_warnings);

    const QVariantMap second_result = results.at(1).toMap();
    QCOMPARE(second_result.value(QStringLiteral("success")).toBool(), false);
    QCOMPARE(second_result.value(QStringLiteral("error")).toString(), QStringLiteral("Descriptor import failed"));
    QCOMPARE(second_result.value(QStringLiteral("reason")).toString(), QStringLiteral("wallet_error"));
}

void WalletQmlModelTests::importDescriptors_keepsLocalValidationErrorsIndexed()
{
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);

    EXPECT_CALL(*wallet, importDescriptors(testing::_)).WillOnce(Invoke([](const std::vector<interfaces::ImportDescriptorRequest>& requests) -> std::vector<wallet::ImportDescriptorResult> {
        EXPECT_EQ(requests.size(), 1U);
        EXPECT_EQ(requests.front().descriptor, std::string{"raw(deadbeef)#llllllll"});

        wallet::ImportDescriptorResult result;
        result.success = true;
        return std::vector<wallet::ImportDescriptorResult>{result};
    }));

    const QVariantList requests{
        QVariantMap{
            {QStringLiteral("descriptor"), QStringLiteral("missing-timestamp")},
        },
        QVariantMap{
            {QStringLiteral("descriptor"), QStringLiteral("raw(deadbeef)#llllllll")},
            {QStringLiteral("timestamp"), 42LL},
        },
    };

    const QVariantList results = model->importDescriptors(requests);
    QCOMPARE(results.size(), 2);

    const QVariantMap first_result = results.at(0).toMap();
    QCOMPARE(first_result.value(QStringLiteral("success")).toBool(), false);
    QCOMPARE(first_result.value(QStringLiteral("reason")).toString(), QStringLiteral("invalid_parameter"));
    QVERIFY(first_result.value(QStringLiteral("error")).toString().contains(QStringLiteral("timestamp")));

    const QVariantMap second_result = results.at(1).toMap();
    QCOMPARE(second_result.value(QStringLiteral("success")).toBool(), true);
    QCOMPARE(second_result.value(QStringLiteral("reason")).toString(), QStringLiteral("none"));
}

void WalletQmlModelTests::scheduleFeeEstimates_populatesFormattedEstimates()
{
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model);

    QSemaphore release_first_call;
    std::atomic<bool> first_call_started{false};
    std::atomic<bool> first_call_blocked{false};
    std::atomic<bool> saw_sign_true{false};
    std::atomic<bool> saw_wrong_recipient_count{false};
    std::atomic<bool> saw_selected_inputs{false};
    std::atomic<bool> saw_nonempty_feerate{false};
    std::vector<unsigned int> requested_targets;

    EXPECT_CALL(*wallet, getNewDestinationValue(OutputType::BECH32, "qml-fee-preview")).Times(1);
    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>& recipients,
                                           const wallet::CCoinControl& coin_control,
                                           bool sign,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        if (sign) saw_sign_true = true;
        if (recipients.size() != 1U) saw_wrong_recipient_count = true;
        if (coin_control.HasSelected() || !coin_control.ListSelected().empty()) saw_selected_inputs = true;
        if (coin_control.m_feerate.has_value()) saw_nonempty_feerate = true;

        requested_targets.push_back(coin_control.m_confirm_target.value_or(0));
        change_pos = -1;
        fee = coin_control.m_confirm_target.value_or(0) * 100;
        if (!first_call_blocked.exchange(true)) {
            first_call_started = true;
            release_first_call.acquire();
        }
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();

    QTRY_VERIFY_WITH_TIMEOUT(first_call_started.load(), FEE_ESTIMATE_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("…"));

    release_first_call.release();

    QTRY_VERIFY_WITH_TIMEOUT(requested_targets.size() == 3, FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(!saw_sign_true.load());
    QVERIFY(!saw_wrong_recipient_count.load());
    QVERIFY(!saw_selected_inputs.load());
    QVERIFY(!saw_nonempty_feerate.load());
    QCOMPARE(requested_targets.at(0), 1U);
    QCOMPARE(requested_targets.at(1), 2U);
    QCOMPARE(requested_targets.at(2), 6U);
    QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(1), QStringLiteral("0.00000100 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("0.00000200 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(6), QStringLiteral("0.00000600 ₿"));
    QCOMPARE(model->estimatedFee(), QStringLiteral("0.00000200 ₿"));
}

void WalletQmlModelTests::scheduleFeeEstimates_usesSelectedCoinsInCoinControl()
{
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model);

    const COutPoint selected_outpoint{Txid{}, 5};
    std::atomic<int> create_transaction_calls{0};
    std::atomic<bool> saw_sign_true{false};
    std::atomic<bool> saw_missing_selection{false};
    std::atomic<int> selected_count{-1};
    std::vector<unsigned int> selected_targets;

    EXPECT_CALL(*wallet, getNewDestinationValue(OutputType::BECH32, "qml-fee-preview")).Times(1);
    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>&,
                                           const wallet::CCoinControl& coin_control,
                                           bool sign,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        if (sign) saw_sign_true = true;
        if (!coin_control.HasSelected() || !coin_control.IsSelected(selected_outpoint)) saw_missing_selection = true;
        const auto selected = coin_control.ListSelected();
        selected_count = static_cast<int>(selected.size());
        if (selected.size() != 1U || selected.front() != selected_outpoint) saw_missing_selection = true;

        selected_targets.push_back(coin_control.m_confirm_target.value_or(0));
        ++create_transaction_calls;
        change_pos = -1;
        fee = coin_control.m_confirm_target.value_or(0) * 200;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->selectCoin(selected_outpoint);

    QTRY_COMPARE_WITH_TIMEOUT(create_transaction_calls.load(), 3, FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(!saw_sign_true.load());
    QVERIFY(!saw_missing_selection.load());
    QCOMPARE(selected_count.load(), 1);
    QCOMPARE(selected_targets.size(), 3U);
    QCOMPARE(selected_targets.at(0), 1U);
    QCOMPARE(selected_targets.at(1), 2U);
    QCOMPARE(selected_targets.at(2), 6U);
}

void WalletQmlModelTests::scheduleFeeEstimates_debouncesRapidRestarts()
{
    NiceMock<MockWallet>* wallet{nullptr};
    auto model = MakeWalletModel(wallet);
    SetValidRecipient(*model);

    std::atomic<int> create_transaction_calls{0};

    EXPECT_CALL(*wallet, getNewDestinationValue(OutputType::BECH32, "qml-fee-preview")).Times(1);
    std::atomic<bool> saw_sign_true{false};
    wallet->createTransactionHandler = [&](const std::vector<wallet::CRecipient>&,
                                           const wallet::CCoinControl& coin_control,
                                           bool sign,
                                           int& change_pos,
                                           CAmount& fee) -> util::Result<CTransactionRef> {
        if (sign) saw_sign_true = true;
        ++create_transaction_calls;
        change_pos = -1;
        fee = coin_control.m_confirm_target.value_or(0) * 250;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();
    model->scheduleFeeEstimates();
    model->scheduleFeeEstimates();

    QTRY_COMPARE_WITH_TIMEOUT(create_transaction_calls.load(), 3, FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(!saw_sign_true.load());
    QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(1), QStringLiteral("0.00000250 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("0.00000500 ₿"));
    QCOMPARE(model->estimatedFeeForTarget(6), QStringLiteral("0.00001500 ₿"));
}

int RunWalletQmlModelTests(int argc, char* argv[])
{
    WalletQmlModelTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#ifndef BITCOINQML_NO_TEST_MAIN
QTEST_MAIN(WalletQmlModelTests)
#endif
#include "test_walletqmlmodel.moc"
