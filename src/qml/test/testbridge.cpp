// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/testbridge.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <QVariant>

#include <algorithm>

TestBridge::TestBridge(QQmlApplicationEngine* engine, const QString& socket_path, QObject* parent)
    : QObject(parent), m_engine(engine), m_server(new QLocalServer(this))
{
    // Remove any stale socket file from a previous run.
    QLocalServer::removeServer(socket_path);

    if (!m_server->listen(socket_path)) {
        qWarning("TestBridge: failed to listen on %s: %s",
                 qPrintable(socket_path),
                 qPrintable(m_server->errorString()));
        return;
    }

    connect(m_server, &QLocalServer::newConnection, this, &TestBridge::handleNewConnection);
    qInfo("TestBridge: listening on %s", qPrintable(socket_path));
}

TestBridge::~TestBridge()
{
    for (auto* client : m_clients) {
        client->disconnectFromServer();
        client->deleteLater();
    }
    m_server->close();
}

void TestBridge::handleNewConnection()
{
    while (QLocalSocket* client = m_server->nextPendingConnection()) {
        m_clients.push_back(client);
        connect(client, &QLocalSocket::readyRead, this, &TestBridge::handleClientData);
        connect(client, &QLocalSocket::disconnected, this, &TestBridge::handleClientDisconnected);
        qInfo("TestBridge: client connected");
    }
}

void TestBridge::handleClientData()
{
    auto* client = qobject_cast<QLocalSocket*>(sender());
    if (!client) return;

    m_read_buffer.append(client->readAll());

    // Process newline-delimited JSON commands.
    int newline_pos;
    while ((newline_pos = m_read_buffer.indexOf('\n')) != -1) {
        QByteArray line = m_read_buffer.left(newline_pos);
        m_read_buffer.remove(0, newline_pos + 1);

        if (line.trimmed().isEmpty()) continue;

        QByteArray response = processCommand(line);
        response.append('\n');
        client->write(response);
        client->flush();
    }
}

void TestBridge::handleClientDisconnected()
{
    auto* client = qobject_cast<QLocalSocket*>(sender());
    if (!client) return;

    auto it = std::find(m_clients.begin(), m_clients.end(), client);
    if (it != m_clients.end()) {
        m_clients.erase(it);
    }
    client->deleteLater();
    qInfo("TestBridge: client disconnected");
}

QObject* TestBridge::findObjectByName(const QString& name) const
{
    for (QObject* root : m_engine->rootObjects()) {
        if (root->objectName() == name) return root;

        // Collect all children with this name.
        QList<QObject*> matches = root->findChildren<QObject*>(name);
        if (matches.isEmpty()) continue;

        // Prefer a visible QQuickItem (important when StackView keeps
        // hidden pages in the tree with duplicate objectNames).
        for (QObject* obj : matches) {
            auto* item = qobject_cast<QQuickItem*>(obj);
            if (item && item->isVisible()) return obj;
        }

        // Fall back to the first match.
        return matches.first();
    }
    return nullptr;
}

void TestBridge::collectNamedObjects(QObject* root, std::vector<std::pair<QString, QString>>& results) const
{
    if (!root) return;
    if (!root->objectName().isEmpty()) {
        results.emplace_back(root->objectName(),
                             QString::fromLatin1(root->metaObject()->className()));
    }
    for (QObject* child : root->children()) {
        collectNamedObjects(child, results);
    }
    // Also traverse visual children for QQuickItem-based trees.
    auto* item = qobject_cast<QQuickItem*>(root);
    if (item) {
        for (QQuickItem* visual_child : item->childItems()) {
            // Avoid duplicates — only traverse if not already a QObject child.
            if (visual_child->parent() != root) {
                collectNamedObjects(visual_child, results);
            }
        }
    }
}

QByteArray TestBridge::processCommand(const QByteArray& json_cmd)
{
    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(json_cmd, &parse_error);
    if (doc.isNull()) {
        return errorResponse(QStringLiteral("JSON parse error: %1").arg(parse_error.errorString()));
    }

    QJsonObject obj = doc.object();
    QString cmd = obj.value(QStringLiteral("cmd")).toString();

    if (cmd == QLatin1String("get_current_page")) {
        return cmdGetCurrentPage();
    } else if (cmd == QLatin1String("get_property")) {
        return cmdGetProperty(
            obj.value(QStringLiteral("objectName")).toString(),
            obj.value(QStringLiteral("prop")).toString());
    } else if (cmd == QLatin1String("click")) {
        return cmdClick(obj.value(QStringLiteral("objectName")).toString());
    } else if (cmd == QLatin1String("set_text")) {
        return cmdSetText(
            obj.value(QStringLiteral("objectName")).toString(),
            obj.value(QStringLiteral("text")).toString());
    } else if (cmd == QLatin1String("wait_for_page")) {
        return cmdWaitForPage(
            obj.value(QStringLiteral("page")).toString(),
            obj.value(QStringLiteral("timeout")).toInt(5000));
    } else if (cmd == QLatin1String("get_text")) {
        return cmdGetText(obj.value(QStringLiteral("objectName")).toString());
    } else if (cmd == QLatin1String("list_objects")) {
        return cmdListObjects();
    }

    return errorResponse(QStringLiteral("Unknown command: %1").arg(cmd));
}

