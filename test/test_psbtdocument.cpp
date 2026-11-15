// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/psbtdocument.h>
#include <qml/retainedvalue.h>

#include <QSemaphore>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>

namespace {
std::shared_ptr<psbtwork::Snapshot> Analysis(bool known = true, CAmount output = 1'500)
{
    CMutableTransaction funding;
    funding.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    funding.vout.emplace_back(2'000, CScript{} << OP_DROP << OP_TRUE);
    const auto parent = MakeTransactionRef(funding);
    CMutableTransaction transaction;
    transaction.vin.emplace_back(COutPoint{parent->GetHash(), 0});
    transaction.vout.emplace_back(output, CScript{} << OP_TRUE);
    PartiallySignedTransaction document{transaction, 2};
    if (known) document.inputs[0].non_witness_utxo = parent;
    auto snapshot = std::make_shared<psbtwork::Snapshot>();
    snapshot->document = std::make_shared<const PartiallySignedTransaction>(std::move(document));
    snapshot->proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(*snapshot->document));
    snapshot->facts = {true, true, {}};
    snapshot->unsigned_inputs = snapshot->could_sign = 1;
    return snapshot;
}
psbtwork::Result Result(std::shared_ptr<const psbtwork::Snapshot> snapshot)
{
    psbtwork::Result result;
    result.snapshot = std::move(snapshot);
    return result;
}
} // namespace

class PsbtDocumentTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void feeFactsKeepUnknownSeparateFromZero_data()
    {
        QTest::addColumn<bool>("known");
        QTest::addColumn<CAmount>("output");
        QTest::addColumn<bool>("authorized");
        QTest::newRow("known-positive-fee") << true << CAmount{1'500} << true;
        QTest::newRow("known-zero-fee") << true << CAmount{2'000} << true;
        QTest::newRow("unknown-fee") << false << CAmount{1'500} << false;
        QTest::newRow("negative-fee") << true << CAmount{2'001} << false;
        QTest::newRow("negative-output") << true << CAmount{-1} << false;
        QTest::newRow("output-exceeds-money-range") << true << CAmount{MAX_MONEY + 1} << false;
    }
    void feeFactsKeepUnknownSeparateFromZero()
    {
        QFETCH(bool, known);
        QFETCH(CAmount, output);
        QFETCH(bool, authorized);
        auto facts = Analysis(known, output);
        const auto original = psbtwork::SerializeRaw(*facts->document);
        facts->can_sign = facts->can_broadcast = true;
        psbtdocument::Qualify(*facts);
        QCOMPARE(facts->can_sign, authorized);
        QVERIFY(!facts->can_broadcast);
        QCOMPARE(facts->proof->amount.has_value(), authorized);
        if (authorized) QCOMPARE(*facts->proof->amount, CAmount{2'000 - output});
        else QVERIFY(!facts->error.isEmpty());
        QCOMPARE(psbtwork::SerializeRaw(*facts->document), original);
    }

    void loadFailureClearsOneCoherentTuple()
    {
        psbtdocument::Owner owner{9};
        auto facts = Analysis();
        const auto first = owner.consume(1, psbtwork::Kind::Load, Result(facts));
        QVERIFY(first.snapshot);
        QVERIFY(owner.snapshot().analysis->can_sign);
        facts->could_sign = 0;
        QCOMPARE(owner.snapshot().analysis->could_sign, 1);
        psbtwork::Result failure;
        failure.code = psbtwork::Error::Invalid;
        failure.error = QStringLiteral("invalid document");
        const auto failed = owner.consume(2, psbtwork::Kind::Load, std::move(failure));
        QVERIFY(!failed.snapshot);
        QVERIFY(!owner.snapshot().analysis);
        QCOMPARE(owner.snapshot().generation, std::uint64_t{9});
        QCOMPARE(owner.snapshot().operation, std::uint64_t{2});
        QCOMPARE(owner.snapshot().sequence, std::uint64_t{2});
        QCOMPARE(owner.snapshot().phase, psbtdocument::Phase::Failed);
    }

    void olderOutcomeCannotMixWithTheCurrentAnalysis()
    {
        psbtdocument::Owner owner{11};
        const auto current = owner.consume(2, psbtwork::Kind::Load, Result(Analysis()));
        const auto accepted = owner.snapshot().analysis;
        QVERIFY(current.snapshot);
        auto old = Result(Analysis(false));
        old.saved = true;
        old.path = QStringLiteral("original.psbt");
        const auto retired = owner.consume(1, psbtwork::Kind::Inspect, std::move(old));
        QVERIFY(!retired.snapshot);
        QVERIFY(retired.saved);
        QCOMPARE(retired.path, QStringLiteral("original.psbt"));
        QCOMPARE(owner.snapshot().analysis, accepted);
        QCOMPARE(owner.snapshot().sequence, std::uint64_t{1});
    }

    void withdrawnOwnerKeepsEffectReceiptWithoutRetargeting()
    {
        psbtdocument::Owner owner{12};
        owner.withdraw();
        auto completed = Result(Analysis());
        completed.submitted = true;
        completed.txid = QStringLiteral("original transaction identity");
        const auto result = owner.consume(1, psbtwork::Kind::Broadcast, std::move(completed));
        QVERIFY(!result.snapshot);
        QVERIFY(result.submitted);
        QCOMPARE(result.txid, QStringLiteral("original transaction identity"));
        QVERIFY(!owner.snapshot().analysis);
        QCOMPARE(owner.snapshot().sequence, std::uint64_t{0});
    }

    void nativeFactsAreConsumedOnTheRealDomainWorker()
    {
        BackendExecutor executor;
        auto owner = execution::RetainValues<psbtdocument::Owner>(executor.scheduler(), [] {
            return std::make_unique<psbtdocument::Owner>(3);
        });
        QVERIFY(owner);
        const auto gui = QThread::currentThread();
        std::atomic<QThread*> native_thread{nullptr}, domain_thread{nullptr};
        std::atomic_bool qualified{false};
        QSemaphore completed;
        QVERIFY(execution::Submit(*executor.scheduler(), [&] {
            native_thread = QThread::currentThread();
            return Result(Analysis());
        }, [&, owner](psbtwork::Result result) {
            domain_thread = QThread::currentThread();
            const auto accepted = owner->consume(1, psbtwork::Kind::Load, std::move(result));
            qualified = accepted.snapshot && accepted.snapshot->can_sign;
            completed.release();
        }, {}, {execution::Lane::Core, 1024}));
        QVERIFY(completed.tryAcquire(1, 5'000));
        QVERIFY(qualified.load());
        QVERIFY(native_thread.load() && native_thread.load() != gui);
        QVERIFY(domain_thread.load() && domain_thread.load() != gui);
        owner->withdraw();
        owner.reset();
        executor.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(executor.isDrained(), 5'000);
        QCOMPARE(executor.scheduler()->pending(), size_t{0});
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(PsbtDocumentTests)
#else
QTEST_MAIN(PsbtDocumentTests)
#endif
#include <test_psbtdocument.moc>
