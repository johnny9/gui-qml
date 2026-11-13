// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// RPC work and delivery use separate executor and GUI turns. Tests cover both
// manual transport ordering and the actual independently routed worker lanes.

#include <coins.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <qml/models/rpcconsolemodel.h>
#include <test/mocks/stubnode.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/admissionhookscheduler.h>
#include <QTextDocument>
#include <QScopeGuard>
#include <univalue.h>
#include <util/result.h>
#include <util/translation.h>

#include <QMutex>
#include <QSemaphore>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

using RpcTestStubNode = StubNode;

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
// RpcTestStubNode subclass that returns a large result from executeRpc, used to test
// the output-truncation logic in RpcConsoleWorker::execute().
class LargeResultNode : public RpcTestStubNode
{
public:
    UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override
    {
        // Return a 100,000-character string — well above the 50,000-char cap.
        return UniValue(std::string(100'000, 'x'));
    }
};

// RpcTestStubNode that returns a JSON object with a string value containing ": ",
// used to exercise the JSON reply formatter's structural key detection.
class JsonReplyNode : public RpcTestStubNode
{
public:
    UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override
    {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("key1", "value1");
        obj.pushKV("note", "see section:");
        obj.pushKV("key3", "value3");
        return obj;
    }
};

class StructuredReplyNode : public RpcTestStubNode
{
public:
    explicit StructuredReplyNode(UniValue reply) : m_reply(std::move(reply)) {}
    UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override { return m_reply; }

private:
    UniValue m_reply;
};

// RpcTestStubNode that provides a known command list for autocomplete tests.
class CommandListNode : public RpcTestStubNode
{
public:
    std::vector<std::string> listRpcCommands() override
    {
        return {"getblockcount", "getblock", "help"};
    }
};

// RpcTestStubNode whose executeRpc() blocks the worker thread until the test
// releases it, so the model stays in the executing state deterministically.
// A timeout backstops the wait so a forgotten release() can never hang the suite.
class BlockingNode : public RpcTestStubNode
{
public:
    UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override
    {
        m_gate.tryAcquire(1, 5000);
        return {};
    }
    void release() { m_gate.release(); }

private:
    QSemaphore m_gate{0};
};

// RpcTestStubNode whose long-running command blocks the worker thread until
// "stop" is executed. "stop" records that it ran and releases the blocked
// command, modelling shutdown aborting an in-flight RPC. Stop must use an
// independent background lane, without occupying the GUI or blocked RPC lane.
class StopAbortNode : public RpcTestStubNode
{
public:
    UniValue executeRpc(const std::string& method, const UniValue&, const std::string&) override
    {
        if (method == "stop") {
            m_stop_on_gui = QThread::currentThread() == QCoreApplication::instance()->thread();
            m_stop_executed = true;
            m_gate.release();
            return {};
        }
        m_gate.tryAcquire(1, 5000);
        return {};
    }
    bool stopExecuted() const { return m_stop_executed; }
    bool stopOnGui() const { return m_stop_on_gui; }

private:
    QSemaphore m_gate{0};
    std::atomic<bool> m_stop_executed{false};
    std::atomic<bool> m_stop_on_gui{false};
};

// RpcTestStubNode that records the URI executeRpc() is invoked with, so tests can
// assert that a wallet name is translated into a /wallet/<name> endpoint.
class UriCapturingNode : public RpcTestStubNode
{
public:
    UniValue executeRpc(const std::string&, const UniValue&, const std::string& uri) override
    {
        QMutexLocker lock(&m_mutex);
        m_last_uri = QString::fromStdString(uri);
        return {};
    }
    QString lastUri()
    {
        QMutexLocker lock(&m_mutex);
        return m_last_uri;
    }

private:
    QMutex m_mutex;
    QString m_last_uri;
};

// submitCommand() now refuses a new command while one is in flight (matching Qt
// Widgets), so the asynchronous worker round-trip must complete before the next
// submit. The stub node's executeRpc() returns instantly, so this settles on the
// first event-loop pass; a rejected/idle command leaves executing() false and the
// wait returns immediately.
static void submitAndSettle(RpcConsoleModel& model, const QString& command)
{
    model.submitCommand(command);
    QTRY_VERIFY(!model.executing());
}

static int roleForName(QAbstractItemModel* model, const QByteArray& name)
{
    const auto roles = model->roleNames();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        if (it.value() == name) return it.key();
    }
    return -1;
}

// Reconstruct the formatted reply for the structural HTML oracle. Physical
// line records introduce <br>; continuation records retain the same line.
static QString replyHtml(QAbstractItemModel* output)
{
    QString result;
    bool first{true};
    for (int i = 0; i < output->rowCount(); ++i) {
        const auto index = output->index(i, 0);
        if (output->data(index, RpcOutputListModel::CategoryRole).toInt() != RpcConsoleModel::CMD_REPLY) continue;
        if (!first && !output->data(index, RpcOutputListModel::ContinuationRole).toBool()) result += QStringLiteral("<br>");
        first = false;
        result += output->data(index, RpcOutputListModel::ContentRole).toString();
    }
    return result;
}

class RpcConsoleModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void constructorReservationKeepsOriginalParentLifetime();
    void cleanup() {
        bool done{false}; BackendExecutor::shutdownAll(this, [&] { done = true; });
        QTRY_VERIFY(done);
    }
    void manualExecutionSeparatesEffectOutcomeAndPublication();
    void acceptedCommandSurvivesObserverLoss();
    void sharedSchedulerClosureWaitsForActualNativeRetirement();
    void aggregateOutputAndRowsStayBounded();
    void historyNavigationOldestToNewest();
    void historyNavigationRestoresPendingText();
    void historyDeduplicatesConsecutiveDuplicates();
    void historyTruncatesAtMax();
    void resetHistoryNavigationClearsState();
    void submitRefusedWhileExecuting();
    void stopRunsIndependentlyWhileExecuting();
    void walletNameScopesRpcToWalletUri();
    void outputRowsExposeCategoryAndRawTimestamp();
    void clearRemovesOutput();
    void outputTruncatedWhenResultTooLong();
    void jsonReplyKeyColoringSkipsStringsContainingColons();
    void jsonReplyPreservesNestedFormatting_data();
    void jsonReplyPreservesNestedFormatting();
    void jsonReplyTraversal_data();
    void jsonReplyTraversal();
    void deeplyNestedJsonReply();
    void availableCommandsIncludesHelpVariants();
    void autocompleteRejectsStaleManualResults();
    void secretRequestsAreNotRetained_data();
    void secretRequestsAreNotRetained();
};

