// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep
#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_application.h>
#ifdef ENABLE_WALLET
#include <test/integration/fixture/test_wallet.h>
#endif
#include <test/integration/fixture/test_util.h>
#include <qml/models/rpcconsolemodel.h>
#include <qml/models/monospaceoutputmodel.h>

#include <QSignalSpy>
#include <QRegularExpression>

class ConsoleIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    WalletMode m_mode;

    bool submitCommand(const QString& command)
    {
        if (!qmltest::SetText(m_app, "consoleInput", command)) return false;
        if (!QMetaObject::invokeMethod(qmltest::Find(m_app, "consoleInput"), "forceActiveFocus")) return false;
        QTest::keyClick(m_app.window(), Qt::Key_Return, Qt::NoModifier, 1);
        return true;
    }

    MonospaceOutputModel* projection() const
    {
        auto* list = qmltest::Find(m_app, "consoleOutputArea_list");
        return list ? qobject_cast<MonospaceOutputModel*>(list->property("model").value<QObject*>()) : nullptr;
    }

    bool waitForOutput()
    {
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel");
        QPointer<MonospaceOutputModel> view{projection()};
        return model && view && QTest::qWaitFor([&] {
            return !model->executing() && view && !view->busy();
        }) && view && view->error().isEmpty() && !view->truncated() && view->sourceModel() == model->outputModel();
    }

    // Physical rows are bounded lines/chunks. A timestamp starts each original
    // logical message. The first wallet query additionally has one exact
    // context banner; Clear preserves that context for later exchanges.
    // No events are processed during the individual role reads: no model
    // mutation can interleave this synchronous, bounded traversal.
    bool hasLogicalExchange(int reply_category, const QString& forbidden = {}, const QString& expected_context = {}) const
    {
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel");
        QPointer<MonospaceOutputModel> view{projection()};
        if (!model || model->executing() || !view || view->busy() || !view->error().isEmpty() || view->truncated()) return false;
        QPointer<RpcOutputListModel> source{model->outputModel()};
        const int rows = source->rowCount();
        if (view->sourceModel() != source || rows <= 0 || rows > RpcOutputListModel::kMaxRows) return false;
        const int context_messages = expected_context.isEmpty() ? 0 : 1;
        const int expected_starts = 2 + context_messages;
        QString first_timestamp;
        int starts{0};
        for (int row{0}; row < rows; ++row) {
            const auto original = [&] {
                return source && view && model->outputModel() == source && source->rowCount() == rows && view->sourceModel() == source;
            };
            const auto read = [&](int role) {
                if (!original()) return QVariant{};
                const auto value = source->data(source->index(row, 0), role);
                return original() ? value : QVariant{};
            };
            const auto timestamp = read(RpcOutputListModel::TimestampRole);
            const auto content = read(RpcOutputListModel::ContentRole);
            const auto category = read(RpcOutputListModel::CategoryRole);
            const auto continuation = read(RpcOutputListModel::ContinuationRole);
            if (!timestamp.isValid() || !content.isValid() || !category.isValid() || !continuation.isValid()) return false;
            if (!forbidden.isEmpty() && content.toString().contains(forbidden)) return false;
            if (!timestamp.toString().isEmpty()) {
                if (continuation.toBool() || !QRegularExpression{QStringLiteral("^[0-9]{2}:[0-9]{2}:[0-9]{2}$")}.match(timestamp.toString()).hasMatch()) return false;
                ++starts;
                if (starts == 1) first_timestamp = timestamp.toString();
            }
            if (starts < 1 || starts > expected_starts || category.toInt() !=
                (starts <= 1 + context_messages ? RpcConsoleModel::CMD_REQUEST : reply_category)) return false;
            // The known short banner occupies precisely one source row.
            if (context_messages && starts == 1 && row != 0) return false;
        }
        if (context_messages) {
            if (!view || view->rowCount() <= 0) return false;
            const auto first = view->index(0, 0);
            if (view->data(first, MonospaceOutputModel::PlainContentRole).toString() != expected_context ||
                view->data(first, MonospaceOutputModel::LeftRole).toString() != first_timestamp ||
                view->data(first, MonospaceOutputModel::CategoryRole).toInt() != RpcConsoleModel::CMD_REQUEST) return false;
        }
        return starts == expected_starts && source && source->rowCount() == rows && view && view->sourceModel() == source;
    }

    bool searchMatches(const QString& query, int expected)
    {
        if (!qmltest::SetText(m_app, "rpcConsoleSearchField", query)) return false;
        QPointer<MonospaceOutputModel> view{projection()};
        auto* console = qmltest::Find(m_app, "rpcConsole");
        return view && console && QTest::qWaitFor([&] {
            return view && console->property("searchText").toString() == query &&
                   view->searchText() == query && !view->busy() && !view->searchBusy() &&
                   view->searchResultCount() == expected && console->property("searchResultCount").toInt() == expected;
        }) && view && view->error().isEmpty() && !view->truncated() && !view->searchLimited() &&
            view->searchResultCount() == expected && console->property("searchResultCount").toInt() == expected;
    }

