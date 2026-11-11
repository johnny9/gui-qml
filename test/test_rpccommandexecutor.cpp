// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests for RpcCommandExecutor::RPCParseCommandLine (parse-only mode).
// These are modelled after src/qt/test/rpcnestedtests.cpp.
// All tests use fExecute=false so no live node is required.

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <qml/models/rpccommandexecutor.h>
#include <util/translation.h>

#include <string>

#ifndef BITCOINQML_NO_TEST_MAIN
const TranslateFn G_TRANSLATION_FUN{nullptr};
#endif

class RpcCommandExecutorTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parseSpaceSeparated();
    void parseFunctionalNotation();
    void parseNestedCommands();
    void parseQuotedArguments();
    void parseSubscriptSyntax();
    void parseUnbalancedQuoteFails();
    void parseImplicitlyClosedParenSucceeds();
    void sensitiveCmdWalletPassphrase();
    void sensitiveCmdNestedWalletPassphrase();
    void sensitiveCmdSignMessage();
    void sensitiveCmdEncryptWallet();
    void sensitiveCmdNonSensitiveUnredacted();
    void secretCommandMatrix_data();
    void secretCommandMatrix();
    void pinnedCoreRedactionPolicy();
};

void RpcCommandExecutorTests::parseSpaceSeparated()
{
    std::string result;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblockhash 0", false));
}

void RpcCommandExecutorTests::parseFunctionalNotation()
{
    std::string result;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblockhash(0)", false));
}

void RpcCommandExecutorTests::parseNestedCommands()
{
    std::string result;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblock(getblockhash(0) 1)", false));
}

void RpcCommandExecutorTests::parseQuotedArguments()
{
    std::string result;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "signmessage \"addr\" \"msg\"", false));
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblockhash '0'", false));
}

void RpcCommandExecutorTests::parseSubscriptSyntax()
{
    std::string result;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(
        nullptr, result, "getblock(getblockhash(0) 1)[tx]", false));
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(
        nullptr, result, "getblock(getblockhash(0),1)[tx][0]", false));
}

void RpcCommandExecutorTests::parseUnbalancedQuoteFails()
{
    std::string result;
    QVERIFY(!RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblockhash '0", false));
    QVERIFY(!RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblockhash \"0", false));
}

void RpcCommandExecutorTests::parseImplicitlyClosedParenSucceeds()
{
    std::string result;
    // An unbalanced open paren is implicitly closed by the trailing newline the
    // parser appends, so it parses successfully (same behaviour as the original
    // Qt GUI console). Only unbalanced quotes produce a parse error.
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblockhash(0", false));
}

void RpcCommandExecutorTests::sensitiveCmdWalletPassphrase()
{
    std::string result, filtered;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "walletpassphrase foo bar", false, &filtered));
    QVERIFY(!QString::fromStdString(filtered).contains("foo"));
    QVERIFY(QString::fromStdString(filtered).contains(QString::fromUtf8("(\xe2\x80\xa6)"))); // (…)
}

void RpcCommandExecutorTests::sensitiveCmdNestedWalletPassphrase()
{
    std::string result, filtered;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "walletpassphrase(help())", false, &filtered));
    QVERIFY(QString::fromStdString(filtered).contains("(\xe2\x80\xa6)"));
}

void RpcCommandExecutorTests::sensitiveCmdSignMessage()
{
    std::string result, filtered;
    // Use argument strings that are not substrings of the command name itself.
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "signmessagewithprivkey myprivatearg mymsgarg", false, &filtered));
    QVERIFY(!QString::fromStdString(filtered).contains("myprivatearg"));
    QVERIFY(!QString::fromStdString(filtered).contains("mymsgarg"));
    QVERIFY(QString::fromStdString(filtered).contains("(\xe2\x80\xa6)"));
}

void RpcCommandExecutorTests::sensitiveCmdEncryptWallet()
{
    std::string result, filtered;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "encryptwallet mypassword", false, &filtered));
    QVERIFY(!QString::fromStdString(filtered).contains("mypassword"));
    QVERIFY(QString::fromStdString(filtered).contains("(\xe2\x80\xa6)"));
}

void RpcCommandExecutorTests::sensitiveCmdNonSensitiveUnredacted()
{
    std::string result, filtered;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblockcount", false, &filtered));
    // Non-sensitive command must not be redacted.
    QCOMPARE(QString::fromStdString(filtered), QString("getblockcount"));
}

void RpcCommandExecutorTests::secretCommandMatrix_data()
{
    QTest::addColumn<QString>("command"); QTest::addColumn<QString>("expected");
    const QString secret = "unique-rpc-secret-92";
    const QString redacted = QString::fromUtf8("(…)");
    for (const QString& name : {QString("createwallet"), QString("migratewallet"), QString("addhdkey"),
                               QString("createwalletdescriptor"), QString("walletpassphrase"),
                               QString("walletpassphrasechange"), QString("encryptwallet"),
                               QString("signmessagewithprivkey"), QString("signrawtransactionwithkey")}) {
        QTest::newRow(qPrintable(name + "-space")) << name + " " + secret << name + redacted;
        QTest::newRow(qPrintable(name + "-functional")) << name + "('" + secret + "')" << name + redacted;
        QTest::newRow(qPrintable(name + "-uppercase")) << name.toUpper() + " " + secret << name.toUpper() + redacted;
        QTest::newRow(qPrintable(name + "-nested")) << "echo(" + name + "('" + secret + "'))" << "echo(" + name + redacted + ")";
    }
}

void RpcCommandExecutorTests::secretCommandMatrix()
{
    QFETCH(QString, command); QFETCH(QString, expected);
    std::string result, filtered;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, command.toStdString() + "\n", false, &filtered));
    const QString display = QString::fromStdString(filtered).trimmed();
    QVERIFY2(!display.contains("unique-rpc-secret-92"), qPrintable(display));
    QCOMPARE(display, expected);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(RpcCommandExecutorTests)
#else
QTEST_MAIN(RpcCommandExecutorTests)
#endif
#include <test_rpccommandexecutor.moc>

// Keep the app policy at least as strict as the pinned Widgets console policy.
// Reading its source makes a future submodule update demand an explicit review.
void RpcCommandExecutorTests::pinnedCoreRedactionPolicy()
{
    QFile source(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("../bitcoin/src/qt/rpcconsole.cpp"));
    QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.errorString()));
    const QString text = QString::fromUtf8(source.readAll());
    const auto declaration = QRegularExpression("const QStringList historyFilter = QStringList\\(\\)([^;]+);").match(text);
    QVERIFY(declaration.hasMatch());
    auto names = QRegularExpression("\"([a-z]+)\"").globalMatch(declaration.captured(1));
    QStringList commands;
    while (names.hasNext()) commands.append(names.next().captured(1));
    const QStringList reviewed_policy{QStringLiteral("addhdkey"), QStringLiteral("createwallet"),
                                      QStringLiteral("createwalletdescriptor"), QStringLiteral("migratewallet"),
                                      QStringLiteral("signmessagewithprivkey"), QStringLiteral("signrawtransactionwithkey"),
                                      QStringLiteral("walletpassphrase"), QStringLiteral("walletpassphrasechange"),
                                      QStringLiteral("encryptwallet")};
    QCOMPARE(commands, reviewed_policy);
    for (const auto& command : commands) {
        std::string result, filtered;
        const QString line = command + " unique-policy-secret-92\n";
        QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, line.toStdString(), false, &filtered));
        QVERIFY2(!QString::fromStdString(filtered).contains("unique-policy-secret-92"), qPrintable(command));
    }
}
