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
        QTRY_VERIFY(!model->executing());
        QTRY_COMPARE(console->property("outputCount").toInt(), 2);
        auto* output = qmltest::WaitForVisible(m_app, "consoleOutputArea_content_1");
        QVERIFY(output);
        QTRY_VERIFY2(output->property("text").toString().contains(expected_reply), qPrintable(output->property("text").toString()));

        // Searching narrows the matches without removing console history.
        QVERIFY(qmltest::SetText(m_app, "rpcConsoleSearchField", command));
        QTRY_COMPARE(console->property("searchResultCount").toInt(), 1);
        QCOMPARE(console->property("outputCount").toInt(), 2);
        QVERIFY(qmltest::SetText(m_app, "rpcConsoleSearchField", ""));
        QVERIFY(QMetaObject::invokeMethod(console, "clearInputOrOutput"));
        QTRY_COMPARE(console->property("outputCount").toInt(), 0);
        QVERIFY(!qmltest::Find(m_app, "rpcConsoleHelpFooter")->property("text").toString().isEmpty());

        // A successful command still works after clearing either kind of reply.
        QVERIFY(submitCommand("getblockcount"));
        QTRY_VERIFY(!model->executing());
        QTRY_COMPARE(console->property("outputCount").toInt(), 2);
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
            QVERIFY(wallet->encryptWallet("owned test fixture password")); QVERIFY(wallet->isLocked());
            loaded_before = qmltest::Rpc(m_app, "listwallets");
            QVERIFY(qmltest::SelectSettings(m_app, "rpc-console"));
            QVERIFY(qmltest::WaitForVisible(m_app, "rpcConsole"));
        }
#endif
        auto* model = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel"); QVERIFY(model);
        const QString command = arguments.isEmpty() ? method : method + " " + arguments;
        QVERIFY(submitCommand(command)); QTRY_VERIFY(!model->executing());
        auto* output = model->outputModel();
        bool request_safe{true}; bool expected_reply{false};
        for (int i = 0; i < output->rowCount(); ++i) {
            const auto index = output->index(i, 0);
            const auto text = output->data(index, RpcOutputListModel::ContentRole).toString();
            request_safe = request_safe && !text.contains("unique-native-secret-92");
            const auto category = output->data(index, RpcOutputListModel::CategoryRole).toInt();
            expected_reply = expected_reply || category == (sensitive ? RpcConsoleModel::CMD_ERROR : RpcConsoleModel::CMD_REPLY);
        }
        QVERIFY(expected_reply);
        auto* console = qmltest::Find(m_app, "rpcConsole"); QVERIFY(console);
        QVERIFY(QMetaObject::invokeMethod(console, "clearInputOrOutput")); QTRY_COMPARE(output->rowCount(), 0);
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
        QTRY_COMPARE(console->property("outputCount").toInt(), 2);
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
        QVERIFY(qmltest::SetText(m_app, "consoleInput", "getblockcou"));
        QVERIFY(qmltest::WaitForVisible(m_app, "consoleAutocompletePopup"));
        QVERIFY(qmltest::Click(m_app, "consoleAutocomplete_0"));
        QTRY_COMPARE(qmltest::Find(m_app, "consoleInput")->property("text").toString().trimmed(), QStringLiteral("getblockcount"));
        QVERIFY(qmltest::SetText(m_app, "consoleInput", "zzzznotacommand"));
        QTRY_VERIFY(!qmltest::Find(m_app, "consoleAutocompletePopup")->property("visible").toBool());
        QVERIFY(qmltest::SetText(m_app, "consoleInput", "help get"));
        QVERIFY(qmltest::WaitForVisible(m_app, "consoleAutocompletePopup"));
    }
};

std::unique_ptr<QObject> CreateConsoleTests(BitcoinQmlApplication& app, WalletMode mode)
{
    return std::make_unique<ConsoleIntegrationTests>(app, mode);
}

#include <test_console.moc>