void RpcConsoleModelTests::constructorReservationKeepsOriginalParentLifetime()
{
    struct Node final : RpcTestStubNode {
        int effects{0}, metadata{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override { ++effects; return UniValue{7}; }
        std::vector<std::string> listRpcCommands() override { ++metadata; return {"help"}; }
    } node;
    ManualRoutedExecution env;
    auto scheduler = std::make_shared<AdmissionHookScheduler>(env.scheduler);
    std::deque<std::function<void()>> publications;
    auto parent = std::make_unique<QObject>();
    const QPointer<QObject> original_parent{parent.get()};
    std::unique_ptr<RpcConsoleModel> model;
    int children_at_admission{-1};
    const auto settle = [&] {
        for (int turn{0}; turn < 128 && (turn == 0 || !env.queuedIdle() || !publications.empty()); ++turn) {
            env.drain();
            while (!publications.empty()) { auto callback = std::move(publications.front()); publications.pop_front(); callback(); }
        }
    };
    const auto cleanup = qScopeGuard([&] { scheduler->disarm(); model.reset(); settle(); });
    auto trigger = scheduler->armCleanup(1, [&] {
        children_at_admission = parent->children().size(); parent.reset();
    });
    bool original_parent_lost{false};
    try {
        model = std::make_unique<RpcConsoleModel>(node,
            execution::SnapshotServices{scheduler, [&](auto callback) { publications.push_back(std::move(callback)); }}, parent.get());
    } catch (const std::runtime_error&) { original_parent_lost = true; }
    QVERIFY(original_parent_lost);
    QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
    QCOMPARE(children_at_admission, 0); QVERIFY(!original_parent); QVERIFY(!model);
    QCOMPARE(node.metadata, 0); QCOMPARE(node.effects, 0); QVERIFY(env.scheduler->pending() != 0);
    QCOMPARE(env.queuedCountIf([&](const auto& turn) {
        return turn.id.value == trigger->originalId.load() && turn.lane == execution::Lane::Cleanup && turn.phase == execution::Phase::Queued;
    }), 1);
    const auto actual_cleanup = env.dispatchNext(execution::Lane::Cleanup); QVERIFY(actual_cleanup);
    QCOMPARE(actual_cleanup->value, trigger->originalId.load()); QVERIFY(env.finishWork(*actual_cleanup));
    // Native destruction is complete, but its actual Domain outcome has not
    // yet retired the producer's ledger entry.
    QVERIFY(env.scheduler->pending() != 0);
    settle(); QCOMPARE(node.effects, 0); QCOMPARE(node.metadata, 0); QCOMPARE(env.scheduler->pending(), size_t{0});
}

void RpcConsoleModelTests::manualExecutionSeparatesEffectOutcomeAndPublication()
{
    struct Node final : RpcTestStubNode {
        int effects{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override { ++effects; return UniValue{7}; }
    } node;
    ManualExecutionEnvironment env;
    RpcConsoleModel model{node, {env.scheduler, [&](auto callback) { env.post(std::move(callback)); }}};
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    QVERIFY(model.submitCommand("getblockcount"));
    QCOMPARE(node.effects, 0); QVERIFY(model.executing()); QCOMPARE(model.outputModel()->rowCount(), 0);
    QVERIFY(env.step());
    QCOMPARE(node.effects, 1); QVERIFY(model.executing()); QCOMPARE(model.outputModel()->rowCount(), 0);
    QVERIFY(env.step()); // Domain owns the actual outcome, GUI still untouched.
    QCOMPARE(node.effects, 1); QCOMPARE(model.outputModel()->rowCount(), 0);
    env.publishAll();
    QVERIFY(!model.executing()); QCOMPARE(model.outputModel()->rowCount(), 2);
    QCOMPARE(replyHtml(model.outputModel()), QStringLiteral("7"));
}

void RpcConsoleModelTests::acceptedCommandSurvivesObserverLoss()
{
    struct Node final : RpcTestStubNode {
        int effects{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override { ++effects; return {}; }
    } node;
    ManualExecutionEnvironment env;
    auto model = std::make_unique<RpcConsoleModel>(node, execution::SnapshotServices{env.scheduler, [&](auto callback) { env.post(std::move(callback)); }});
    QVERIFY(model->submitCommand("getblockcount"));
    model.reset(); // No nested GUI loop or cancellation of the accepted effect.
    QCOMPARE(node.effects, 0);
    env.drain(); env.publishAll();
    QCOMPARE(node.effects, 1); QVERIFY(env.quiescent());
}

void RpcConsoleModelTests::sharedSchedulerClosureWaitsForActualNativeRetirement()
{
    struct Node final : RpcTestStubNode {
        int effects{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override {
            ++effects; return UniValue{std::string(200'000, 'r')};
        }
    } node;
    ManualRoutedExecution env;
    std::deque<std::function<void()>> gui;
    RpcConsoleModel model{node, {env.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}};
    QSignalSpy drained{&model, &RpcConsoleModel::drained};
    QVERIFY(model.submitCommand("getblockcount")); QCOMPARE(node.effects, 0);
    env.scheduler->closeAdmission();
    model.beginShutdown();
    QVERIFY(!model.submitCommand("getblockcount")); QCOMPARE(drained.size(), 0);
    QVERIFY(env.step(execution::Lane::Core)); QCOMPARE(node.effects, 1); QCOMPARE(drained.size(), 0);
    QVERIFY(env.step(execution::Lane::Domain));
    while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
    QCOMPARE(drained.size(), 0); // The actual outcome finished; Native payload retirement has not.
    QVERIFY(env.step(execution::Lane::Cleanup)); QCOMPARE(drained.size(), 0);
    env.drain(); QVERIFY(env.quiescent());
    while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
    QCOMPARE(drained.size(), 1); QCOMPARE(model.outputModel()->rowCount(), 0);
    model.beginShutdown(); QCOMPARE(drained.size(), 1); QCOMPARE(node.effects, 1);
}

void RpcConsoleModelTests::aggregateOutputAndRowsStayBounded()
{
    LargeResultNode node;
    RpcConsoleModel model{node};
    for (int command = 0; command < 50; ++command) submitAndSettle(model, "getblockcount");
    auto* output = model.outputModel();
    QVERIFY(output->rowCount() <= RpcOutputListModel::kMaxRows);
    qsizetype characters{0};
    for (int row = 0; row < output->rowCount(); ++row) characters += output->data(output->index(row, 0), RpcOutputListModel::ContentRole).toString().size();
    QVERIFY(characters <= RpcOutputListModel::kMaxCharacters);
    QVERIFY(characters > 1'000'000); // Exercise eviction, never an empty pass.
}

void RpcConsoleModelTests::autocompleteRejectsStaleManualResults()
{
    CommandListNode node;
    ManualExecutionEnvironment env;
    RpcConsoleModel model{node, {env.scheduler, [&](auto callback) { env.post(std::move(callback)); }}};
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    model.onNodeInitialized(); env.drain(); env.publishAll();
    QVERIFY(!model.availableCommands().isEmpty());
    model.setCompletionPrefix("getb");
    QVERIFY(model.completionBusy()); QCOMPARE(model.completionSuggestions().size(), 0);
    env.drain(); // Older match set is retained in the mailbox, not yet GUI.
    model.setCompletionPrefix("help getblockc");
    env.publishAll();
    QVERIFY(model.completionBusy()); QVERIFY(model.completionSuggestions().isEmpty());
    env.drain(); env.publishAll();
    QVERIFY(!model.completionBusy());
    QCOMPARE(model.completionSuggestions(), QStringList{QStringLiteral("help getblockcount")});
}

void RpcConsoleModelTests::historyNavigationOldestToNewest()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    // Each command must settle before the next, since submitCommand() refuses a
    // new command while one is executing.
    submitAndSettle(model, "cmd_a");
    submitAndSettle(model, "cmd_b");
    submitAndSettle(model, "cmd_c");

    // Navigate backward (direction=1 → older)
    QCOMPARE(model.browseHistory(1, ""), QString("cmd_c"));
    QCOMPARE(model.browseHistory(1, ""), QString("cmd_b"));
    QCOMPARE(model.browseHistory(1, ""), QString("cmd_a"));

    // Clamped at oldest — should stay at cmd_a
    QCOMPARE(model.browseHistory(1, ""), QString("cmd_a"));

    // Navigate forward (direction=-1 → newer)
    QCOMPARE(model.browseHistory(-1, ""), QString("cmd_b"));
    QCOMPARE(model.browseHistory(-1, ""), QString("cmd_c"));

    // Past the newest end → restore pending text (empty string we passed in)
    QCOMPARE(model.browseHistory(-1, ""), QString(""));
}

void RpcConsoleModelTests::historyNavigationRestoresPendingText()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    submitAndSettle(model, "getblockcount");
    submitAndSettle(model, "help");

    const QString pending = "getblock";
    QCOMPARE(model.browseHistory(1, pending), QString("help"));
    QCOMPARE(model.browseHistory(1, pending), QString("getblockcount"));
    // Navigate back past newest → pending text is restored
    QCOMPARE(model.browseHistory(-1, pending), QString("help"));
    QCOMPARE(model.browseHistory(-1, pending), QString("getblock"));
}

void RpcConsoleModelTests::historyDeduplicatesConsecutiveDuplicates()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    submitAndSettle(model, "getblockcount");
    submitAndSettle(model, "getblockcount"); // duplicate — should not be added
    submitAndSettle(model, "getblockcount"); // duplicate — should not be added
    submitAndSettle(model, "help");

    // History should be: [getblockcount, help]
    QCOMPARE(model.browseHistory(1, ""), QString("help"));
    QCOMPARE(model.browseHistory(1, ""), QString("getblockcount"));
    // No more entries
    QCOMPARE(model.browseHistory(1, ""), QString("getblockcount"));
}

void RpcConsoleModelTests::historyTruncatesAtMax()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    // Submit 51 unique commands — only the last 50 should be kept.
    for (int i = 0; i < 51; ++i) {
        submitAndSettle(model, QString("cmd_%1").arg(i));
    }

    // Navigate to oldest: should be cmd_1 (cmd_0 was evicted), newest cmd_50.
    // Walk all the way back to the oldest.
    QString last;
    for (int i = 0; i < 50; ++i) {
        last = model.browseHistory(1, "");
    }
    QCOMPARE(last, QString("cmd_1")); // cmd_0 was evicted
}

void RpcConsoleModelTests::resetHistoryNavigationClearsState()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    submitAndSettle(model, "getblockcount");
    submitAndSettle(model, "help");

    // Start navigating
    QCOMPARE(model.browseHistory(1, "typing..."), QString("help"));

    // Reset — next browseHistory should restart from the top
    model.resetHistoryNavigation();
    QCOMPARE(model.browseHistory(1, ""), QString("help"));
}

void RpcConsoleModelTests::submitRefusedWhileExecuting()
{
    BlockingNode mock;
    RpcConsoleModel model{mock};

    // First command occupies the worker thread and blocks there.
    QVERIFY(model.submitCommand("waitfornewblock"));
    QVERIFY(model.executing());

    // A second, non-"stop" command must be refused while one is in flight, so
    // keyboard Enter cannot queue commands behind the disabled Run button.
    QVERIFY(!model.submitCommand("getblockcount"));

    // Let the worker finish and the model settle.
    mock.release();
    QTRY_VERIFY(!model.executing());

    // The refused command never entered history — only the first remains.
    QCOMPARE(model.browseHistory(1, ""), QString("waitfornewblock"));
    QCOMPARE(model.browseHistory(1, ""), QString("waitfornewblock")); // clamped: no 2nd entry
}

void RpcConsoleModelTests::stopRunsIndependentlyWhileExecuting()
{
    StopAbortNode mock;
    RpcConsoleModel model{mock};

    // A long-running command blocks the worker thread.
    QVERIFY(model.submitCommand("waitfornewblock"));
    QVERIFY(model.executing());

    // Stop is admitted independently of the blocked command. Admission does
    // not claim that Core has already executed the shutdown request.
    QVERIFY(model.submitCommand("stop"));
    QTRY_VERIFY(mock.stopExecuted());
    QVERIFY(!mock.stopOnGui());

    // The independent stop released the blocked command, so the model settles.
    QTRY_VERIFY(!model.executing());
}

void RpcConsoleModelTests::walletNameScopesRpcToWalletUri()
{
    UriCapturingNode mock;
    RpcConsoleModel model{mock};

    // A non-empty wallet name is percent-encoded into a /wallet/<name> endpoint.
    model.submitCommand("getwalletinfo", "my wallet");
    QTRY_VERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString("/wallet/my%20wallet"));

    // An empty wallet name runs the command without a wallet endpoint.
    model.submitCommand("getblockcount", "");
    QTRY_VERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString());
}

