// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/startupdiagnosticsmodel.h>
#include <qml/backendexecutor.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QtTest/QtTest>
#include <atomic>

namespace {
struct Fixture {
    ManualRoutedExecution commands;
    std::deque<std::function<void()>> publications;
    std::unique_ptr<StartupDiagnosticsModel> model;
    Fixture() : model(std::make_unique<StartupDiagnosticsModel>(execution::SnapshotServices{
        commands.scheduler, [this](auto work) { publications.push_back(std::move(work)); }})) {}
    ~Fixture() { model.reset(); commands.drain(); }
    void publish() { while (!publications.empty()) { auto work = std::move(publications.front()); publications.pop_front(); work(); } }
    void finish() { commands.drain(); publish(); commands.drain(); }
};
QString Complete(const QAbstractItemModel& model) {
    QString text; for (int row = 0; row < model.rowCount(); ++row) text += model.data(model.index(row, 0), Qt::UserRole + 1).toString();
    return text;
}
class RecordingScheduler final : public execution::Scheduler {
    struct Task final : execution::Task {
        std::shared_ptr<execution::Task> inner;
        RecordingScheduler& recorder;
        Task(std::shared_ptr<execution::Task> value, RecordingScheduler& owner) : inner(std::move(value)), recorder(owner) {}
        void execute() override {
            recorder.thread = QThread::currentThread(); recorder.entered.release(); recorder.release.acquire(); inner->execute();
        }
        void deliver() override { inner->deliver(); }
        void notStarted(execution::NotStarted::Cause cause) override { inner->notStarted(cause); }
    };
    std::shared_ptr<execution::Scheduler> m_inner;
public:
    std::atomic<QThread*> thread{nullptr}; QSemaphore entered, release;
    explicit RecordingScheduler(std::shared_ptr<execution::Scheduler> inner) : m_inner(std::move(inner)) {}
    execution::Admission submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override {
        if (spec.lane == execution::Lane::Domain) task = std::make_shared<Task>(std::move(task), *this);
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
}
class StartupDiagnosticsModelTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void nativeOutcomeAndGuiPublicationAreSeparate() {
        Fixture f;
        QVERIFY(f.model->append(QStringList{" first ", "first", "second"}, true));
        QVERIFY(f.model->project(true)); QSignalSpy finished{f.model.get(), &StartupDiagnosticsModel::finished};
        QVERIFY(f.model->busy()); QCOMPARE(f.model->rowCount(), 0);
        QVERIFY(f.commands.step(execution::Lane::Domain)); f.model->retryPublication();
        QVERIFY(f.model->busy()); QCOMPARE(finished.count(), 0);
        QVERIFY(f.commands.step(execution::Lane::Domain)); QVERIFY(f.model->busy()); QCOMPARE(f.model->rowCount(), 0);
        f.publish(); QVERIFY(!f.model->busy()); QCOMPARE(finished.count(), 1);
        QCOMPARE(f.model->summary(), QString{"first\n\nsecond"}); QCOMPARE(Complete(*f.model), f.model->summary());
        QVERIFY(!f.model->append("late startup warning", true));
    }
    void completeLargeMessageSurvivesBoundedSummary() {
        Fixture f;
        const QString original = QStringLiteral("  leading\n") + QString(300'000, QChar{'x'}) + QStringLiteral("\nfinal diagnostic tail  ");
        QVERIFY(f.model->append(original, false));
        QVERIFY(f.model->project(false, {}, {}, {}, false)); f.finish();
        QVERIFY(f.model->summary().size() < 16500); QVERIFY(f.model->rowCount() > 250);
        for (int row = 0; row < f.model->rowCount(); ++row) QVERIFY(f.model->data(f.model->index(row, 0), Qt::UserRole + 1).toString().size() <= 1024);
        QCOMPARE(Complete(*f.model), original);
    }
    void earlyCollectionsKeepOrderAndFailureOwnership() {
        Fixture f; auto early = startupdiagnostics::Create(f.commands.scheduler); QVERIFY(early);
        QVERIFY(early->append(" early warning ", true)); QVERIFY(early->append("early warning", true));
        early->seal();
        QVERIFY(f.model->adopt(early)); QVERIFY(f.model->append(" later warning ", true));
        QVERIFY(f.model->append(" explicit failure ", false));
        QVERIFY(f.model->project(false, "fallback", "Startup warnings:")); f.finish();
        QCOMPARE(Complete(*f.model), QString{"Startup warnings:\nearly warning\n\nlater warning\n\nexplicit failure"});
        QVERIFY(!early->append("arrived after seal", true)); early.reset();
    }
    void heldOldPublicationCannotOverwriteNewFatal() {
        Fixture f; QVERIFY(f.model->append("old", true)); QVERIFY(f.model->project(true)); f.commands.drain();
        QVERIFY(f.model->resetInput()); QVERIFY(f.model->append("new fatal detail", false));
        QVERIFY(f.model->project(false, {}, {}, "Fatal error"));
        f.publish(); QVERIFY(f.model->busy()); QCOMPARE(f.model->rowCount(), 0);
        f.finish(); QCOMPARE(Complete(*f.model), QString{"Fatal error\n\nnew fatal detail"});
    }
    void closeAndDispatchRejectionAreExplicit() {
        Fixture f; QVERIFY(f.model->append("important warning", true)); f.commands.rejectLane(execution::Lane::Domain);
        QVERIFY(!f.model->project(true)); QVERIFY(!f.model->busy()); QVERIFY(!f.model->error().isEmpty()); f.finish();
        QVERIFY(f.model->resetInput()); QVERIFY(f.model->append("closed", true)); QVERIFY(f.model->project(true));
        f.model->closeAdmission(); f.finish(); QVERIFY(f.model->summary().isEmpty());
        QVERIFY(!f.model->append("late", false));
    }
    void observerDestructionAndResetSignalsCannotPublishAfterClose() {
        Fixture f; QVERIFY(f.model->append(QString(200'000, QChar{'z'}), false)); QVERIFY(f.model->project(false));
        f.commands.drain(); f.model.reset(); f.publish(); f.commands.drain(); QVERIFY(f.commands.quiescent());
        f.model = std::make_unique<StartupDiagnosticsModel>(execution::SnapshotServices{f.commands.scheduler, [&f](auto work) { f.publications.push_back(std::move(work)); }});
        QVERIFY(f.model->append("reset destroys observer", false)); QVERIFY(f.model->project(false));
        connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, [&f] { f.model.reset(); });
        f.finish(); QVERIFY(!f.model); QVERIFY(f.commands.quiescent());
    }
    void refusalRetainsOriginalForRetry() {
        Fixture f; const QString original = "retained original diagnostic";
        QVERIFY(f.model->append(original, false)); f.commands.rejectLane(execution::Lane::Domain);
        QVERIFY(!f.model->project(false)); QVERIFY(f.model->project(false)); f.finish();
        QCOMPARE(Complete(*f.model), original);
    }
    void intakeBudgetsRefuseWholeBatchWithoutTruncatingAcceptedData() {
        Fixture f;
        const QString accepted(startupdiagnostics::Collection::MAX_CHARS, QChar{'a'});
        QVERIFY(f.model->append(accepted, false)); QVERIFY(!f.model->append("over capacity", false));
        QVERIFY(f.model->project(false)); f.finish(); QVERIFY(Complete(*f.model).endsWith(accepted)); QVERIFY(!f.model->error().isEmpty());
        QVERIFY(f.model->resetInput());
        QStringList too_many; for (size_t i = 0; i <= startupdiagnostics::Collection::MAX_BATCH_MESSAGES; ++i) too_many.push_back("bounded");
        QVERIFY(!f.model->append(too_many, false)); QVERIFY(f.model->append("accepted later", false));
        QVERIFY(f.model->project(false)); f.finish(); QVERIFY(Complete(*f.model).endsWith("accepted later")); QVERIFY(!f.model->error().isEmpty());
    }
    void rawTextBudgetIsReleasedOnlyByNativeCleanup() {
        Fixture f; auto budget = std::make_shared<startupdiagnostics::TextBudget>();
        auto text = startupdiagnostics::RetainText(f.commands.scheduler, budget,
            QString(startupdiagnostics::TextBudget::MAX_CHARS, QChar{'r'}));
        QVERIFY(text); QCOMPARE(budget->used.load(), startupdiagnostics::TextBudget::MAX_CHARS);
        QVERIFY(!startupdiagnostics::RetainText(f.commands.scheduler, budget, "refused"));
        text.reset(); QCOMPARE(budget->used.load(), startupdiagnostics::TextBudget::MAX_CHARS);
        f.commands.drain(); QCOMPARE(budget->used.load(), uint64_t{0});
    }
    void reentrantSuccessorAtEveryPublicationPhaseKeepsItsOwnOutcome_data() {
        QTest::addColumn<int>("phase");
        QTest::newRow("before-reset") << 0; QTest::newRow("after-reset") << 1; QTest::newRow("changed") << 2;
    }
    void reentrantSuccessorAtEveryPublicationPhaseKeepsItsOwnOutcome() {
        QFETCH(int, phase); Fixture f;
        QVERIFY(f.model->append("old result", false)); QVERIFY(f.model->project(false)); f.commands.drain();
        bool replaced{false}; bool replacement_ok{true}; int finished{0};
        const auto replace = [&] {
            if (replaced) return;
            replaced = true;
            replacement_ok = f.model->resetInput() && f.model->append("successor result", false) && f.model->project(false);
        };
        if (phase == 0) connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, replace);
        if (phase == 1) connect(f.model.get(), &QAbstractItemModel::modelReset, this, replace);
        if (phase == 2) connect(f.model.get(), &StartupDiagnosticsModel::changed, this, replace);
        connect(f.model.get(), &StartupDiagnosticsModel::finished, this, [&] { ++finished; });
        f.publish(); QVERIFY(replaced); QVERIFY(replacement_ok); QCOMPARE(finished, 0); QVERIFY(f.model->busy());
        f.finish(); QCOMPARE(finished, 1); QCOMPARE(Complete(*f.model), QString{"successor result"});
    }
    void actualDiagnosticLossFailsTheFinalReceipt() {
        QCOMPARE(startupdiagnostics::FinalExitStatus(EXIT_SUCCESS, false), EXIT_SUCCESS);
        QCOMPARE(startupdiagnostics::FinalExitStatus(EXIT_SUCCESS, true), EXIT_FAILURE);
        QCOMPARE(startupdiagnostics::FinalExitStatus(17, true), 17);
        // Ordinary explicit intent refusal is not itself a diagnostic-loss receipt.
        Fixture f; f.model->closeAdmission(); QVERIFY(!f.model->append("not admitted", false));
        QCOMPARE(startupdiagnostics::FinalExitStatus(EXIT_SUCCESS, false), EXIT_SUCCESS);
    }
    void realDomainThreadPreservesGuiHeartbeatAndExactTail() {
        BackendExecutor backend{execution::Limits::Bounded()};
        auto scheduler = std::make_shared<RecordingScheduler>(backend.scheduler());
        StartupDiagnosticsModel model{{scheduler, {}}};
        const QString text = QString(400'000, QChar{'q'}) + "actual final error";
        QVERIFY(model.append(text, false)); QVERIFY(model.project(false));
        const auto unblock = qScopeGuard([scheduler] { scheduler->release.release(8); });
        QTRY_VERIFY_WITH_TIMEOUT(scheduler->entered.available() > 0, 5000);
        QVERIFY(scheduler->thread.load() != QThread::currentThread());
        bool heartbeat{false}; QTimer::singleShot(0, this, [&] { heartbeat = true; });
        QTRY_VERIFY_WITH_TIMEOUT(heartbeat, 5000); QVERIFY(model.busy());
        scheduler->release.release(8);
        QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000); QCOMPARE(Complete(model), text);
        model.closeAdmission(); backend.shutdown(); QTRY_VERIFY_WITH_TIMEOUT(backend.isDrained(), 5000);
    }
};
BITCOINQML_REGISTER_QT_TEST(StartupDiagnosticsModelTests)
#include <test_startupdiagnosticsmodel.moc>
