// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_TESTBRIDGE_H
#define BITCOIN_QML_TEST_TESTBRIDGE_H

#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QString>

#include <vector>

/// Exposes QML object tree to external test scripts over a Unix domain socket.
/// Enabled only when compiled with ENABLE_TEST_AUTOMATION and launched with
/// --test-automation=<socket_path>.
///
/// Supported commands (JSON over newline-delimited stream):
///   {"cmd": "get_current_page"}
///   {"cmd": "get_property", "objectName": "<name>", "prop": "<property>"}
///   {"cmd": "click", "objectName": "<name>"}
///   {"cmd": "set_text", "objectName": "<name>", "text": "<value>"}
///   {"cmd": "wait_for_page", "page": "<objectName>", "timeout": <ms>}
///   {"cmd": "get_text", "objectName": "<name>"}
///   {"cmd": "list_objects"}
class TestBridge : public QObject
{
    Q_OBJECT

public:
    /// Construct a TestBridge listening on @p socket_path.
    /// @p engine must remain valid for the lifetime of this object.
    explicit TestBridge(QQmlApplicationEngine* engine, const QString& socket_path, QObject* parent = nullptr);
    ~TestBridge() override;

private Q_SLOTS:
    void handleNewConnection();
    void handleClientData();
    void handleClientDisconnected();

private:
    /// Find a QObject by objectName, searching the entire QML tree.
    QObject* findObjectByName(const QString& name) const;

    /// Recursively collect all named objects from the QML tree.
    void collectNamedObjects(QObject* root, std::vector<std::pair<QString, QString>>& results) const;

    /// Process a single JSON command and return the JSON response.
    QByteArray processCommand(const QByteArray& json_cmd);

    /// Dispatch individual command handlers.
    QByteArray cmdGetCurrentPage();
    QByteArray cmdGetProperty(const QString& object_name, const QString& prop);
    QByteArray cmdClick(const QString& object_name);
    QByteArray cmdSetText(const QString& object_name, const QString& text);
    QByteArray cmdWaitForPage(const QString& page_name, int timeout_ms);
    QByteArray cmdGetText(const QString& object_name);
    QByteArray cmdListObjects();

    /// Build a JSON error response.
    static QByteArray errorResponse(const QString& message);

    QQmlApplicationEngine* m_engine;
    QLocalServer* m_server;
    std::vector<QLocalSocket*> m_clients;
    QByteArray m_read_buffer;
};

#endif // BITCOIN_QML_TEST_TESTBRIDGE_H