void RpcConsoleModelTests::outputRowsExposeCategoryAndRawTimestamp()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    auto* out = qobject_cast<QAbstractListModel*>(model.outputModel());
    QVERIFY(out != nullptr);
    const int timestamp_role = roleForName(out, "timestamp");
    const int content_role = roleForName(out, "content");
    const int category_role = roleForName(out, "category");
    QVERIFY(timestamp_role != -1);
    QVERIFY(content_role != -1);
    QVERIFY(category_role != -1);

    QVERIFY(model.submitCommand("getblockcount"));
    QTRY_VERIFY(out->rowCount() >= 1);

    const QModelIndex request_index = out->index(0, 0);
    const QString timestamp = out->data(request_index, timestamp_role).toString();
    QCOMPARE(timestamp.size(), 8);
    QVERIFY(!timestamp.startsWith("["));
    QVERIFY(!timestamp.endsWith("]"));
    QCOMPARE(out->data(request_index, category_role).toInt(), int(RpcConsoleModel::CMD_REQUEST));

    const QString request_html = out->data(request_index, content_role).toString();
    QVERIFY(request_html.contains("getblockcount"));
    QVERIFY(!request_html.contains("&gt;&gt;"));

    QTRY_VERIFY(!model.executing());
    QCOMPARE(out->rowCount(), 2);
    QCOMPARE(out->data(out->index(1, 0), category_role).toInt(), int(RpcConsoleModel::CMD_REPLY));
}