public:
    explicit ConsoleIntegrationTests(BitcoinQmlApplication& app, WalletMode mode) : m_app{app}, m_mode{mode} {}

private Q_SLOTS:
    void init()
    {
        QVERIFY(qmltest::SelectSettings(m_app, "rpc-console"));
        QVERIFY(qmltest::WaitForVisible(m_app, "rpcConsole"));
    }

    void executesCommandsAndRecoversFromErrors_data()
    {
        QTest::addColumn<QString>("command");
        QTest::addColumn<QString>("expected_reply");
        QTest::newRow("block-count") << "getblockcount" << "0";
        QTest::newRow("help") << "help getblockcount" << "Returns the height";
        QTest::newRow("unknown-command") << "thiscommanddoesnotexist" << "Method not found";
    }

    void executesCommandsAndRecoversFromErrors()
    {
        QFETCH(QString, command);
        QFETCH(QString, expected_reply);
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel");
        QVERIFY(model);
        auto* console = qmltest::Find(m_app, "rpcConsole");
        QVERIFY(console);

        QVERIFY(submitCommand(command));
        QVERIFY(waitForOutput());
        const int reply_category = command == "thiscommanddoesnotexist" ? RpcConsoleModel::CMD_ERROR : RpcConsoleModel::CMD_REPLY;
        QVERIFY(hasLogicalExchange(reply_category));
        // Search the complete Native projection, including uninstantiated rows.
        QVERIFY(searchMatches(expected_reply, 1));

        // Searching narrows the matches without removing console history.
        QVERIFY(searchMatches(command, 1));
        QVERIFY(hasLogicalExchange(reply_category));
        QVERIFY(searchMatches("unique-native-absent-output-sentinel-92", 0));
        QVERIFY(searchMatches("", 0));
        QVERIFY(QMetaObject::invokeMethod(console, "clearInputOrOutput"));
        QTRY_COMPARE(console->property("outputCount").toInt(), 0);
        QVERIFY(waitForOutput());
        QTRY_COMPARE(projection()->rowCount(), 0);
        QVERIFY(!qmltest::Find(m_app, "rpcConsoleHelpFooter")->property("text").toString().isEmpty());

        // A successful command still works after clearing either kind of reply.
        QVERIFY(submitCommand("getblockcount"));
        QVERIFY(waitForOutput());
        QVERIFY(hasLogicalExchange(RpcConsoleModel::CMD_REPLY));
        QVERIFY(searchMatches("getblockcount", 1));
        QVERIFY(searchMatches("0", 1));
        QVERIFY(searchMatches("", 0));
    }

    void secretRequestsStayRedactedAfterClear_data()
    {
        QTest::addColumn<QString>("method"); QTest::addColumn<QString>("arguments"); QTest::addColumn<bool>("sensitive");
        QTest::newRow("createwallet-passphrase") << "createwallet" << "privacy-rpc false false unique-native-secret-92" << true;
        QTest::newRow("migratewallet-passphrase") << "migratewallet" << "privacy-rpc unique-native-secret-92" << true;
        QTest::newRow("addhdkey-private-key") << "addhdkey" << "unique-native-secret-92" << true;
        QTest::newRow("walletpassphrase-control") << "walletpassphrase" << "unique-native-secret-92 10" << true;
        QTest::newRow("ordinary-control") << "help" << "getblockcount" << false;
    }

    void secretRequestsStayRedactedAfterClear()
    {
        QFETCH(QString, method); QFETCH(QString, arguments); QFETCH(bool, sensitive);
        const bool wallet_enabled = m_mode == WalletMode::Enabled;
#ifdef ENABLE_WALLET
        WalletQmlModel* wallet{nullptr};
        UniValue loaded_before;
        if (wallet_enabled) {
            wallet = qmltest::CreateWallet(m_app, "privacy-rpc"); QVERIFY(wallet);
            QVERIFY(wallet->encryptWallet("owned test fixture password"));
            QTRY_COMPARE(wallet->securityState(), WalletQmlModel::SecurityState::SecuritySucceeded);
            QVERIFY(wallet->isLocked());
            loaded_before = qmltest::Rpc(m_app, "listwallets");
            QVERIFY(qmltest::SelectSettings(m_app, "rpc-console"));
            QVERIFY(qmltest::WaitForVisible(m_app, "rpcConsole"));
        }
#endif
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel"); QVERIFY(model);
        const QString command = arguments.isEmpty() ? method : method + " " + arguments;
        QVERIFY(submitCommand(command));
        QVERIFY(waitForOutput());
        auto* output = model->outputModel();
        const QString expected_request = sensitive ? method + QString::fromUtf8("(…)") : command;
        QVERIFY(searchMatches(expected_request, 1));
        QVERIFY(searchMatches(sensitive ? QStringLiteral("(code ") : QStringLiteral("Returns the height"), 1));
        const bool request_safe = hasLogicalExchange(sensitive ? RpcConsoleModel::CMD_ERROR : RpcConsoleModel::CMD_REPLY,
                                                     "unique-native-secret-92",
                                                     wallet_enabled ? QStringLiteral("Executing command using \"privacy-rpc\" wallet") : QString{});
        QVERIFY(request_safe);
        QVERIFY(searchMatches("unique-native-secret-92", 0));
        QVERIFY(searchMatches("", 0));
        auto* console = qmltest::Find(m_app, "rpcConsole"); QVERIFY(console);
        QVERIFY(QMetaObject::invokeMethod(console, "clearInputOrOutput")); QTRY_COMPARE(output->rowCount(), 0);
        QVERIFY(waitForOutput());
        QTRY_COMPARE(projection()->rowCount(), 0);
        QVERIFY(searchMatches("unique-native-secret-92", 0));
        QVERIFY(searchMatches("", 0));
        auto* input = qmltest::Find(m_app, "consoleInput"); QVERIFY(input);
        QVERIFY(QMetaObject::invokeMethod(input, "forceActiveFocus"));
        QTest::keyClick(m_app.window(), Qt::Key_Up, Qt::NoModifier, 1);
        QTRY_VERIFY(!input->property("text").toString().isEmpty());
        const QString recalled = input->property("text").toString();
        const bool recall_safe = !recalled.contains("unique-native-secret-92");
        auto* completion = qmltest::Find(m_app, "consoleAutocompletePopup");
        if (completion && completion->property("visible").toBool()) {
            QTest::keyClick(m_app.window(), Qt::Key_Escape, Qt::NoModifier, 1);
            QTRY_VERIFY(!completion->property("visible").toBool());
            QCOMPARE(input->property("text").toString(), recalled);
        }
        QTest::keyClick(m_app.window(), Qt::Key_Down, Qt::NoModifier, 1); QTRY_COMPARE(input->property("text").toString(), QString{});
#ifdef ENABLE_WALLET
        if (wallet_enabled) {
            QVERIFY(wallet->isLocked());
            QCOMPARE(qmltest::Rpc(m_app, "listwallets").write(), loaded_before.write());
        }
#endif
        qInfo() << "Native RPC privacy oracle" << method << "wallet enabled" << wallet_enabled
                << "request safe" << request_safe << "recall after Clear safe" << recall_safe;
        QVERIFY(request_safe); QVERIFY(recall_safe);
        QCOMPARE(recalled, sensitive ? method + QString::fromUtf8("(…)") : command);
    }

    void consoleLayoutAndOutputStayAlignedAcrossFontSizes()
    {
        auto* console = qmltest::Find(m_app, "rpcConsole");
        QVERIFY(console);
        const auto geometry = [&](const char* name, const char* property) {
            auto* object = qmltest::Find(m_app, name);
            return object ? object->property(property).toDouble() : -1.0;
        };

        // The input row keeps its fixed padding and prompt dimensions.
        QCOMPARE(geometry("consoleInputRow", "x"), 0.0);
        QCOMPARE(geometry("consoleInputRow", "width"), console->property("width").toDouble());
        QCOMPARE(geometry("consoleInputRow", "height"), 64.0);
        QCOMPARE(geometry("consoleInputDivider", "height"), 1.0);
        QCOMPARE(geometry("consoleInputContent", "x"), 12.0);
        QCOMPARE(geometry("consoleInputContent", "y"), 12.0);
        QCOMPARE(geometry("consoleInputContent", "height"), 40.0);
        QCOMPARE(geometry("consolePromptIcon", "width"), 16.0);
        QCOMPARE(geometry("consolePromptIcon", "height"), 16.0);
        QCOMPARE(geometry("consoleInputContent", "x") + geometry("consoleInput", "x"), 36.0);
        QVERIFY(!qmltest::Find(m_app, "rpcConsoleSearchPreviousButton")->property("enabled").toBool());
        QVERIFY(!qmltest::Find(m_app, "rpcConsoleSearchNextButton")->property("enabled").toBool());
        QVERIFY(!qmltest::Find(m_app, "rpcConsoleWarningBanner")->property("text").toString().isEmpty());

        // Both the command and its result must stay aligned at every font size.
        QVERIFY(submitCommand("getblockcount"));
        QVERIFY(waitForOutput());
        QVERIFY(hasLogicalExchange(RpcConsoleModel::CMD_REPLY));
        QTRY_COMPARE(projection()->rowCount(), 2);
        const int original_size = console->property("outputFontPixelSize").toInt();
        const int minimum = console->property("minimumOutputFontPixelSize").toInt();
        const int maximum = console->property("maximumOutputFontPixelSize").toInt();
        for (int size{original_size}; size > minimum; --size) QVERIFY(qmltest::Click(m_app, "consoleFontDecreaseButton"));
        for (int size{minimum}; size <= maximum; ++size) {
            QTRY_COMPARE(console->property("outputFontPixelSize").toInt(), size);
            for (int row{0}; row < 2; ++row) {
                const auto row_name = QString{"consoleOutputArea_row_%1"}.arg(row);
                const auto timestamp_name = QString{"consoleOutputArea_left_%1"}.arg(row);
                const auto content_name = QString{"consoleOutputArea_content_%1"}.arg(row);
                QVERIFY(qmltest::WaitForVisible(m_app, qPrintable(row_name)));
                QTRY_VERIFY(qAbs(geometry(qPrintable(row_name), "width") - console->property("width").toDouble() + 32) <= 1);
                QTRY_VERIFY(geometry(qPrintable(timestamp_name), "width") >= std::max(60.0, geometry(qPrintable(timestamp_name), "contentWidth")));
                QTRY_VERIFY(qAbs(geometry(qPrintable(content_name), "x") - geometry(qPrintable(timestamp_name), "width") - 20) <= 1);
                QCOMPARE(qmltest::Find(m_app, qPrintable(row_name))->property("rowCategory").toInt(), row);
            }
            if (size < maximum) QVERIFY(qmltest::Click(m_app, "consoleFontIncreaseButton"));
        }
        const auto timestamp = qmltest::Find(m_app, "consoleOutputArea_left_0")->property("text").toString();
        QVERIFY(QRegularExpression{QStringLiteral("^[0-9]{2}:[0-9]{2}:[0-9]{2}$")}.match(timestamp).hasMatch());
        QVERIFY(qmltest::ReturnToNode(m_app));
        QVERIFY(qmltest::WaitForVisible(m_app, "blockClock"));
    }

    void autocompleteUsesLiveRpcCommands()
    {
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel");
        QVERIFY(model);
        QVERIFY(qmltest::SetText(m_app, "consoleInput", "getblockcou"));
        QVERIFY(qmltest::WaitForVisible(m_app, "consoleAutocompletePopup"));
        QTRY_VERIFY(!model->completionBusy());
        QVERIFY(model->completionError().isEmpty());
        QCOMPARE(model->completionSuggestions(), QStringList{QStringLiteral("getblockcount")});
        QVERIFY(qmltest::Click(m_app, "consoleAutocomplete_0"));
        QTRY_COMPARE(qmltest::Find(m_app, "consoleInput")->property("text").toString().trimmed(), QStringLiteral("getblockcount"));
        QVERIFY(qmltest::SetText(m_app, "consoleInput", "zzzznotacommand"));
        QTRY_COMPARE(model->completionPrefix(), QStringLiteral("zzzznotacommand"));
        QTRY_VERIFY(!model->completionBusy());
        QVERIFY(model->completionError().isEmpty());
        QCOMPARE(model->completionSuggestions().size(), 0);
        QTRY_VERIFY(!qmltest::Find(m_app, "consoleAutocompletePopup")->property("visible").toBool());
        QVERIFY(qmltest::SetText(m_app, "consoleInput", "help get"));
        QVERIFY(qmltest::WaitForVisible(m_app, "consoleAutocompletePopup"));
        QTRY_VERIFY(!model->completionBusy());
        QVERIFY(model->completionError().isEmpty());
        QVERIFY(!model->completionSuggestions().isEmpty());
        QVERIFY(model->completionSuggestions().first().startsWith("help get"));
    }
};

std::unique_ptr<QObject> CreateConsoleTests(BitcoinQmlApplication& app, WalletMode mode)
{
    return std::make_unique<ConsoleIntegrationTests>(app, mode);
}

#include <test_console.moc>
