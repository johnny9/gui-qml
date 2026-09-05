// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/sendpreview.h>
#include <qml/test/qt_test_registry.h>
#include <key_io.h>
#include <test/util/setup_common.h>
#include <wallet/types.h>
#include <QTest>

class SendPreviewTests : public QObject
{
    Q_OBJECT
    struct Attempt {
        std::vector<wallet::CRecipient> recipients;
        wallet::CCoinControl control;
    };
    struct Probe {
        std::vector<std::optional<CAmount>> replies;
        std::vector<Attempt> attempts;
        std::vector<unsigned int> required_sizes;
        CAmount balance{10 * COIN};
        CAmount required{1000};
        explicit Probe(std::vector<std::optional<CAmount>> values = {}) : replies(std::move(values)) {}
        SendPreviewBackend backend()
        {
            return {
                [this](const auto& recipients, const auto& control) -> std::optional<CAmount> {
                    attempts.push_back({recipients, control});
                    return attempts.size() <= replies.size() ? replies[attempts.size() - 1] : std::nullopt;
                },
                [this](const auto&) { return balance; },
                [this](unsigned int size) { required_sizes.push_back(size); return required; },
            };
        }
    };
    static SendDraftSnapshot draft()
    {
        return {11, 3, 17, {{QString::fromStdString(EncodeDestination(WitnessV0KeyHash(uint160{}))), {}, COIN, true, false}}, {}, OutputType::BECH32};
    }
    static std::vector<wallet::CRecipient> recipients(const SendDraftSnapshot& draft)
    {
        std::vector<wallet::CRecipient> result;
        for (const auto& value : draft.recipients) result.push_back({DecodeDestination(value.address.toStdString()), value.amount, value.subtract_fee});
        return result;
    }
private Q_SLOTS:
    void standardTargetsKeepTheirFallbackPolicy_data()
    {
        QTest::addColumn<unsigned int>("target");
        QTest::addColumn<CAmount>("multiplier");
        QTest::newRow("fast") << 1U << CAmount{3};
        QTest::newRow("standard") << 2U << CAmount{2};
        QTest::newRow("economical") << 6U << CAmount{1};
    }
    void standardTargetsKeepTheirFallbackPolicy()
    {
        BasicTestingSetup setup{ChainType::MAIN};
        QFETCH(unsigned int, target);
        QFETCH(CAmount, multiplier);
        auto input = draft();
        input.fees.target = target;
        Probe probe{{std::nullopt, 250}};
        const auto preview = EstimateSendFee(input, recipients(input), 9, probe.backend());
        QCOMPARE(probe.attempts.size(), size_t{2});
        QCOMPARE(probe.required_sizes, std::vector<unsigned int>{1000});
        QVERIFY(!probe.attempts[0].control.m_feerate);
        QCOMPARE(probe.attempts[0].control.m_confirm_target, std::optional<unsigned int>{target});
        QVERIFY(!probe.attempts[1].control.m_confirm_target);
        QCOMPARE(probe.attempts[1].control.m_feerate->GetFeePerK(), probe.required * multiplier);
        for (const auto& attempt : probe.attempts) {
            QVERIFY(IsValidDestination(attempt.control.destChange));
        }
        QCOMPARE(preview.session_id, input.session_id);
        QCOMPARE(preview.session_generation, input.session_generation);
        QCOMPARE(preview.revision, input.revision);
        QCOMPARE(preview.request_id, quint64{9});
        QCOMPARE(preview.fee, std::optional<CAmount>{250});
        QVERIFY(preview.fallback);
        QVERIFY(preview.affordable);
        QVERIFY(preview.error.isEmpty());
    }

    void customRateNeverFallsBackToADifferentPolicy()
    {
        BasicTestingSetup setup{ChainType::MAIN};
        auto input = draft();
        input.fees.custom_per_kvb = 2345;
        Probe probe{{std::nullopt}};
        const auto preview = EstimateSendFee(input, recipients(input), 1, probe.backend());
        QCOMPARE(probe.attempts.size(), size_t{1});
        QVERIFY(probe.required_sizes.empty());
        QCOMPARE(probe.attempts[0].control.m_feerate->GetFeePerK(), CAmount{2345});
        QVERIFY(!probe.attempts[0].control.m_confirm_target);
        QVERIFY(!preview.fee);
        QVERIFY(!preview.affordable);
        QVERIFY(!preview.error.isEmpty());
    }