void RpcConsoleModelTests::clearRemovesOutput()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    auto* out = qobject_cast<QAbstractListModel*>(model.outputModel());
    QVERIFY(out != nullptr);
    submitAndSettle(model, "getblockcount");
    QVERIFY(out->rowCount() > 0);

    model.clear();
    QCOMPARE(out->rowCount(), 0);
}

void RpcConsoleModelTests::outputTruncatedWhenResultTooLong()
{
    LargeResultNode mock;
    RpcConsoleModel model{mock};

    auto* out = qobject_cast<QAbstractListModel*>(model.outputModel());
    QVERIFY(out != nullptr);
    QSignalSpy spy{out, &QAbstractItemModel::rowsInserted};

    QVERIFY(model.submitCommand("getblockcount"));
    QTRY_VERIFY(!model.executing());
    QVERIFY(out->rowCount() > 2);
    for (int i = 0; i < out->rowCount(); ++i) {
        QTextDocument document;
        const auto html = out->data(out->index(i, 0), RpcOutputListModel::ContentRole).toString();
        document.setHtml(html);
        QVERIFY(document.toPlainText().size() <= RpcOutputListModel::kRowCharacters);
        QVERIFY(html.size() < 8192);
    }

    // Look up the content role dynamically — the model's roleNames map
    // "content" to the ContentRole integer.
    int content_role = roleForName(out, "content");
    QVERIFY(content_role != -1);

    const QString reply_html = replyHtml(out);

    // Formatted reply must be well below the raw 100,000-character input.
    QVERIFY2(reply_html.size() < 100'000,
             qPrintable(QString("reply_html size was %1").arg(reply_html.size())));
    // The truncation notice must appear in the output.
    QVERIFY2(reply_html.contains("truncated"),
             "Expected truncation notice in output");
}