QByteArray TestBridge::cmdGetCurrentPage()
{
    // Walk the visual tree looking for StackView instances (which have both
    // "currentItem" and "depth" properties).  Drill into nested StackViews
    // (e.g. OnboardingWizard is a PageStack inside the main PageStack) to
    // return the deepest page.  We check for "depth" to distinguish StackView
    // from StackLayout, which also has "currentItem" but is not a page stack.
    for (QObject* root : m_engine->rootObjects()) {
        auto* window = qobject_cast<QQuickWindow*>(root);
        if (!window) continue;

        // Breadth-first search through the visual item tree.
        std::vector<QQuickItem*> queue;
        queue.push_back(window->contentItem());

        QObject* deepest_page = nullptr;
        QObject* deepest_named_page = nullptr;

        while (!queue.empty()) {
            QQuickItem* item = queue.back();
            queue.pop_back();
            if (!item) continue;

            // Only consider items that look like a StackView: they must
            // have both "currentItem" and "depth" properties.
            QVariant depth = item->property("depth");
            QVariant current = item->property("currentItem");
            if (depth.isValid() && current.isValid()) {
                QObject* current_obj = current.value<QObject*>();
                if (current_obj) {
                    deepest_page = current_obj;
                    if (!current_obj->objectName().isEmpty()) {
                        deepest_named_page = current_obj;
                    }
                    // Push the current item itself back into the queue
                    // so that if it is a nested StackView (e.g.
                    // OnboardingWizard) it gets checked for its own
                    // currentItem on the next iteration.  Its children
                    // will be explored when it is popped and either
                    // matched (StackView) or falls through to the
                    // childItems loop below.
                    auto* current_item = qobject_cast<QQuickItem*>(current_obj);
                    if (current_item) {
                        queue.push_back(current_item);
                    }
                    continue;
                }
            }

            for (QQuickItem* child : item->childItems()) {
                queue.push_back(child);
            }
        }

        // Prefer the deepest page that has an objectName.  Fall back
        // to the class name of the absolute deepest page otherwise.
        QObject* result_page = deepest_named_page ? deepest_named_page : deepest_page;
        if (result_page) {
            QString name = result_page->objectName();
            if (name.isEmpty()) {
                name = QString::fromLatin1(result_page->metaObject()->className());
            }
            QJsonObject resp;
            resp[QStringLiteral("page")] = name;
            return QJsonDocument(resp).toJson(QJsonDocument::Compact);
        }
    }
    return errorResponse(QStringLiteral("Could not determine current page"));
}

QByteArray TestBridge::cmdGetProperty(const QString& object_name, const QString& prop)
{
    if (object_name.isEmpty() || prop.isEmpty()) {
        return errorResponse(QStringLiteral("objectName and prop are required"));
    }

    QObject* obj = findObjectByName(object_name);
    if (!obj) {
        return errorResponse(QStringLiteral("Object not found: %1").arg(object_name));
    }

    QVariant value = obj->property(prop.toLatin1().constData());
    if (!value.isValid()) {
        return errorResponse(QStringLiteral("Property not found: %1.%2").arg(object_name, prop));
    }

    QJsonObject resp;
    resp[QStringLiteral("value")] = QJsonValue::fromVariant(value);
    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}

