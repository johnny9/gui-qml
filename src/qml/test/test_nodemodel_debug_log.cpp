// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mocknode.h>
#include <net_processing.h>
#include <qml/models/nodemodel.h>

#include <logging.h>
#include <util/fs.h>

#include <QTemporaryFile>
#include <QVariantList>
#include <QVariantMap>

using ::testing::NiceMock;
using ::testing::Return;

// RAII guard that saves and restores LogInstance().m_file_path.
// Guarantees restoration even when a QVERIFY/QCOMPARE assertion fires.
struct LogPathGuard {
    fs::path saved;
    LogPathGuard() : saved{LogInstance().m_file_path} {}
    ~LogPathGuard() { LogInstance().m_file_path = saved; }
};

class NodeModelDebugLogTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void debugLogPath_matchesLogInstance();
    void openDebugLogFile_missingFile_returnsFalse();
    void openDebugLogFile_missingFile_setsError();
    void openDebugLogFile_errorClearedAfterMissingFile();
    void debugLogLines_missingFile_returnsEmpty();
    void debugLogLines_missingFile_setsError();
    void debugLogLines_existingFile_returnsLines();
    void debugLogLines_existingFile_clearsError();
    void debugLogLines_truncatesAtMaxLines();
};

void NodeModelDebugLogTests::debugLogPath_matchesLogInstance()
{
    NiceMock<MockNode> mock_node;
    NodeModel model(mock_node);

    const QString expected = QString::fromStdString(LogInstance().m_file_path.utf8string());
    QCOMPARE(model.debugLogPath(), expected);
}

void NodeModelDebugLogTests::openDebugLogFile_missingFile_returnsFalse()
{
    NiceMock<MockNode> mock_node;
    NodeModel model(mock_node);

    LogPathGuard guard;
    LogInstance().m_file_path = fs::path{"/tmp/this_file_does_not_exist_qml_test_12345.log"};

    const bool result = model.openDebugLogFile();
    QVERIFY(!result);
}

void NodeModelDebugLogTests::openDebugLogFile_missingFile_setsError()
{
    NiceMock<MockNode> mock_node;
    NodeModel model(mock_node);

    LogPathGuard guard;
    LogInstance().m_file_path = fs::path{"/tmp/this_file_does_not_exist_qml_test_12345.log"};

    model.openDebugLogFile();
    QVERIFY(!model.debugLogOpenError().isEmpty());
    QVERIFY(model.debugLogOpenError().contains("not found", Qt::CaseInsensitive));
}

void NodeModelDebugLogTests::openDebugLogFile_errorClearedAfterMissingFile()
{
    NiceMock<MockNode> mock_node;
    NodeModel model(mock_node);

    LogPathGuard guard;

    // First: trigger an error with a missing file
    LogInstance().m_file_path = fs::path{"/tmp/this_file_does_not_exist_qml_test_12345.log"};
    model.openDebugLogFile();
    QVERIFY(!model.debugLogOpenError().isEmpty());

    // Second: point at a real temp file and use debugLogLines() (which does not
    // depend on QDesktopServices) to verify the error clears on a successful read.
    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    tmp.write("line1\n");
    tmp.flush();
    LogInstance().m_file_path = fs::PathFromString(tmp.fileName().toStdString());

    model.debugLogLines(1);
    QVERIFY(model.debugLogOpenError().isEmpty());
}

void NodeModelDebugLogTests::debugLogLines_missingFile_returnsEmpty()
{
    NiceMock<MockNode> mock_node;
    NodeModel model(mock_node);

    LogPathGuard guard;
    LogInstance().m_file_path = fs::path{"/tmp/this_file_does_not_exist_qml_test_12345.log"};

    const QVariantList result = model.debugLogLines(10000);
    QVERIFY(result.isEmpty());
}

void NodeModelDebugLogTests::debugLogLines_missingFile_setsError()
{
    NiceMock<MockNode> mock_node;
    NodeModel model(mock_node);

    LogPathGuard guard;
    LogInstance().m_file_path = fs::path{"/tmp/this_file_does_not_exist_qml_test_12345.log"};

    model.debugLogLines(10000);
    QVERIFY(!model.debugLogOpenError().isEmpty());
    QVERIFY(model.debugLogOpenError().contains("not found", Qt::CaseInsensitive));
}

void NodeModelDebugLogTests::debugLogLines_existingFile_returnsLines()
{
    NiceMock<MockNode> mock_node;
    NodeModel model(mock_node);

    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    tmp.write("line1\nline2\nline3\n");
    tmp.flush();

    LogPathGuard guard;
    LogInstance().m_file_path = fs::PathFromString(tmp.fileName().toStdString());

    const QVariantList result = model.debugLogLines(10000);
    QCOMPARE(result.size(), 3);
    QCOMPARE(result.at(0).toMap().value(QStringLiteral("raw")).toString(), QString("line1"));
}

void NodeModelDebugLogTests::debugLogLines_existingFile_clearsError()
{
    NiceMock<MockNode> mock_node;
    NodeModel model(mock_node);

    LogPathGuard guard;

    // First: trigger an error with a missing file
    LogInstance().m_file_path = fs::path{"/tmp/this_file_does_not_exist_qml_test_12345.log"};
    model.debugLogLines(10000);
    QVERIFY(!model.debugLogOpenError().isEmpty());

    // Second: point at a real temp file — error should clear
    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    LogInstance().m_file_path = fs::PathFromString(tmp.fileName().toStdString());

    model.debugLogLines(10000);
    QVERIFY(model.debugLogOpenError().isEmpty());
}

void NodeModelDebugLogTests::debugLogLines_truncatesAtMaxLines()
{
    NiceMock<MockNode> mock_node;
    NodeModel model(mock_node);

    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    // Write 10 lines; we will request only the last 5.
    for (int i = 0; i < 10; ++i) {
        tmp.write(QStringLiteral("line%1\n").arg(i).toUtf8());
    }
    tmp.flush();

    LogPathGuard guard;
    LogInstance().m_file_path = fs::PathFromString(tmp.fileName().toStdString());

    const QVariantList result = model.debugLogLines(5);
    QCOMPARE(result.size(), 5);
    // The last 5 lines are line5 through line9.
    QCOMPARE(result.at(0).toMap().value(QStringLiteral("raw")).toString(), QString("line5"));
    QCOMPARE(result.at(4).toMap().value(QStringLiteral("raw")).toString(), QString("line9"));
}

int RunNodeModelDebugLogTests(int argc, char* argv[])
{
    NodeModelDebugLogTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#ifndef BITCOINQML_NO_TEST_MAIN
QTEST_MAIN(NodeModelDebugLogTests)
#endif
#include "test_nodemodel_debug_log.moc"