void RpcConsoleModelTests::jsonReplyKeyColoringSkipsStringsContainingColons()
{
    JsonReplyNode mock;
    RpcConsoleModel model{mock};

    auto* out = qobject_cast<QAbstractListModel*>(model.outputModel());
    QVERIFY(out != nullptr);
    QSignalSpy spy{out, &QAbstractItemModel::rowsInserted};

    model.submitCommand("getsomething");
    QTRY_VERIFY(!model.executing());
    QVERIFY(out->rowCount() > 2);

    int content_role = roleForName(out, "content");
    QVERIFY(content_role != -1);

    const QString html = replyHtml(out);

    // Real keys are wrapped in a key-colour span that closes immediately
    // after the quoted key — the structural formatter produces literally
    // `"key1"</span>` at the boundary.
    QVERIFY2(html.contains(QStringLiteral("\"key1\"</span>")),
             qPrintable("Missing coloured wrap for key1 in:\n" + html));
    QVERIFY2(html.contains(QStringLiteral("\"note\"</span>")),
             qPrintable("Missing coloured wrap for note in:\n" + html));
    QVERIFY2(html.contains(QStringLiteral("\"key3\"</span>")),
             qPrintable("Missing coloured wrap for key3 in:\n" + html));

    // The string value `"see section:"` must NOT close a key-colour span.
    // The old regex-based formatter incorrectly treated this as a key.
    QVERIFY2(!html.contains(QStringLiteral("\"see section\"</span>")),
             qPrintable("String value was mis-coloured as a key in:\n" + html));
}

