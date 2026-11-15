// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/fileexportoperation.h>
#include <qml/backendexecutor.h>
#include <qml/psbtwork.h>
#include <chainparams.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/psbtfeefixture.h>
#include <test/qt_test_registry.h>

#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>
#include <deque>

namespace {
struct Fixture {
    ManualRoutedExecution execution;
    std::deque<std::function<void()>> gui_posts;

    explicit Fixture(execution::Limits limits = execution::Limits::Bounded()) : execution{limits} {}

    backup::Services services()
    {
        backup::Services result;
        result.scheduler = execution.scheduler;
        result.post = [this](std::function<void()> work) { gui_posts.push_back(std::move(work)); };
        result.coordinator = std::make_shared<backup::InstallCoordinator>();
        return result;
    }
    void deliverGui()
    {
        while (!gui_posts.empty()) {
            auto work = std::move(gui_posts.front());
            gui_posts.pop_front();
            work();
        }
    }
};
struct DeletionState {
    std::atomic<bool> destroyed{false};
    std::atomic<bool> destroyed_on_gui{false};
};
std::shared_ptr<const PartiallySignedTransaction> TrackedDocument(const std::shared_ptr<DeletionState>& state)
{
    return {new PartiallySignedTransaction{psbtfeefixture::Legacy(2, false)}, [state](const PartiallySignedTransaction* value) {
        state->destroyed_on_gui = QThread::currentThread() == qApp->thread();
        delete value;
        state->destroyed = true;
    }};
}
QByteArray ReadFile(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
}

class FileExportOperationTests : public QObject
{
    Q_OBJECT
    std::unique_ptr<ECC_Context> m_ecc;
private Q_SLOTS:
    void initTestCase()
    {
        SelectParams(ChainType::MAIN);
        m_ecc = std::make_unique<ECC_Context>();
    }

    void exactCapturedPsbtBytesUseApprovedFinalPath()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("captured.psbt"));
        const auto document = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
        const QByteArray expected = psbtwork::SerializeRaw(*document);
        QVERIFY(!expected.isEmpty());
        Fixture fixture;
        FileExportOperationModel operation{path, document, fixture.services()};

        QVERIFY(operation.start());
        QVERIFY(operation.pending());
        QVERIFY(!QFile::exists(path));
        fixture.execution.drain();
        fixture.deliverGui();

