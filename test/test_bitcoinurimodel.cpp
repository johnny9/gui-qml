// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bitcoinurimodel.h>
#include <qml/backendexecutor.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>
#include <key_io.h>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QtTest/QtTest>
#include <stdexcept>

namespace {
constexpr qint64 MAX_BYTES{1024 * 1024};
QString Address() { return QString::fromStdString(EncodeDestination(PKHash{uint160{std::vector<unsigned char>(20, 3)}})); }
QString Uri(const QString& label = QStringLiteral("Invoice")) { return "bitcoin:" + Address() + "?amount=0.12345678&label=" + label; }
bool Write(const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
struct Fixture {
    ManualRoutedExecution commands;
    std::deque<std::function<void()>> publications;
    std::unique_ptr<QObject> owner{std::make_unique<QObject>()};
    std::unique_ptr<BitcoinUriModel> model{std::make_unique<BitcoinUriModel>()};
    bool fail_post{false};
    explicit Fixture(execution::Limits limits = execution::Limits::Bounded()) : commands(limits) {
        model->setServices({commands.scheduler, [this](std::function<void()> turn) {
            if (fail_post) throw std::runtime_error{"GUI transport unavailable"};
            publications.push_back(std::move(turn));
        }});
    }
    ~Fixture() { if (model) model->closeAdmission(); owner.reset(); model.reset(); commands.drain(); }
    void publish() { while (!publications.empty()) { auto turn = std::move(publications.front()); publications.pop_front(); turn(); } }
    void finish() { do { commands.drain(); publish(); } while (!commands.queuedIdle() || !commands.activeIdle()); }
};
class RecordingScheduler final : public execution::Scheduler {
    struct RecordedTask final : execution::Task {
        std::shared_ptr<execution::Task> task;
        std::atomic<QThread*>* thread;
        RecordedTask(std::shared_ptr<execution::Task> value, std::atomic<QThread*>* output) : task(std::move(value)), thread(output) {}
        void execute() override { *thread = QThread::currentThread(); task->execute(); }
        void deliver() override { task->deliver(); }
        void notStarted(execution::NotStarted::Cause cause) override { task->notStarted(cause); }
    };
    const std::shared_ptr<execution::Scheduler> m_inner;
public:
    std::atomic<QThread*> native_thread{nullptr};
    explicit RecordingScheduler(std::shared_ptr<execution::Scheduler> inner) : m_inner(std::move(inner)) {}
    execution::Admission submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec = {}) override {
        if (spec.lane == execution::Lane::Core) task = std::make_shared<RecordedTask>(std::move(task), &native_thread);
        return m_inner->submit(std::move(task), spec);
    }
    execution::Admission reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override { return m_inner->reserveCleanup(std::move(task), spec); }
    bool activateCleanup(execution::TaskId id) override { return m_inner->activateCleanup(id); }
    execution::BorrowAdmission borrow(execution::ResourceKey key) override { return m_inner->borrow(key); }
    execution::ResourceKey allocateKey() override { return m_inner->allocateKey(); }
    size_t pending() const override { return m_inner->pending(); }
    void closeOrdinaryAdmission() override { m_inner->closeOrdinaryAdmission(); }
    bool cancelQueued(execution::TaskId id) override { return m_inner->cancelQueued(id); }
};
} // namespace

class BitcoinUriModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void textHasSeparateDomainOutcomeAndGuiPublication() {
        Fixture f; auto* operation = f.model->requestParse(Uri("Invoice%207"), f.owner.get()); QVERIFY(operation);
        QSignalSpy finished{operation, &BitcoinUriParseOperation::finished};
        QVERIFY(operation->pending()); QVERIFY(operation->result().isEmpty());
        QVERIFY(f.commands.step(execution::Lane::Domain));
        operation->retryPublication(); // Native parsing alone cannot publish.
        QCOMPARE(finished.count(), 0); QVERIFY(operation->pending()); QVERIFY(operation->result().isEmpty());
        QVERIFY(f.commands.step(execution::Lane::Domain));
        QVERIFY(operation->pending()); QVERIFY(operation->result().isEmpty());
        f.publish();
        QVERIFY(!operation->pending()); QCOMPARE(finished.count(), 1);
        const auto result = operation->result(); QVERIFY(result.value("success").toBool());
        QCOMPARE(result.value("address").toString(), Address());
        QCOMPARE(result.value("amountSats").toLongLong(), qint64{12'345'678});
        QCOMPARE(result.value("label").toString(), QStringLiteral("Invoice 7"));
        operation->retryPublication(); QCOMPARE(finished.count(), 1);
    }
    void probeOwnsConflictsAndDerivedMetadataUntilMatchingPublication() {
        Fixture f;
        const QString input = Uri("Invoice") + "&message=Private%20note";
        const QVariantMap context{{"address", Address()}, {"amount", qint64{1}}, {"note", "Existing"}, {"sourceField", "address"}};
        auto* op = f.model->requestProbe(input, context, f.owner.get()); QVERIFY(op);
        QVERIFY(op->pending()); QVERIFY(op->result().isEmpty());
        f.commands.drain(); QVERIFY(op->pending()); f.publish();
        const auto result = op->result(); QVERIFY(result.value("probeSucceeded").toBool());
        QVERIFY(result.value("isPaymentUri").toBool()); QVERIFY(result.value("conflicts").toBool());
        QCOMPARE(result.value("privateNote").toString(), QStringLiteral("Private note - Invoice"));
        const auto identity = result.value("sourceId").toString(); QCOMPARE(identity.size(), qsizetype{64});
        auto* same = f.model->requestProbe(input, {{"address", Address()}, {"amount", qint64{12'345'678}}, {"requestLabel", "Invoice"}, {"note", "Private note - Invoice"}}, f.owner.get());
        f.finish(); QCOMPARE(same->result().value("sourceId").toString(), identity);
        QVERIFY(same->result().value("matchesExistingRequest").toBool()); QVERIFY(!same->result().value("conflicts").toBool());
    }
    void probeVerifiesExactNativePasteProvenance_data() {
        QTest::addColumn<QString>("previous"); QTest::addColumn<QString>("current"); QTest::addColumn<QString>("literal"); QTest::addColumn<bool>("accepted");
        const auto uri = Uri();
        const QString replaced = QString{"12"} + uri + "34";
        const QString unrelated = QString{"99"} + uri;
        QTest::newRow("replace-selection") << QString{"12.34"} << replaced << uri << true;
        QTest::newRow("unrelated-prefix") << QString{"12"} << unrelated << uri << false;
        QTest::newRow("unchanged") << uri << uri << uri << false;
        const QString padded = QString(8192, ' ') + uri;
        QTest::newRow("all-original-leading-space") << QString{} << padded << padded << true;
    }
    void probeVerifiesExactNativePasteProvenance() {
        QFETCH(QString, previous); QFETCH(QString, current); QFETCH(QString, literal); QFETCH(bool, accepted);
        Fixture f; auto* op = f.model->requestProbe(literal, {{"edited", true}, {"previousText", previous}, {"currentText", current}}, f.owner.get());
        QVERIFY(op); f.finish(); const auto result = op->result();
        QCOMPARE(result.value("pasteProvenance").toBool(), accepted);
        QVERIFY(result.value("isPaymentUri").toBool()); QVERIFY(result.value("containsPaymentUri").toBool());
    }
    void explicitCancellationSuppressesHeldPublicationWithoutClosingSuccessor() {
        Fixture f; auto* old = f.model->requestProbe(Uri("old"), {}, f.owner.get());
        QSignalSpy finished{old, &BitcoinUriParseOperation::finished};
        f.commands.drain(); old->cancel();
        auto* successor = f.model->requestProbe(Uri("new"), {}, f.owner.get()); f.finish();
        QCOMPARE(finished.count(), 0); QVERIFY(!old->pending()); QVERIFY(old->result().isEmpty());
        QCOMPARE(successor->result().value("label").toString(), QStringLiteral("new"));
    }
    void fileUsesNativeLaneAndCannotPublishAheadOfOutcome() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("payment.uri"); QVERIFY(Write(path, Uri().toUtf8()));
        Fixture f; auto* operation = f.model->requestParseFile(QUrl::fromLocalFile(path).toString(), f.owner.get()); QVERIFY(operation);
        QVERIFY(f.commands.step(execution::Lane::Core)); QVERIFY(operation->result().isEmpty());
        operation->retryPublication(); QVERIFY(operation->pending());
        QVERIFY(f.commands.step(execution::Lane::Domain)); QVERIFY(operation->pending());
        f.publish(); QVERIFY(operation->result().value("success").toBool());
    }
    void replacementQuietlyFencesHeldOriginalGuiTurn() {
        Fixture f; QPointer<BitcoinUriParseOperation> first = f.model->requestParse(Uri("original"), f.owner.get()); QVERIFY(first);
        QSignalSpy old_finished{first, &BitcoinUriParseOperation::finished};
        f.commands.drain(); // Hold the original GUI publication.
        auto* second = f.model->requestParse(Uri("successor"), f.owner.get(), first); QVERIFY(second);
        QVERIFY(second->requestId() > first->requestId()); QVERIFY(!first->pending()); QVERIFY(first->result().isEmpty());
        f.finish(); QCOMPARE(old_finished.count(), 0);
        QCOMPARE(second->result().value("label").toString(), QStringLiteral("successor"));
    }
    void replacementCannotCloseAnotherOwnersOriginalOperation() {
        Fixture f; QObject other;
        auto* first = f.model->requestParse(Uri("first owner"), f.owner.get()); QVERIFY(first);
        auto* second = f.model->requestParse(Uri("other owner"), &other, first); QVERIFY(second);
        f.finish(); QVERIFY(!first->pending()); QVERIFY(!second->pending());
        QCOMPARE(first->result().value("label").toString(), QStringLiteral("first owner"));
        QCOMPARE(second->result().value("label").toString(), QStringLiteral("other owner"));
    }
    void ownerDestructionAfterNativeReadRetiresPayloadWithoutGuiPublication() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("payment.uri"); QVERIFY(Write(path, Uri().toUtf8()));
        Fixture f; QPointer<BitcoinUriParseOperation> operation = f.model->requestParseFile(path, f.owner.get()); QVERIFY(operation);
        QVERIFY(f.commands.step(execution::Lane::Core));
        f.owner.reset(); QVERIFY(!operation);
        f.finish(); QCOMPARE(f.commands.scheduler->pending(), size_t{0});
    }
    void quietCloseStopsAdmissionAndSuppressesOriginalPublication() {
        Fixture f; auto* operation = f.model->requestParse(Uri(), f.owner.get()); QVERIFY(operation);
        QSignalSpy finished{operation, &BitcoinUriParseOperation::finished};
        QVERIFY(f.commands.step(execution::Lane::Domain));
        f.model->closeAdmission();
        QVERIFY(!operation->pending()); QVERIFY(operation->result().isEmpty());
        QVERIFY(!f.model->requestParse(Uri(), f.owner.get()));
        f.finish(); QCOMPARE(finished.count(), 0); QCOMPARE(f.commands.scheduler->pending(), size_t{0});
    }
    void quietCloseRetiresUnexecutedAndReadyOperations_data() {
        QTest::addColumn<bool>("ready");
        QTest::newRow("before-native-turn") << false;
        QTest::newRow("after-ready-publication") << true;
    }
    void quietCloseRetiresUnexecutedAndReadyOperations() {
        QFETCH(bool, ready); Fixture f;
        auto* operation = f.model->requestParse(Uri(), f.owner.get()); QVERIFY(operation);
        QSignalSpy finished{operation, &BitcoinUriParseOperation::finished};
        if (ready) { f.finish(); QVERIFY(operation->result().value("success").toBool()); }
        const auto before = finished.count();
        f.model->closeAdmission(); f.finish();
        QCOMPARE(finished.count(), before); QVERIFY(operation->result().isEmpty());
        QVERIFY(!operation->pending()); QCOMPARE(f.commands.scheduler->pending(), size_t{0});
    }
    void quietCloseRetiresLargeInventoryInBoundedPresentationTurns() {
        Fixture f; std::vector<BitcoinUriParseOperation*> operations;
        for (int n{0}; n < 65; ++n) {
            auto* operation = f.model->requestParse(Uri(), f.owner.get()); QVERIFY(operation);
            operations.push_back(operation);
            f.commands.drain();
        }
        f.publish();
        for (const auto* operation : operations) QVERIFY(operation->result().value("success").toBool());
        f.model->closeAdmission();
        // First32 aliases retire synchronously, and the next turn is retained
        // independently. The last Native lease cannot be retired on this turn.
        QVERIFY(!f.publications.empty());
        f.commands.drain(); QVERIFY(f.commands.scheduler->pending() > 0);
        for (const auto* operation : operations) { QVERIFY(!operation->pending()); QVERIFY(operation->result().isEmpty()); }
        f.model.reset(); // Continuations cannot depend on a live model facade.
        f.finish(); QCOMPARE(f.commands.scheduler->pending(), size_t{0});
    }
    void operationInventoryRejectsBeforeRetainingMoreInput() {
        Fixture f; QSignalSpy rejected{f.model.get(), &BitcoinUriModel::requestRejected};
        for (size_t n{0}; n < BitcoinUriModel::MAX_OPERATIONS; ++n) {
            QVERIFY(f.model->requestParse(Uri(), f.owner.get())); f.commands.drain();
        }
        const auto pending = f.commands.scheduler->pending();
        QVERIFY(!f.model->requestParse(Uri(), f.owner.get()));
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(rejected.front().front().value<BitcoinUriModel::Rejection>(), BitcoinUriModel::Rejection::Capacity);
        QCOMPARE(f.commands.scheduler->pending(), pending);
        f.model->closeAdmission(); f.finish(); QCOMPARE(f.commands.scheduler->pending(), size_t{0});
    }
    void retirementPostFailureUsesIndependentFallback() {
        Fixture f;
        for (int n{0}; n < 33; ++n) { QVERIFY(f.model->requestParse(Uri(), f.owner.get())); f.commands.drain(); }
        f.fail_post = true; f.model->closeAdmission(); f.model.reset();
        f.publish();
        QTRY_VERIFY(([&] { f.commands.drain(); return f.commands.scheduler->pending() == 0; })());
    }
    void rejectedTransportIsARealFailureOperation_data() {
        QTest::addColumn<bool>("file"); QTest::newRow("domain-text") << false; QTest::newRow("native-file") << true;
    }
    void rejectedTransportIsARealFailureOperation() {
        QFETCH(bool, file); Fixture f;
        f.commands.rejectLane(file ? execution::Lane::Core : execution::Lane::Domain);
        auto* operation = file ? f.model->requestParseFile("unused.uri", f.owner.get()) : f.model->requestParse(Uri(), f.owner.get());
        QVERIFY(operation); QVERIFY(operation->pending()); QVERIFY(operation->result().isEmpty());
        f.finish(); QVERIFY(!operation->pending()); QVERIFY(!operation->result().value("success").toBool());
        QVERIFY(!operation->result().value("error").toString().isEmpty());
    }
    void cleanupReservationRejectionDoesNotExecuteParser() {
        auto limits = execution::Limits::Bounded(); limits.cleanup_reservations.tasks = 0;
        Fixture f{limits}; auto* operation = f.model->requestParse(Uri(), f.owner.get()); QVERIFY(operation);
        QVERIFY(operation->pending()); f.finish();
        QVERIFY(!operation->result().value("success").toBool());
        QVERIFY(operation->result().value("error").toString().contains("capacity"));
        QCOMPARE(f.commands.scheduler->pending(), size_t{0});
    }
    void missingServicesDoesNotCreateAnExecutorOrReportSuccess() {
        BitcoinUriModel model; QObject owner;
        auto* operation = model.requestParse(Uri(), &owner); QVERIFY(operation); QVERIFY(operation->pending());
        QTRY_VERIFY(!operation->pending());
        QVERIFY(!operation->result().value("success").toBool());
        QVERIFY(operation->result().value("error").toString().contains("unavailable"));
    }
    void failedGuiPostRetainsDomainResultForExplicitRetry() {
        Fixture f; f.fail_post = true;
        auto* operation = f.model->requestParse(Uri(), f.owner.get()); QVERIFY(operation);
        QSignalSpy finished{operation, &BitcoinUriParseOperation::finished};
        f.commands.drain(); QVERIFY(operation->pending()); QVERIFY(operation->result().isEmpty()); QCOMPARE(finished.count(), 0);
        operation->retryPublication(); QVERIFY(!operation->pending()); QVERIFY(operation->result().value("success").toBool());
        QCOMPARE(finished.count(), 1);
    }
    void oversizedTextHasAnExplicitDomainFailure() {
        Fixture f; auto* operation = f.model->requestParse(QString(MAX_BYTES + 1, QLatin1Char{'x'}), f.owner.get()); QVERIFY(operation);
        QVERIFY(operation->pending()); QVERIFY(operation->result().isEmpty()); f.finish();
        QVERIFY(!operation->result().value("success").toBool());
        QVERIFY(operation->result().value("error").toString().contains("too large"));
    }
    void fileByteLimitIsInclusive_data() {
        QTest::addColumn<qint64>("size"); QTest::addColumn<bool>("valid");
        QTest::newRow("exact-cap") << MAX_BYTES << true;
        QTest::newRow("one-byte-over") << MAX_BYTES + 1 << false;
    }
    void fileByteLimitIsInclusive() {
        QFETCH(qint64, size); QFETCH(bool, valid);
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("limit.uri");
        const auto prefix = ("bitcoin:" + Address() + "?label=").toUtf8();
        const QByteArray bytes = prefix + QByteArray(size - prefix.size(), 'x'); QCOMPARE(bytes.size(), size); QVERIFY(Write(path, bytes));
        Fixture f; auto* operation = f.model->requestParseFile(path, f.owner.get()); QVERIFY(operation); f.finish();
        QCOMPARE(operation->result().value("success").toBool(), valid);
        if (!valid) QVERIFY(operation->result().value("error").toString().contains("too large"));
        else QCOMPARE(operation->result().value("label").toString().size(), size - prefix.size());
    }
    void fileGrowingWhileQueuedIsReadAtDispatchAndRejectedAtTheCap() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("growing.uri"); QVERIFY(Write(path, Uri().toUtf8()));
        Fixture f; auto* operation = f.model->requestParseFile(path, f.owner.get()); QVERIFY(operation);
        QVERIFY(Write(path, QByteArray(MAX_BYTES + 1, 'x'))); f.finish();
        QVERIFY(!operation->result().value("success").toBool());
        QVERIFY(operation->result().value("error").toString().contains("too large"));
    }
    void zeroSizeNativeStreamCannotEscapeTheReadCap() {
#ifdef Q_OS_UNIX
        if (!QFileInfo::exists("/dev/zero")) QSKIP("This host has no unreported-size zero stream");
        Fixture f; auto* operation = f.model->requestParseFile("/dev/zero", f.owner.get()); QVERIFY(operation); f.finish();
        QVERIFY(!operation->result().value("success").toBool());
        QVERIFY(operation->result().value("error").toString().contains("too large"));
#else
        QSKIP("The unreported-size stream check uses the Unix /dev/zero device");
#endif
    }
    void realFileReadRunsOutsideGuiOnTheInjectedExecutor() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("worker.uri"); QVERIFY(Write(path, Uri().toUtf8()));
        BackendExecutor executor;
        auto recording = std::make_shared<RecordingScheduler>(executor.scheduler());
        {
            QObject owner; BitcoinUriModel model; model.setServices({recording, {}});
            auto* operation = model.requestParseFile(path, &owner); QVERIFY(operation);
            QTRY_VERIFY(!operation->pending()); QVERIFY(operation->result().value("success").toBool());
            QVERIFY(recording->native_thread.load()); QVERIFY(recording->native_thread.load() != QThread::currentThread());
            model.closeAdmission();
        }
        executor.shutdown(); QTRY_VERIFY(executor.isDrained());
    }
};
BITCOINQML_REGISTER_QT_TEST(BitcoinUriModelTests)
#include <test_bitcoinurimodel.moc>