void RpcConsoleModelTests::jsonReplyPreservesNestedFormatting_data()
{
    QTest::addColumn<QString>("json");
    QTest::addColumn<QString>("expected");
    QTest::newRow("empty-object") << QStringLiteral("{}") << QStringLiteral("{}");
    QTest::newRow("empty-array") << QStringLiteral("[]") << QStringLiteral("[]");
    const QString indent{QStringLiteral("&nbsp;&nbsp;")};
    const QString key{QStringLiteral("<span style='color:#123456'>\"")};
    QTest::newRow("mixed-containers") << QStringLiteral(R"({"a":[1,{"b":[]}],"z":{}})")
        << "{<br>" + indent + key + "a\"</span>: [<br>" + indent.repeated(2) + "1,<br>" +
           indent.repeated(2) + "{<br>" + indent.repeated(3) + key + "b\"</span>: []<br>" +
           indent.repeated(2) + "}<br>" + indent + "],<br>" + indent + key + "z\"</span>: {}<br>}";
    QTest::newRow("scalar-values") << QStringLiteral(R"([null,true,false,12,"<>&"])")
        << "[<br>" + indent + "null,<br>" + indent + "true,<br>" + indent + "false,<br>" +
           indent + "12,<br>" + indent + "\"&lt;&gt;&amp;\"<br>]";
    constexpr int depth{64};
    QString expected;
    for (int level{0}; level < depth; ++level) expected += "[<br>" + indent.repeated(level + 1);
    expected += "null";
    for (int level{depth - 1}; level >= 0; --level) expected += "<br>" + indent.repeated(level) + "]";
    QTest::newRow("deep-array") << QString(depth, '[') + "null" + QString(depth, ']') << expected;
}