QByteArray TestBridge::cmdClick(const QString& object_name)
{
    if (object_name.isEmpty()) {
        return errorResponse(QStringLiteral("objectName is required"));
    }

    QObject* obj = findObjectByName(object_name);
    if (!obj) {
        return errorResponse(QStringLiteral("Object not found: %1").arg(object_name));
    }

    // Try invoking the clicked() signal or onClicked handler.
    // For QQuickItem-based controls, we can also simulate a mouse click.
    auto* item = qobject_cast<QQuickItem*>(obj);

    // First, try to find and invoke a "clicked" signal.
    const QMetaObject* meta = obj->metaObject();
    int clicked_index = meta->indexOfSignal("clicked()");
    if (clicked_index >= 0) {
        meta->method(clicked_index).invoke(obj, Qt::DirectConnection);
        QJsonObject resp;
        resp[QStringLiteral("ok")] = true;
        return QJsonDocument(resp).toJson(QJsonDocument::Compact);
    }

    // Fallback: for QQuickItems, try the "toggle" or "trigger" method
    // or directly invoke via AbstractButton's clicked().
    int toggle_index = meta->indexOfMethod("toggle()");
    if (toggle_index >= 0) {
        meta->method(toggle_index).invoke(obj, Qt::DirectConnection);
        QJsonObject resp;
        resp[QStringLiteral("ok")] = true;
        return QJsonDocument(resp).toJson(QJsonDocument::Compact);
    }

    // Last resort: if it's a QQuickItem, synthesize pointer events.
    if (item) {
        QQuickWindow* window = item->window();
        if (window) {
            QPointF center = item->mapToScene(
                QPointF(item->width() / 2.0, item->height() / 2.0));
            QPoint pos = center.toPoint();

            QMouseEvent press(QEvent::MouseButtonPress, pos, window->mapToGlobal(pos),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, pos, window->mapToGlobal(pos),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);

            QCoreApplication::sendEvent(window, &press);
            QCoreApplication::sendEvent(window, &release);

            QJsonObject resp;
            resp[QStringLiteral("ok")] = true;
            return QJsonDocument(resp).toJson(QJsonDocument::Compact);
        }
    }

    return errorResponse(QStringLiteral("Cannot click object: %1").arg(object_name));
}

QByteArray TestBridge::cmdSetText(const QString& object_name, const QString& text)
{
    if (object_name.isEmpty()) {
        return errorResponse(QStringLiteral("objectName is required"));
    }

    QObject* obj = findObjectByName(object_name);
    if (!obj) {
        return errorResponse(QStringLiteral("Object not found: %1").arg(object_name));
    }

    // Try "text" property first (covers TextField, TextInput, TextArea, etc.)
    if (obj->property("text").isValid()) {
        obj->setProperty("text", text);
        QJsonObject resp;
        resp[QStringLiteral("ok")] = true;
        return QJsonDocument(resp).toJson(QJsonDocument::Compact);
    }

    return errorResponse(QStringLiteral("Object %1 has no 'text' property").arg(object_name));
}

QByteArray TestBridge::cmdWaitForPage(const QString& page_name, int timeout_ms)
{
    if (page_name.isEmpty()) {
        return errorResponse(QStringLiteral("page is required"));
    }

    // Poll for the page to appear. We process events between checks.
    const int poll_interval_ms = 50;
    int elapsed = 0;

    while (elapsed < timeout_ms) {
        // Check if the requested page/object exists and is visible.
        QObject* obj = findObjectByName(page_name);
        if (obj) {
            auto* item = qobject_cast<QQuickItem*>(obj);
            if (!item || item->isVisible()) {
                QJsonObject resp;
                resp[QStringLiteral("ok")] = true;
                return QJsonDocument(resp).toJson(QJsonDocument::Compact);
            }
        }

        // Process pending events to let the UI update.
        QCoreApplication::processEvents(QEventLoop::AllEvents, poll_interval_ms);
        QThread::msleep(poll_interval_ms);
        elapsed += poll_interval_ms;
    }

    return errorResponse(QStringLiteral("Timed out waiting for page: %1").arg(page_name));
}

QByteArray TestBridge::cmdGetText(const QString& object_name)
{
    if (object_name.isEmpty()) {
        return errorResponse(QStringLiteral("objectName is required"));
    }

    QObject* obj = findObjectByName(object_name);
    if (!obj) {
        return errorResponse(QStringLiteral("Object not found: %1").arg(object_name));
    }

    QVariant text_val = obj->property("text");
    if (!text_val.isValid()) {
        return errorResponse(QStringLiteral("Object %1 has no 'text' property").arg(object_name));
    }

    QJsonObject resp;
    resp[QStringLiteral("text")] = text_val.toString();
    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}

QByteArray TestBridge::cmdListObjects()
{
    std::vector<std::pair<QString, QString>> objects;
    for (QObject* root : m_engine->rootObjects()) {
        collectNamedObjects(root, objects);
    }

    QJsonArray arr;
    for (const auto& [name, class_name] : objects) {
        QJsonObject entry;
        entry[QStringLiteral("objectName")] = name;
        entry[QStringLiteral("className")] = class_name;
        arr.append(entry);
    }

    QJsonObject resp;
    resp[QStringLiteral("objects")] = arr;
    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}

QByteArray TestBridge::errorResponse(const QString& message)
{
    QJsonObject resp;
    resp[QStringLiteral("error")] = message;
    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}