        QVERIFY(operation.saved());
        QVERIFY(!operation.pending());
        QCOMPARE(operation.finalPath(), path);
        QCOMPARE(ReadFile(path), expected);
        QVERIFY(operation.errorText().isEmpty());
    }

    void realWorkerExportsCapturedPsbtOffGui()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("worker.psbt"));
        const auto document = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
        const QByteArray expected = psbtwork::SerializeRaw(*document);
        QVERIFY(!expected.isEmpty());

        BackendExecutor executor;
        QObject gui;
        std::atomic<bool> off_gui{true};
        std::atomic<int> outcomes{0};
        std::atomic<int> outcome_phase{-1};
        backup::Services services;
        services.scheduler = executor.scheduler();
        services.post = [&gui](auto publication) {
            if (!QMetaObject::invokeMethod(&gui, std::move(publication), Qt::QueuedConnection)) {
                throw std::runtime_error("GUI publication could not be queued");
            }
        };
        services.available = [&] {
            if (QThread::currentThread() == qApp->thread()) off_gui = false;
            return true;
        };
        services.outcome = [&](backup::Snapshot result) {
            if (QThread::currentThread() == qApp->thread()) off_gui = false;
            outcome_phase = static_cast<int>(result.phase);
            ++outcomes;
        };
        FileExportOperationModel operation{path, document, std::move(services)};

        QVERIFY(operation.start());
        QTRY_VERIFY_WITH_TIMEOUT(operation.saved(), 5'000);
        QCOMPARE(ReadFile(path), expected);
        QCOMPARE(operation.finalPath(), path);
        QVERIFY(operation.errorText().isEmpty());
        QCOMPARE(outcomes.load(), 1);
        QCOMPARE(outcome_phase.load(), static_cast<int>(backup::Phase::Succeeded));
        QVERIFY(off_gui.load());

        executor.shutdown();
        QTRY_VERIFY(executor.isDrained());
    }

    void retiringBeforeInspectionReleasesPsbtThroughCleanup()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("retired.psbt"));
        auto state = std::make_shared<DeletionState>();
        auto document = TrackedDocument(state);
        Fixture fixture;
        FileExportOperationModel operation{path, document, fixture.services()};
        document.reset();

        QVERIFY(operation.start());
        QVERIFY(operation.pending());
        operation.retire();
        QVERIFY(!state->destroyed);

        fixture.execution.drain();
        fixture.deliverGui();
        QVERIFY(!operation.pending());
        QVERIFY(!QFile::exists(path));
        QVERIFY(!state->destroyed);
        QCOMPARE(fixture.execution.queuedCountIf([](const auto& turn) {
            return turn.lane == execution::Lane::Cleanup && turn.phase == execution::Phase::Queued;
        }), 1);
        QVERIFY(fixture.execution.step(execution::Lane::Cleanup));
        QVERIFY(state->destroyed);
        fixture.execution.drain();
        QVERIFY(fixture.execution.quiescent());
    }

    void refusalReleasesPsbtThroughCleanup()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("existing.psbt"));
        const QByteArray original{"keep the existing file"};
        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), qint64(original.size()));
        }
        auto state = std::make_shared<DeletionState>();
        auto document = TrackedDocument(state);
        Fixture fixture;
        FileExportOperationModel operation{path, document, fixture.services()};
        document.reset();

        QVERIFY(operation.start());
        fixture.execution.drain();
        fixture.deliverGui();
        QVERIFY(operation.confirmationRequired());
        QVERIFY(!state->destroyed);
        QVERIFY(operation.refuse());
        fixture.execution.drain();
        fixture.deliverGui();

        QVERIFY(!operation.pending());
        QVERIFY(operation.errorText().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
        QCOMPARE(ReadFile(path), original);
        QVERIFY(!state->destroyed);
        QCOMPARE(fixture.execution.queuedCountIf([](const auto& turn) {
            return turn.lane == execution::Lane::Cleanup && turn.phase == execution::Phase::Queued;
        }), 1);
        QVERIFY(fixture.execution.step(execution::Lane::Cleanup));
        QVERIFY(state->destroyed);
        fixture.execution.drain();
        QVERIFY(fixture.execution.quiescent());
    }

    void rejectedStartAdmissionReleasesPsbtThroughCleanup()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto state = std::make_shared<DeletionState>();
        auto document = TrackedDocument(state);
        Fixture fixture;
        FileExportOperationModel operation{directory.filePath(QStringLiteral("rejected.psbt")), document, fixture.services()};
        document.reset();
        fixture.execution.rejectLane(execution::Lane::Core);

        QVERIFY(!operation.start());
        QVERIFY(!operation.pending());
        QVERIFY(operation.errorText().contains(QStringLiteral("scheduled"), Qt::CaseInsensitive));
        QVERIFY(!state->destroyed);
        QCOMPARE(fixture.execution.queuedCountIf([](const auto& turn) {
            return turn.lane == execution::Lane::Cleanup && turn.phase == execution::Phase::Queued;
        }), 1);
        QVERIFY(fixture.execution.step(execution::Lane::Cleanup));
        QVERIFY(state->destroyed);
        fixture.execution.drain();
        QVERIFY(fixture.execution.quiescent());
    }

    void realWorkerRefusalDestroysCapturedPsbtOffGui()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("existing.psbt"));
        const QByteArray original{"do not replace after refusal"};
        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), qint64(original.size()));
        }
        auto state = std::make_shared<DeletionState>();
        auto document = TrackedDocument(state);
        BackendExecutor executor;
        QObject gui;
        backup::Services services;
        services.scheduler = executor.scheduler();
        services.post = [&gui](auto publication) {
            if (!QMetaObject::invokeMethod(&gui, std::move(publication), Qt::QueuedConnection)) {
                throw std::runtime_error("GUI publication could not be queued");
            }
        };
        services.available = [] { return true; };
        FileExportOperationModel operation{path, document, std::move(services)};
        document.reset();

        QVERIFY(operation.start());
        QTRY_VERIFY_WITH_TIMEOUT(operation.confirmationRequired(), 5'000);
        QVERIFY(!state->destroyed);
        QVERIFY(operation.refuse());
        QTRY_VERIFY_WITH_TIMEOUT(!operation.pending(), 5'000);
        QTRY_VERIFY_WITH_TIMEOUT(state->destroyed.load(), 5'000);

        QVERIFY(!state->destroyed_on_gui.load());
        QCOMPARE(ReadFile(path), original);
        executor.shutdown();
        QTRY_VERIFY(executor.isDrained());
    }

    void overwriteNeedsApprovalAndRechecksDestinationVersion()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("existing.psbt"));
        const QByteArray original{"previous reviewed export"};
        const QByteArray intervening{"concurrent replacement"};
        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), qint64(original.size()));
        }
        const auto document = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
        Fixture fixture;
        FileExportOperationModel operation{path, document, fixture.services()};

        QVERIFY(operation.start());
        fixture.execution.drain();
        fixture.deliverGui();
        QVERIFY(operation.pending());
        QVERIFY(operation.confirmationRequired());
        QVERIFY(operation.replacing());
        QCOMPARE(ReadFile(path), original);

        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(file.write(intervening), qint64(intervening.size()));
        }
        QVERIFY(operation.approve());
        fixture.execution.drain();
        fixture.deliverGui();

        QVERIFY(!operation.pending());
        QVERIFY(!operation.saved());
        QVERIFY(operation.errorText().contains(QStringLiteral("changed"), Qt::CaseInsensitive));
        QCOMPARE(ReadFile(path), intervening);
    }

    void refusalDoesNotReplaceExistingFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("existing.psbt"));
        const QByteArray original{"do not overwrite"};
        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), qint64(original.size()));
        }
        const auto document = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
        Fixture fixture;
        FileExportOperationModel operation{path, document, fixture.services()};

        QVERIFY(operation.start());
        fixture.execution.drain();
        fixture.deliverGui();
        QVERIFY(operation.confirmationRequired());
        QVERIFY(operation.refuse());
        fixture.execution.drain();
        fixture.deliverGui();

        QVERIFY(!operation.pending());
        QVERIFY(!operation.saved());
        QVERIFY(operation.errorText().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
        QCOMPARE(ReadFile(path), original);
    }
};

BITCOINQML_REGISTER_QT_TEST(FileExportOperationTests)

#include <test/test_fileexportoperation.moc>