void RpcConsoleModelTests::jsonReplyPreservesNestedFormatting()
{
    QFETCH(QString, json);
    QFETCH(QString, expected);
    UniValue reply;
    QVERIFY(reply.read(json.toStdString()));
    StructuredReplyNode mock{std::move(reply)};
    RpcConsoleModel model{mock};
    QVERIFY(model.setProperty("keyColor", QColor{"#123456"}));
    submitAndSettle(model, "getsomething");
    auto* out{model.outputModel()};
    QVERIFY(out->rowCount() >= 2);
    const int content_role{roleForName(out, "content")};
    QVERIFY(content_role != -1);
    QCOMPARE(replyHtml(out), expected);
}

void RpcConsoleModelTests::jsonReplyTraversal_data()
{
    QTest::addColumn<QString>("raw");
    QTest::addColumn<QString>("expected");
    QTest::newRow("empty-object") << QStringLiteral("{}") << QStringLiteral("{}");
    QTest::newRow("empty-array") << QStringLiteral("[]") << QStringLiteral("[]");
    QTest::newRow("mixed") << QStringLiteral("[{},[],true,false,null,12,\"<x>\"]")
        << QStringLiteral("[<br>&nbsp;&nbsp;{},<br>&nbsp;&nbsp;[],<br>&nbsp;&nbsp;true,<br>&nbsp;&nbsp;false,<br>&nbsp;&nbsp;null,<br>&nbsp;&nbsp;12,<br>&nbsp;&nbsp;\"&lt;x&gt;\"<br>]");
    QTest::newRow("nested-key") << QStringLiteral("{\"<key>\":[{\"n\":1}]}")
        << QStringLiteral("{<br>&nbsp;&nbsp;<span style='color:#98c379'>\"&lt;key&gt;\"</span>: [<br>&nbsp;&nbsp;&nbsp;&nbsp;{<br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<span style='color:#98c379'>\"n\"</span>: 1<br>&nbsp;&nbsp;&nbsp;&nbsp;}<br>&nbsp;&nbsp;]<br>}");
    QTest::newRow("non-json") << QStringLiteral(" <help>\n  text") << QStringLiteral("&nbsp;&lt;help&gt;<br>&nbsp;&nbsp;text");
}

void RpcConsoleModelTests::jsonReplyTraversal()
{
    QFETCH(QString, raw);
    QFETCH(QString, expected);
    UniValue reply;
    if (!reply.read(raw.toStdString())) reply = UniValue(raw.toStdString());
    StructuredReplyNode node{std::move(reply)};
    RpcConsoleModel model{node};
    submitAndSettle(model, "getsomething");
    auto* output = qobject_cast<QAbstractListModel*>(model.outputModel());
    QVERIFY(output->rowCount() >= 2);
    QCOMPARE(replyHtml(output), expected);
}

void RpcConsoleModelTests::deeplyNestedJsonReply()
{
    constexpr int depth{256};
    const QString raw = QString(depth, '[') + "\"<leaf>\"" + QString(depth, ']');
    UniValue reply;
    if (!reply.read(raw.toStdString())) reply = UniValue(raw.toStdString());
    // Exercise the complete iterative formatter at depth256. Core pretty
    // serialization of an array at this depth exceeds the independent50k
    // console transport limit. A compact JSON string traverses the same reply
    // formatter without hitting that deliberately separate truncation policy.
    QVERIFY(reply.isArray());
    StructuredReplyNode node{UniValue{raw.toStdString()}};
    RpcConsoleModel model{node};
    submitAndSettle(model, "getsomething");
    auto* output = qobject_cast<QAbstractListModel*>(model.outputModel());
    const auto html = replyHtml(output);
    QCOMPARE(html.count('['), depth);
    QCOMPARE(html.count(']'), depth);
    QCOMPARE(html.count("<br>"), depth * 2);
    QCOMPARE(html.count("&lt;leaf&gt;"), 1);
    QVERIFY(!html.contains(','));
    QVERIFY(html.startsWith("[<br>"));
    QVERIFY(html.endsWith("<br>]"));
}

