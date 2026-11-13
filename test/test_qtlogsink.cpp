// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/qtlogsink.h>
#include <qml/guipresentation.h>
#include <test/qt_test_registry.h>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSemaphore>
#include <QTimer>
#include <QtTest/QtTest>
#include <atomic>
#include <stdexcept>
#include <vector>

class QtLogSinkTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void manualWakeOwnsNativeOrderStageAndBoundedTurns() {
        int wakes{0}; std::vector<qtlog::Record> writes;
        qtlog::Queue queue{[&] { ++wakes; }, [&](const auto& record) { writes.push_back(record); return true; }, {32, 128 * 1024}};
        const QMessageLogContext context{"source.cpp", 12, "function", "category"};
        for (int i = 0; i < 20; ++i) QVERIFY(queue.enqueue(QtWarningMsg, context, QString::number(i), i < 10 ? qtlog::Stage::Startup : qtlog::Stage::Main));
        QCOMPARE(wakes, 1); QVERIFY(writes.empty()); queue.close(); QVERIFY(!queue.receipt().closed);
        queue.drain(); QCOMPARE(writes.size(), size_t{16}); QCOMPARE(wakes, 2); QVERIFY(!queue.receipt().closed);
        queue.drain(); QCOMPARE(writes.size(), size_t{20}); QVERIFY(queue.receipt().closed);
        for (int i = 0; i < 20; ++i) { QCOMPARE(writes[i].message, QString::number(i)); QCOMPARE(writes[i].stage, i < 10 ? qtlog::Stage::Startup : qtlog::Stage::Main); }
        QCOMPARE(queue.receipt().accepted, quint64{20}); QCOMPARE(queue.receipt().failed, quint64{0});
    }
    void fullAndOversizedAdmissionAlwaysProduceReservedOverflowDiagnostic() {
        std::vector<qtlog::Record> writes; qtlog::Queue queue{[] {}, [&](const auto& record) { writes.push_back(record); return true; }, {1, 4096}};
        const QMessageLogContext context{};
        QVERIFY(queue.enqueue(QtInfoMsg, context, "first", qtlog::Stage::Startup));
        QVERIFY(!queue.enqueue(QtInfoMsg, context, "second", qtlog::Stage::Main));
        QVERIFY(!queue.enqueue(QtWarningMsg, context, QString(8192, 'x'), qtlog::Stage::Main));
        queue.close(); queue.drain(); QCOMPARE(writes.size(), size_t{2}); QCOMPARE(writes[0].message, QStringLiteral("first"));
        QVERIFY(writes[1].overflow); QCOMPARE(writes[1].rejected, quint64{2}); QVERIFY(writes[1].rejected_bytes >= 16384);
        QCOMPARE(queue.receipt().accepted, quint64{1}); QCOMPARE(queue.receipt().rejected, quint64{2}); QVERIFY(queue.receipt().closed);
    }
    void closedAdmissionIsARealLossInFinalExitStatus() {
        qtlog::Queue queue{[] {}, [](const auto&) { return true; }, {4, 32768}};
        queue.close(); queue.drain(); QVERIFY(queue.receipt().closed);
        QVERIFY(!queue.enqueue(QtWarningMsg, {}, "late", qtlog::Stage::Main));
        QCOMPARE(queue.receipt().rejected, quint64{1}); QCOMPARE(qtlog::FinalExitStatus(0, queue.receipt()), 1);
        QCOMPARE(qtlog::FinalExitStatus(7, queue.receipt()), 7);
    }
    void fatalCounterParsingMatchesSupportedRuntimeVersions_data() {
        QTest::addColumn<QByteArray>("value"); QTest::addColumn<QString>("version"); QTest::addColumn<int>("expected");
        QTest::newRow("empty62") << QByteArray{} << QString{"6.2.4"} << 0;
        QTest::newRow("zero610") << QByteArray{"0"} << QString{"6.10.2"} << 0;
        QTest::newRow("text62") << QByteArray{"enabled"} << QString{"6.2.4"} << 1;
        QTest::newRow("text610") << QByteArray{"enabled"} << QString{"6.10.2"} << 1;
        QTest::newRow("hex62") << QByteArray{"0x2"} << QString{"6.2.4"} << 2;
        QTest::newRow("hex610") << QByteArray{"0x2"} << QString{"6.10.2"} << 2;
        QTest::newRow("octal") << QByteArray{"010"} << QString{"6.10.2"} << 8;
        QTest::newRow("negative62") << QByteArray{"-1"} << QString{"6.2.4"} << -1;
        QTest::newRow("negative69") << QByteArray{"-1"} << QString{"6.9.0"} << -1;
        QTest::newRow("negative610") << QByteArray{"-1"} << QString{"6.10.2"} << 1;
        QTest::newRow("long-leading-whitespace") << QByteArray(8192, ' ') + "2" << QString{"6.10.2"} << 2;
    }
    void fatalCounterParsingMatchesSupportedRuntimeVersions() {
        QFETCH(QByteArray, value); QFETCH(QString, version); QFETCH(int, expected);
        QCOMPARE(qtlog::FatalPolicy::parseCounter(value, QVersionNumber::fromString(version)), expected);
    }
    void warningAndCriticalCountersHaveIndependentOriginalOwnership() {
        const auto fatal = [](auto& policy, auto type) { return policy.enter(type).terminating; };
        qtlog::FatalPolicy policy{2, 2};
        QVERIFY(!fatal(policy, QtCriticalMsg)); QVERIFY(!fatal(policy, QtWarningMsg)); QVERIFY(!fatal(policy, QtDebugMsg));
        QVERIFY(fatal(policy, QtCriticalMsg)); QVERIFY(policy.enter(QtWarningMsg).already_fatal);
        qtlog::FatalPolicy disabled{-1, 0}; for (int i = 0; i < 20; ++i) { QVERIFY(!fatal(disabled, QtWarningMsg)); QVERIFY(!fatal(disabled, QtCriticalMsg)); }
    }
    void contextCaptureIsBoundedAndIndependentOfEmitterBuffers() {
        qtlog::Record written; qtlog::Queue queue{[] {}, [&](const auto& value) { written = value; return true; }, {4, 32768}};
        QByteArray file(8192, 'a'), function{"function"}, category{"category"};
        const QMessageLogContext context{file.constData(), 7, function.constData(), category.constData()};
        QVERIFY(queue.enqueue(QtWarningMsg, context, "message", qtlog::Stage::Startup));
        file.fill('b'); function.fill('b'); category.fill('b'); queue.close(); queue.drain();
        QVERIFY(written.context_truncated); QCOMPARE(QByteArray(written.file.data()), QByteArray(255, 'a'));
        QCOMPARE(QByteArray(written.function.data()), QByteArray{"function"}); QCOMPARE(written.line, 7);
    }
    void nativeWriteFailureIsOwnedByFinalReceipt() {
        qtlog::Queue queue{[] {}, [](const auto&) -> bool { throw std::runtime_error("write rejected"); }, {4, 32768}};
        QVERIFY(queue.enqueue(QtInfoMsg, {}, "message", qtlog::Stage::Main)); queue.close(); queue.drain();
        QCOMPARE(queue.receipt().written, quint64{0}); QCOMPARE(queue.receipt().failed, quint64{1}); QVERIFY(queue.receipt().closed);
    }
    void blockedRealWriterKeepsGuiResponsiveAndCloseAcknowledgesFinalFlush() {
        QSemaphore release; std::atomic_bool entered{false}; std::atomic<QThread*> writer{nullptr};
        std::vector<QString> writes; bool finished{false}; qtlog::Receipt receipt;
        qtlog::NativeSink sink{[&](const auto& record) {
            writer = QThread::currentThread(); entered = true; release.acquire(); writes.push_back(record.message); return true;
        }};
        QVERIFY(sink.queue()->enqueue(QtWarningMsg, {}, "before cleanup", qtlog::Stage::Startup));
        QVERIFY(sink.queue()->enqueue(QtWarningMsg, {}, "after cleanup", qtlog::Stage::Main));
        int heartbeats{0}; QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] { if (entered && !finished) { ++heartbeats; release.release(); } });
        heartbeat.start(1);
        sink.closeAsync(presentation::QueuedPost(), [&](auto result) { receipt = result; finished = true; });
        QVERIFY(!finished); QTRY_VERIFY(finished); heartbeat.stop();
        QVERIFY(heartbeats > 0); QVERIFY(writer.load() != nullptr && writer.load() != QThread::currentThread());
        QCOMPARE(writes, std::vector<QString>({"before cleanup", "after cleanup"}));
        QCOMPARE(receipt.accepted, quint64{2}); QCOMPARE(receipt.written, quint64{2}); QVERIFY(receipt.closed);
    }
    void diagnosticLossAffectsFinalProcessExit_data() {
        QTest::addColumn<QString>("mode"); QTest::addColumn<QByteArray>("diagnostic");
        QTest::newRow("overflow") << QStringLiteral("overflow") << QByteArray{"Qt log queue overflow: 1 messages (2000 UTF-16 bytes) were rejected.\n"};
        QTest::newRow("write-error") << QStringLiteral("write-error") << QByteArray{"Qt log injected write failure\n"};
    }
    void diagnosticLossAffectsFinalProcessExit() {
        QFETCH(QString, mode); QFETCH(QByteArray, diagnostic);
        QProcess process; auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("BITCOIN_QML_QTLOG_SINK_CASE", mode); environment.insert("QT_MESSAGE_PATTERN", "%{message}");
        process.setProcessEnvironment(environment); process.start(QCoreApplication::applicationFilePath(), {});
        QTRY_VERIFY_WITH_TIMEOUT(process.state() == QProcess::NotRunning, 10000);
        QCOMPARE(process.exitStatus(), QProcess::NormalExit); QCOMPARE(process.exitCode(), 1);
        QCOMPARE(process.readAllStandardError(), diagnostic);
    }
    void configuredFatalMessagesKeepImmediateProcessDiagnostics_data() {
        QTest::addColumn<QString>("mode"); QTest::addColumn<QByteArray>("counter");
        for (const auto& severity : {QString{"warning"}, QString{"critical"}}) for (const auto& stage : {QString{"startup"}, QString{"main"}}) {
            const QString prefix = severity + "-" + stage;
            QTest::newRow(qPrintable(prefix + "-first")) << prefix << QByteArray{"1"};
            QTest::newRow(qPrintable(prefix + "-hex-second")) << prefix << QByteArray{"0x2"};
            QTest::newRow(qPrintable(prefix + "-nonnumeric")) << prefix << QByteArray{"enabled"};
            QTest::newRow(qPrintable(prefix + "-concurrent")) << prefix + "-concurrent" << QByteArray{"8"};
        }
    }
    void configuredFatalMessagesKeepImmediateProcessDiagnostics() {
        QFETCH(QString, mode); QFETCH(QByteArray, counter);
        QProcess process; auto environment = QProcessEnvironment::systemEnvironment();
        const bool critical = mode.startsWith("critical");
        environment.insert("BITCOIN_QML_QTLOG_CONFIGURED_CASE", mode);
        environment.insert("QT_FATAL_WARNINGS", critical ? "0" : QString::fromLatin1(counter));
        environment.insert("QT_FATAL_CRITICALS", critical ? QString::fromLatin1(counter) : "0");
        environment.insert("QT_MESSAGE_PATTERN", "%{type}: %{message}");
        process.setProcessEnvironment(environment); process.start(QCoreApplication::applicationFilePath(), {});
        QTRY_VERIFY_WITH_TIMEOUT(process.state() == QProcess::NotRunning, 10000);
        QCOMPARE(process.exitStatus(), QProcess::CrashExit); QVERIFY(process.exitCode() != 0);
        const auto error = process.readAllStandardError();
        QVERIFY(error.contains(critical ? "critical: qtlog configured critical" : "warning: qtlog configured warning"));
        QVERIFY(error.contains(QString::fromUtf8("configured-fatal-tail-\xE2\x82\xAC\n").toUtf8()));
    }
    void fatalEmergencyPreservesFullStderrAndImmediateTermination() {
        QProcess process; auto environment = QProcessEnvironment::systemEnvironment();
        const QString payload = QString(8192, 'x') + QString::fromUtf8("-fatal-tail-\xE2\x82\xAC");
        environment.insert("BITCOIN_QML_QTLOG_FATAL_CASE", payload); environment.insert("QT_MESSAGE_PATTERN", "%{message}");
        process.setProcessEnvironment(environment); process.start(QCoreApplication::applicationFilePath(), {"-platform", "minimal"});
        QTRY_VERIFY_WITH_TIMEOUT(process.state() == QProcess::NotRunning, 10000);
        QCOMPARE(process.exitStatus(), QProcess::CrashExit); QVERIFY(process.exitCode() != 0);
        QCOMPARE(process.readAllStandardError(), (QStringLiteral("qtlog fatal: ") + payload + '\n').toUtf8());
    }
};
BITCOINQML_REGISTER_QT_TEST(QtLogSinkTests)
#include <test_qtlogsink.moc>
