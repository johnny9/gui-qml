// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/paymentimport.h>
#include <qml/paymentimportreview.h>
#include <qml/models/bitcoinurimodel.h>
#include <qml/psbtwork.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>
#include <key_io.h>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>
namespace {
QString Address() { return QString::fromStdString(EncodeDestination(PKHash{uint160{std::vector<unsigned char>(20, 3)}})); }
QString Uri() { return "bitcoin:" + Address() + "?amount=0.12345678&label=Public&message=Message"; }
std::shared_ptr<const PartiallySignedTransaction> Document()
{
    CMutableTransaction tx; tx.vin.emplace_back(COutPoint{Txid::FromHex("0101010101010101010101010101010101010101010101010101010101010101").value(), 0});
    tx.vout.emplace_back(1, CScript{} << OP_TRUE);
    return std::make_shared<const PartiallySignedTransaction>(tx, 2);
}
paymentimport::Inspection Psbt()
{
    paymentimport::Inspection source; source.kind = paymentimport::Kind::Psbt;
    auto snapshot = std::make_shared<psbtwork::Snapshot>(); snapshot->document = Document(); source.psbt = snapshot; return source;
}
paymentimport::Target Target(uint64_t revision = 7, bool conflict = false)
{ static const auto opening = std::make_shared<const walletopening::Identity>(); return {opening, 2, revision, 3, true, true, conflict}; }
struct Fixture {
    ManualRoutedExecution source, target;
    paymentimport::Snapshot snapshot;
    std::chrono::steady_clock::time_point now{};
    std::vector<std::function<void()>> wakes;
    std::shared_ptr<paymentimport::Owner> owner;
    std::shared_ptr<paymentimport::TargetPort> port{std::make_shared<paymentimport::TargetPort>()};
    paymentimport::Target facts{Target()};
    int reads{0}, analyses{0}, writes{0}, queries{0}, acks{0}, retired{0};
    bool lose_reply{false}, unknown_query{false}, reject_ack{false};
    std::optional<paymentimport::ChildReply> result;
    Fixture() {
        port->opening = facts.opening; port->draft = facts.draft; port->recipient = facts.recipient;
        port->observe = [this](auto, auto done, auto failed) {
            return execution::Submit(*target.scheduler, [this] { return facts; }, std::move(done), std::move(failed), {execution::Lane::Domain, 512});
        };
        port->dispatch = [this](auto, auto child, auto, auto done, auto failed) {
            return execution::Submit(*target.scheduler, [this, child] {
                ++writes; result = paymentimport::ChildReply{{child, paymentimport::Outcome::Applied, 8, 8}, {}, {}, {}}; return *result;
            }, [this, done](auto reply) { if (!lose_reply) done(std::move(reply)); }, std::move(failed), {execution::Lane::Domain, 512});
        };
        port->query = [this](auto, auto child, auto done, auto failed) {
            return execution::Submit(*target.scheduler, [this, child] {
                ++queries; return unknown_query || !result ? paymentimport::ChildReply{{child, paymentimport::Outcome::Unknown, 0, 0}, {}, {}, {}} : *result;
            }, std::move(done), std::move(failed), {execution::Lane::Domain, 512});
        };
        port->acknowledge = [this](auto, auto) { ++acks; return execution::Admission{{}, reject_ack ? execution::Rejection::Transport : execution::Rejection::None}; };
        paymentimport::Services services; services.scheduler = source.scheduler;
        services.read = [this](const auto& input) { ++reads; return paymentimport::ReadSource(input); };
        services.analyze = [this](const auto& input) { ++analyses; return paymentimport::AnalyzeSource(input); };
        services.now = [this] { return now; }; services.wake = [this](auto, auto wake) { wakes.push_back(std::move(wake)); };
        owner = paymentimport::Owner::Create(42, services, [this](auto value) { snapshot = std::move(value); }, [this](auto) { ++retired; });
    }
    ~Fixture() { owner->close(); drain(); owner.reset(); drain(); }
    void drain() { for (size_t n{0}; n < 20 && (!source.queuedIdle() || !target.queuedIdle()); ++n) { source.drain(); target.drain(); } }
    void inspect() { owner->inspect({paymentimport::Input::Kind::Text, Uri()}); owner->chooseTarget(port); drain(); }
    void advance() { now += std::chrono::milliseconds{100}; auto pending = std::exchange(wakes, {}); for (auto& wake : pending) wake(); drain(); }
};
} // namespace
class PaymentImportTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void readerKeepsPublicMetadataSeparateFromPrivateNote() {
        const auto result = paymentimport::ReadSource({paymentimport::Input::Kind::Text, Uri()});
        QCOMPARE(result.kind, paymentimport::Kind::Payment); QCOMPARE(result.payment.address, Address());
        QCOMPARE(result.payment.amount, std::optional<CAmount>{12'345'678});
        QCOMPARE(result.payment.label, QStringLiteral("Public")); QCOMPARE(result.payment.message, QStringLiteral("Message"));
        QVERIFY(result.payment.private_note.isEmpty());
    }
    void sourceIdentityIncludesMessageAndCanonicalPath() {
        const auto first = paymentimport::ReadSource({paymentimport::Input::Kind::Text, Uri()});
        const auto second = paymentimport::ReadSource({paymentimport::Input::Kind::Text, Uri() + "2"});
        QVERIFY(first.source_identity != second.source_identity);
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("payment");
        { QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(Uri().toUtf8()), Uri().toUtf8().size()); }
        const auto local = paymentimport::ReadSource({paymentimport::Input::Kind::File, path});
        QCOMPARE(local.kind, paymentimport::Kind::Payment); QVERIFY(first.source_identity != local.source_identity);
        QCOMPARE(local.source_identity, paymentimport::ReadSource({paymentimport::Input::Kind::File, QUrl::fromLocalFile(path).toString()}).source_identity);
    }
    void boundedAndMalformedSourcesNeverBecomeCapabilities_data() {
        QTest::addColumn<QString>("text");
        QTest::newRow("malformed") << "bitcoin://" + Address();
        QTest::newRow("required") << "bitcoin:" + Address() + "?req-unknown=value";
        QTest::newRow("amount") << "bitcoin:" + Address() + "?amount=1.000000001";
        QTest::newRow("large") << QString(1024 * 1024 + 1, 'x');
    }
    void boundedAndMalformedSourcesNeverBecomeCapabilities() {
        QFETCH(QString, text); const auto result = paymentimport::ReadSource({paymentimport::Input::Kind::Text, text});
        QCOMPARE(result.kind, paymentimport::Kind::Unknown); QVERIFY(!result.error.isEmpty());
    }
    void psbtInspectionIsStandaloneAndRetainsOriginalDocument() {
        const auto parsed = paymentimport::ReadSource({paymentimport::Input::Kind::Text, QString::fromLatin1(psbtwork::SerializeRaw(*Document()).toBase64())});
        QCOMPARE(parsed.kind, paymentimport::Kind::Psbt); QVERIFY(parsed.psbt->document);
        const auto analyzed = paymentimport::AnalyzeSource(parsed); QCOMPARE(analyzed.kind, paymentimport::Kind::Psbt);
        QVERIFY(analyzed.psbt); QCOMPARE(psbtwork::SerializeRaw(*analyzed.psbt->document), psbtwork::SerializeRaw(*Document()));
    }
    void sourceAndTargetExecutorsKeepSeparateDecisions() {
        Fixture f; f.owner->inspect({paymentimport::Input::Kind::Text, Uri()}); f.owner->chooseTarget(f.port);
        f.source.drain(); QCOMPARE(f.reads, 1); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Inspecting);
        QVERIFY(!f.snapshot.can_apply); QCOMPARE(f.writes, 0);
        f.target.drain(); QVERIFY(!f.snapshot.can_apply); // Foreign Domain only fills a reply cell.
        f.source.drain(); QVERIFY(f.snapshot.can_apply); QCOMPARE(f.writes, 0);
        f.owner->apply(); f.source.drain(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applying);
        f.target.drain(); QCOMPARE(f.writes, 1); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applying);
        f.source.drain(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applied); QVERIFY(!f.snapshot.pending); QCOMPARE(f.acks, 1);
    }
    void explicitUseWaitsForInspectionAndOriginalTarget() {
        Fixture f; f.owner->use(); f.owner->inspect({paymentimport::Input::Kind::Text, Uri()}); f.owner->chooseTarget(f.port); f.drain();
        QCOMPARE(f.writes, 1); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applied);
    }
    void replacementPromptIsOriginalSourceAndRevisionBound() {
        Fixture f; f.facts.conflicts = true; f.inspect(); f.owner->apply(); f.drain();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::AwaitingReplacement); QVERIFY(f.snapshot.prompt);
        const auto old = f.snapshot.prompt->id; f.owner->replace(old + 1); f.drain(); QCOMPARE(f.writes, 0);
        f.owner->keepDraft(old); f.drain(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Inspecting);
        f.owner->replace(old); f.drain(); QCOMPARE(f.writes, 0);
        f.owner->apply(); f.drain(); QVERIFY(f.snapshot.prompt->id != old);
        f.owner->replace(f.snapshot.prompt->id); f.drain(); QCOMPARE(f.writes, 1);
    }
    void lostApplyAcknowledgmentQueriesWithoutRepeatingMutation() {
        Fixture f; f.lose_reply = true; f.inspect(); f.owner->apply(); f.drain(); QCOMPARE(f.writes, 1);
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applying); f.advance();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applied); QCOMPARE(f.queries, 1); QCOMPARE(f.writes, 1); QCOMPARE(f.acks, 1);
    }
    void knownOutcomeRetainedUntilAcknowledgeAdmission() {
        Fixture f; f.reject_ack = true; f.inspect(); f.owner->apply(); f.drain();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applied); QVERIFY(f.snapshot.pending); QCOMPARE(f.writes, 1);
        f.owner->close(); f.drain(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Closing); QCOMPARE(f.retired, 0);
        f.reject_ack = false; f.advance(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Closed); QCOMPARE(f.retired, 1); QCOMPARE(f.writes, 1);
    }
    void unknownAfterCloseKeepsOriginalChildAndReadOnlyRecovery() {
        Fixture f; f.lose_reply = true; f.unknown_query = true; f.inspect(); f.owner->apply(); f.drain();
        f.owner->close(); f.drain(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Closing); QVERIFY(f.snapshot.pending); QCOMPARE(f.retired, 0);
        f.advance(); QCOMPARE(f.writes, 1); QVERIFY(f.queries >= 1); QCOMPARE(f.retired, 0);
        f.unknown_query = false; f.advance(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Closed); QCOMPARE(f.retired, 1); QCOMPARE(f.writes, 1);
    }
    void unavailableTargetDoesNotInvalidateStandaloneSource() {
        Fixture f; f.facts.available = false; f.inspect(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Inspecting);
        QVERIFY(f.snapshot.inspection); QVERIFY(!f.snapshot.can_apply); f.owner->apply(); f.drain(); QCOMPARE(f.writes, 0);
    }
    void flowRetainsKnownRejectionAcrossAcknowledgmentFailure() {
        paymentimport::Flow flow; const auto source = flow.inspect(); QVERIFY(source); QVERIFY(flow.parsed(*source, paymentimport::Kind::Payment));
        QVERIFY(flow.chooseTarget(Target())); const auto child = flow.apply(); QVERIFY(child);
        QVERIFY(flow.finished({*child, paymentimport::Outcome::Rejected, 0, 7})); QVERIFY(flow.child());
        QVERIFY(flow.acknowledgmentLost(*child)); QVERIFY(flow.acknowledged(*child));
        QCOMPARE(flow.phase(), paymentimport::Phase::Failed); QCOMPARE(flow.error(), paymentimport::Error::Rejected);
    }
    void reviewFactoryQueriesOriginalOutcomeWithoutRecreating() {
        ManualRoutedExecution commands; auto base = std::make_shared<paymentimport::TargetPort>();
        base->opening = Target().opening; base->draft = 2; base->recipient = 3;
        int creates{0}, presentations{0}; bool current{true};
        paymentimport::ReviewServices services; services.scheduler = commands.scheduler; services.view = 7; services.current = [&] { return current; };
        services.prepare = [&](auto, auto done, auto failed) {
            return execution::Submit(*commands.scheduler, [&, done] {
                ++creates; psbtwork::ImportResult result; result.review = std::make_shared<const walletreview::Snapshot>(); result.document = Document(); done(result);
            }, [] {}, std::move(failed), {execution::Lane::Core, 512});
        };
        services.present = [&](auto, auto) { ++presentations; };
        const auto port = paymentimport::WithReviewFactory(base, services); QVERIFY(port);
        const paymentimport::Child child{1, 1, paymentimport::ChildKind::CreateReview, Target(), false};
        std::optional<paymentimport::ChildReply> queried; const auto source = std::make_shared<const paymentimport::Inspection>(Psbt());
        QVERIFY(port->dispatch({42, 1}, child, source, [](auto) {}, {}));
        commands.drain(); QCOMPARE(creates, 1); QCOMPARE(presentations, 1);
        current = false; commands.scheduler->closeOrdinaryAdmission();
        QVERIFY(port->query({42, 1}, child, [&](auto reply) { queried = reply; }, {})); commands.drain();
        QVERIFY(queried); QCOMPARE(queried->receipt.outcome, paymentimport::Outcome::ReviewOpened); QCOMPARE(creates, 1);
        QVERIFY(port->acknowledge({42, 1}, child)); commands.drain();
    }
    void typedFacadePublishesBoundedSourceWithoutTargetOrGuiParsing() {
        ManualRoutedExecution commands; std::deque<std::function<void()>> gui;
        BitcoinUriModel model; QObject owner;
        model.setServices({commands.scheduler, [&](auto post) { gui.push_back(std::move(post)); }});
        auto* operation = model.requestImport({paymentimport::Input::Kind::Text, Uri()}, {}, &owner); QVERIFY(operation); QVERIFY(operation->pending());
        commands.drain(); QVERIFY(operation->address().isEmpty());
        while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
        QCOMPARE(operation->address(), Address()); QCOMPARE(operation->publicLabel(), QStringLiteral("Public"));
        QCOMPARE(operation->message(), QStringLiteral("Message")); QCOMPARE(operation->amount(), qint64{12'345'678}); QVERIFY(!operation->canApply());
        operation->cancel(); commands.drain(); model.closeAdmission(); commands.drain();
    }
};
BITCOINQML_REGISTER_QT_TEST(PaymentImportTests)
#include <test_paymentimport.moc>
