// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QApplication>
#include <QEventLoop>
#include <QStyleFactory>

#include <chainparams.h>
#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <qml/qtlogsink.h>
#include <test/qt_test_registry.h>
#include <util/translation.h>
#include <cstdio>
#include <atomic>
#include <thread>
#include <vector>

const TranslateFn G_TRANSLATION_FUN{nullptr};

namespace {
std::shared_ptr<qtlog::FatalPolicy> configured_fatal_policy;
std::shared_ptr<qtlog::Queue> configured_log_queue;
qtlog::Stage configured_log_stage{qtlog::Stage::Startup};
void ConfiguredQtHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{ qtlog::Forward(type, context, message, configured_log_stage, configured_fatal_policy, configured_log_queue); }
}

int main(int argc, char* argv[])
{
    // Transition animations are irrelevant to assertions and make teardown flaky on Qt 6.2.
    qputenv("BITCOIN_QML_TEST_NO_ANIMATIONS", "1");
    if (qEnvironmentVariableIsSet("BITCOIN_QML_QTLOG_SINK_CASE")) {
        const bool overflow = qgetenv("BITCOIN_QML_QTLOG_SINK_CASE") == "overflow";
        qtlog::NativeSink sink{[overflow](const qtlog::Record& record) {
            const auto text = overflow ? qtlog::FormatStderr(record) : QByteArray{"Qt log injected write failure\n"};
            std::fwrite(text.constData(), 1, static_cast<size_t>(text.size()), stderr); std::fflush(stderr);
            return overflow;
        }, {1, sizeof(qtlog::Record) + 16}};
        sink.queue()->enqueue(QtWarningMsg, {}, overflow ? QString(1000, 'x') : QStringLiteral("fail"), qtlog::Stage::Startup);
        sink.finishAfterGui();
        return qtlog::FinalExitStatus(0, sink.queue()->receipt());
    }
    if (qEnvironmentVariableIsSet("BITCOIN_QML_QTLOG_CONFIGURED_CASE")) {
        const auto mode = qgetenv("BITCOIN_QML_QTLOG_CONFIGURED_CASE");
        const bool critical = mode.contains("critical"), concurrent = mode.contains("concurrent");
        configured_log_stage = mode.contains("main") ? qtlog::Stage::Main : qtlog::Stage::Startup;
        qtlog::NativeSink sink{[](const qtlog::Record& record) {
            if (record.stage == qtlog::Stage::Startup) {
                const auto text = qtlog::FormatStderr(record);
                std::fwrite(text.constData(), 1, static_cast<size_t>(text.size()), stderr); std::fflush(stderr);
            }
            return true;
        }};
        configured_log_queue = sink.queue(); configured_fatal_policy = qtlog::CaptureFatalPolicy();
        qInstallMessageHandler(ConfiguredQtHandler);
        const auto emit_message = [&](int thread, int index) {
            const QString message = QStringLiteral("qtlog configured %1 %2:%3 - configured-fatal-tail-").arg(critical ? "critical" : "warning").arg(thread).arg(index) + QString::fromUtf8("\xE2\x82\xAC");
            if (critical) qCritical("%s", message.toUtf8().constData()); else qWarning("%s", message.toUtf8().constData());
        };
        if (concurrent) {
            std::atomic<int> entered{0}; std::atomic_bool go{false}; std::vector<std::thread> threads;
            threads.reserve(4);
            for (int thread = 0; thread < 4; ++thread) threads.emplace_back([&, thread] {
                ++entered; while (!go) std::this_thread::yield();
                for (int index = 0; index < 32; ++index) emit_message(thread, index);
            });
            while (entered != 4) std::this_thread::yield();
            go = true;
            for (auto& thread : threads) thread.join();
        } else for (int index = 0; index < 16; ++index) emit_message(0, index);
        configured_log_queue.reset(); qInstallMessageHandler(nullptr); sink.finishAfterGui();
        return qtlog::FinalExitStatus(0, sink.queue()->receipt());
    }
    if (qEnvironmentVariableIsSet("BITCOIN_QML_QTLOG_FATAL_CASE")) {
        configured_fatal_policy = qtlog::CaptureFatalPolicy(); qInstallMessageHandler(ConfiguredQtHandler);
        qFatal("qtlog fatal: %s", qgetenv("BITCOIN_QML_QTLOG_FATAL_CASE").constData());
    }
    QApplication app(argc, argv);
    // The default macOS style draws through Cocoa views, which the minimal
    // platform the tests run on does not provide, so any shown widget crashes.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QCoreApplication::setOrganizationName("BitcoinCoreGuiQmlTests");
    QCoreApplication::setApplicationName("bitcoinqml_unit_tests");
    SelectParams(ChainType::REGTEST);

    const auto classes = qEnvironmentVariable("BITCOIN_QML_TEST_CLASSES").split(',', Qt::SkipEmptyParts);
    int status = 0;
    bool ran{false};
    for (const auto& test : qttestregistry::SortedEntries()) {
        if (!classes.isEmpty() && !classes.contains(QString::fromUtf8(test.name))) continue;
        ran = true;
        status |= test.run(argc, argv);
    }
    QEventLoop loop;
    bool drained{false};
    BackendExecutor::shutdownAll(&loop, [&] { drained = true; loop.quit(); });
    while (!drained) loop.exec();
    presentation::FlushRetirements();
    return ran ? status : 1;
}
