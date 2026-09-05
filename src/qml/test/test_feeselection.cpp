// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/feeselectionmodel.h>
#include <qml/test/qt_test_registry.h>
#include <QSignalSpy>
#include <QTest>

class FeeSelectionTests : public QObject
{
    Q_OBJECT
    struct Request { SendDraftSnapshot draft; quint64 id; FeeSelectionModel::Complete complete; };
    static SendDraftSnapshot draft(quint64 revision)
    {
        return {11, 3, revision, {{"address", {}, COIN, false, false}}, {}, OutputType::BECH32, {}, false};
    }
    static FeePreview reply(const Request& request, CAmount fee)
    {
        return {request.draft.session_id, request.draft.session_generation, request.draft.revision, request.id, fee, true, false, {}};
    }
private Q_SLOTS:
    void feeRatePrecision()
    {
        QCOMPARE(ParseCustomFeeRate("1"), std::optional<CAmount>{1000});
        QCOMPARE(ParseCustomFeeRate("1."), std::optional<CAmount>{1000});
        QCOMPARE(ParseCustomFeeRate(" 0.001 "), std::optional<CAmount>{1});
        QCOMPARE(ParseCustomFeeRate("12.345"), std::optional<CAmount>{12345});
        for (const QString invalid : {"", "0", "-1", "1.0001", "1e2", "1,5", "9000000000000000000", "nan"}) {
            QVERIFY2(!ParseCustomFeeRate(invalid), qPrintable(invalid));
        }
    }

    void coalescesEditsAndRejectsStaleReplies()
    {
        std::vector<Request> requests;
        FeeSelectionModel model([&requests](auto draft, auto id, auto complete) {
            requests.push_back({draft, id, complete});
        }, nullptr, 0);
        model.setDraft(draft(1));
        QTRY_COMPARE(requests.size(), size_t{1});
        model.setDraft(draft(2));
        model.setDraft(draft(3));
        QCOMPARE(requests.size(), size_t{1});
        QCoreApplication::processEvents();
        const auto first = requests[0];
        first.complete(reply(first, 111));
        QTRY_COMPARE(requests.size(), size_t{2});
        QCOMPARE(requests[1].draft.revision, quint64{3});
        QVERIFY(model.estimatedFee().isEmpty());
        QVERIFY(model.pending());
        const auto latest = requests[1];
        latest.complete(reply(latest, 333));
        QCOMPARE(model.result().fee, std::optional<CAmount>{333});
        QVERIFY(!model.pending());
        QSignalSpy policy(&model, &FeeSelectionModel::policyChanged);
        model.setDisplayUnit(3);
        QCOMPARE(model.estimatedFee(), QString("333 sat"));
        QCOMPARE(policy.count(), 0);
        QCOMPARE(requests.size(), size_t{2});
    }

    void invalidDraftPolicyAndGenerationCannotReviveAnEstimate()
    {
        std::vector<Request> requests;
        FeeSelectionModel model([&requests](auto draft, auto id, auto complete) { requests.push_back({draft, id, complete}); }, nullptr, 0);
        model.setDraft(draft(1));
        QTRY_COMPARE(requests.size(), size_t{1});
        model.invalidate();
        const auto first = requests[0];
        first.complete(reply(first, 10));
        QVERIFY(!model.pending());
        QVERIFY(!model.result().fee);
        model.setCustom(true);
        QVERIFY(!model.valid());
        model.setDraft(draft(2));
        QVERIFY(!model.pending());
        model.setCustomRate("2.345");
        QCOMPARE(model.policy().custom_per_kvb, std::optional<CAmount>{2345});
        model.setDraft(draft(3));
        QTRY_COMPARE(requests.size(), size_t{2});
        const auto second = requests[1];
        auto wrong_generation = reply(second, 11);
        ++wrong_generation.session_generation;
        second.complete(wrong_generation);
        QVERIFY(!model.result().fee);
        const int target = model.target();
        model.setTarget(-1);
        QCOMPARE(model.target(), target);
    }

    void completionAfterDestruction()
    {
        std::optional<Request> request;
        {
            FeeSelectionModel model([&request](auto draft, auto id, auto complete) { request = Request{draft, id, complete}; }, nullptr, 0);
            model.setDraft(draft(1));
            QTRY_VERIFY(request.has_value());
        }
        request->complete(reply(*request, 1));
    }
};

BITCOINQML_REGISTER_QT_TEST(FeeSelectionTests)
#include <test_feeselection.moc>