void RpcConsoleModelTests::availableCommandsIncludesHelpVariants()
{
    CommandListNode mock;
    RpcConsoleModel model{mock};

    // Populate availableCommands via the public slot.
    model.onNodeInitialized();

    QTRY_VERIFY(!model.availableCommands().isEmpty());
    QStringList cmds = model.availableCommands();

    // Original commands present
    QVERIFY(cmds.contains("getblockcount"));
    QVERIFY(cmds.contains("getblock"));
    QVERIFY(cmds.contains("help"));

    // "help <cmd>" variants present
    QVERIFY(cmds.contains("help getblockcount"));
    QVERIFY(cmds.contains("help getblock"));
    QVERIFY(cmds.contains("help help"));

    // "help-console" present
    QVERIFY(cmds.contains("help-console"));

    // List is sorted (case-insensitive)
    for (int i = 1; i < cmds.size(); ++i) {
        QVERIFY2(cmds[i - 1].compare(cmds[i], Qt::CaseInsensitive) <= 0,
                 qPrintable(QString("Not sorted: '%1' > '%2'").arg(cmds[i - 1], cmds[i])));
    }

    // No duplicates
    QStringList deduped = cmds;
    deduped.removeDuplicates();
    QCOMPARE(cmds.size(), deduped.size());
}

void RpcConsoleModelTests::secretRequestsAreNotRetained_data()
{
    QTest::addColumn<QString>("method"); QTest::addColumn<QString>("arguments"); QTest::addColumn<QString>("expected_parameters");
    QTest::newRow("createwallet-passphrase") << "createwallet" << "redaction-wallet false false unique-rpc-secret-92"
        << R"(["redaction-wallet",false,false,"unique-rpc-secret-92"])";
    QTest::newRow("migratewallet-passphrase") << "migratewallet" << "redaction-wallet unique-rpc-secret-92"
        << R"(["redaction-wallet","unique-rpc-secret-92"])";
    QTest::newRow("addhdkey-private-key") << "addhdkey" << "unique-rpc-secret-92"
        << R"(["unique-rpc-secret-92"])";
    QTest::newRow("walletpassphrase-control") << "walletpassphrase" << "unique-rpc-secret-92 10"
        << R"(["unique-rpc-secret-92",10])";
}

void RpcConsoleModelTests::secretRequestsAreNotRetained()
{
    QFETCH(QString, method); QFETCH(QString, arguments); QFETCH(QString, expected_parameters);
    class RecordingNode final : public RpcTestStubNode {
        QMutex mutex;
        std::string called_method, called_arguments;
    public:
        UniValue executeRpc(const std::string& method, const UniValue& parameters, const std::string&) override
        {
            const QMutexLocker lock{&mutex}; called_method = method; called_arguments = parameters.write();
            return UniValue("safe reply");
        }
        std::pair<std::string, std::string> received() { const QMutexLocker lock{&mutex}; return {called_method, called_arguments}; }
    } node;
    RpcConsoleModel model{node};
    QVERIFY(model.submitCommand(method + " " + arguments));
    QTRY_VERIFY(!model.executing());
    const auto received = node.received();
    QCOMPARE(received.first, method.toStdString());
    QCOMPARE(received.second, expected_parameters.toStdString());
    QVERIFY(QString::fromStdString(received.second).contains("unique-rpc-secret-92"));
    const int content = roleForName(model.outputModel(), "content"); QVERIFY(content >= 0);
    bool requests_safe{true};
    for (int i = 0; i < model.outputModel()->rowCount(); ++i) {
        const auto text = model.outputModel()->data(model.outputModel()->index(i, 0), content).toString();
        requests_safe = requests_safe && !text.contains("unique-rpc-secret-92");
    }
    model.clear(); QCOMPARE(model.outputModel()->rowCount(), 0);
    const QString pending = "ordinary unsent draft";
    const QString recalled = model.browseHistory(1, pending);
    const bool history_safe = !recalled.contains("unique-rpc-secret-92");
    qInfo() << "RPC privacy oracle" << method << "request safe" << requests_safe << "recall after Clear safe" << history_safe;
    QCOMPARE(model.browseHistory(-1, pending), pending);
    QVERIFY(requests_safe); QVERIFY(history_safe);
    QCOMPARE(recalled, method + QString::fromUtf8("(…)"));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(RpcConsoleModelTests)
#else
QTEST_MAIN(RpcConsoleModelTests)
#endif
#include <test_rpcconsolemodel.moc>