    void regtestUsesItsExplicitRateAndHonorsCustomOverride()
    {
        BasicTestingSetup setup{ChainType::REGTEST};
        auto input = draft();
        for (const auto custom : {std::optional<CAmount>{}, std::optional<CAmount>{8001}}) {
            input.fees.custom_per_kvb = custom;
            Probe probe{{std::nullopt}};
            const auto preview = EstimateSendFee(input, recipients(input), 1, probe.backend());
            QCOMPARE(probe.attempts.size(), size_t{1});
            QVERIFY(probe.required_sizes.empty());
            const auto expected = custom.value_or(wallet::DEFAULT_TRANSACTION_MINFEE);
            QCOMPARE(probe.attempts[0].control.m_feerate->GetFeePerK(), expected);
            QCOMPARE(CoinControlForDraft(input, false).m_feerate->GetFeePerK(), expected);
            QVERIFY(!preview.fee);
            QVERIFY(!preview.fallback);
        }
    }

    void fullBalanceFeeBufferDoesNotRewriteRecipients_data()
    {
        QTest::addColumn<bool>("custom");
        QTest::newRow("standard") << false;
        QTest::newRow("custom") << true;
    }
    void fullBalanceFeeBufferDoesNotRewriteRecipients()
    {
        BasicTestingSetup setup{ChainType::MAIN};
        QFETCH(bool, custom);
        auto input = draft();
        if (custom) input.fees.custom_per_kvb = 2345;
        input.recipients[0].amount = 3 * COIN;
        input.recipients[0].subtract_fee = false;
        input.recipients.push_back({QString::fromStdString(EncodeDestination(PKHash(uint160{}))), {}, 7 * COIN, false, false});
        const auto original = recipients(input);
        Probe probe{{std::nullopt, 1234}};
        const auto preview = EstimateSendFee(input, original, 1, probe.backend());
        QCOMPARE(probe.attempts.size(), size_t{2});
        QVERIFY(probe.required_sizes.empty());
        for (const auto& attempt : probe.attempts) {
            if (custom) {
                QCOMPARE(attempt.control.m_feerate->GetFeePerK(), CAmount{2345});
                QVERIFY(!attempt.control.m_confirm_target);
            } else {
                QVERIFY(!attempt.control.m_feerate);
                QCOMPARE(attempt.control.m_confirm_target, std::optional<unsigned int>{2});
            }
        }
        QVERIFY(!probe.attempts[0].recipients[0].fSubtractFeeFromAmount);
        QVERIFY(!probe.attempts[0].recipients[1].fSubtractFeeFromAmount);
        QVERIFY(!probe.attempts[1].recipients[0].fSubtractFeeFromAmount);
        QVERIFY(probe.attempts[1].recipients[1].fSubtractFeeFromAmount);
        QCOMPARE(probe.attempts[1].recipients[0].nAmount, 3 * COIN);
        QCOMPARE(probe.attempts[1].recipients[1].nAmount, 7 * COIN);
        QVERIFY(!original[1].fSubtractFeeFromAmount);
        QVERIFY(!input.recipients[1].subtract_fee);
        QVERIFY(!input.recipients[1].maximum);
        QCOMPARE(input.recipients[1].amount, 7 * COIN);
        QCOMPARE(preview.fee, std::optional<CAmount>{1234});
        QVERIFY(preview.fallback);
        QVERIFY(!preview.affordable); // The real draft still needs a fee buffer.
        QVERIFY(!preview.error.isEmpty());
    }

    void failedFallbackNeverPublishesAUsableEstimate_data()
    {
        QTest::addColumn<CAmount>("required");
        QTest::addColumn<int>("attempts");
        QTest::newRow("no-required-fee") << CAmount{0} << 1;
        QTest::newRow("negative-required-fee") << CAmount{-1} << 1;
        QTest::newRow("overflow-guard") << MAX_MONEY << 1;
        QTest::newRow("backend-failure") << CAmount{1000} << 2;
    }
    void failedFallbackNeverPublishesAUsableEstimate()
    {
        BasicTestingSetup setup{ChainType::MAIN};
        QFETCH(CAmount, required);
        QFETCH(int, attempts);
        auto input = draft();
        Probe probe;
        probe.required = required;
        const auto preview = EstimateSendFee(input, recipients(input), 1, probe.backend());
        QCOMPARE(probe.attempts.size(), size_t(attempts));
        QVERIFY(!preview.fee);
        QVERIFY(!preview.affordable);
        QVERIFY(!preview.error.isEmpty());
    }
};

BITCOINQML_REGISTER_QT_TEST(SendPreviewTests)
#include <test_sendpreview.moc>
