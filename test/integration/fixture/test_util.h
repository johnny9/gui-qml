// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_UTIL_H
#define BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_UTIL_H

#include <interfaces/node.h>
#include <netbase.h>
#include <qml/bitcoinqmlapplication.h>
#include <qml/models/nodemodel.h>
#include <univalue.h>
#include <test/integration/fixture/test_wait.h>

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QMetaEnum>
#include <QInputMethodEvent>
#include <QSet>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QtQuickTest/quicktest.h>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <utility>

namespace qmltest {
template <typename Method, typename... Args>
auto Call(BitcoinQmlApplication& app, Method method, Args&&... args)
{
    return Async([&] { return std::invoke(method, &app.node(), std::forward<Args>(args)...); });
}

inline bool RpcCommandRunning(BitcoinQmlApplication& app, const std::string& command)
{
    const auto info = Call(app, &interfaces::Node::executeRpc, "getrpcinfo", UniValue{UniValue::VARR}, "");
    for (const auto& active : info.find_value("active_commands").getValues()) {
        if (active.find_value("method").get_str() == command) return true;
    }
    return false;
}

template <typename T> T* Model(BitcoinQmlApplication& app, const char* name)
{
    return qobject_cast<T*>(app.engine().rootContext()->contextProperty(QString::fromLatin1(name)).value<QObject*>());
}

// QML delegates and popup contents can live in the visual tree without
// being QObject children of the window.
inline QObject* Find(QObject* root, const QString& name)
{
    QList<QObject*> matches;
    QList<QObject*> pending{root};
    QSet<QObject*> visited;
    while (!pending.isEmpty()) {
        auto* object = pending.takeLast();
        if (visited.contains(object)) continue;
        visited.insert(object);
        if (object->objectName() == name) matches.append(object);
        pending.append(object->children());
        if (auto* window = qobject_cast<QQuickWindow*>(object)) pending.append(window->contentItem());
        if (auto* item = qobject_cast<QQuickItem*>(object)) {
            for (auto* child : item->childItems()) pending.append(child);
        }
    }
    if (matches.isEmpty()) return nullptr;
    // Stack views retain hidden pages with duplicate names.
    for (auto* object : matches) {
        if (auto* item = qobject_cast<QQuickItem*>(object); item && item->isVisible()) return object;
    }
    for (auto* object : matches) {
        if (object->property("visible").toBool()) return object;
    }
    for (auto* object : matches) {
        for (auto* parent = object->parent(); parent; parent = parent->parent()) {
            if (auto* item = qobject_cast<QQuickItem*>(parent)) {
                if (item->isVisible()) return object;
                break;
            }
        }
    }
    return matches.first();
}

inline QObject* Find(BitcoinQmlApplication& app, const char* name)
{
    const auto object_name = QString::fromLatin1(name);
    // Closed popups and nonvisual models also share names across wallets.
    // Resolve these inside the selected wallet's retained view first.
    auto* host = Find(app.window(), QStringLiteral("walletViewsHost"));
    for (auto* parent = host; parent; parent = parent->parent()) {
        if (parent->metaObject()->indexOfProperty("activeView") < 0) continue;
        if (auto* view = parent->property("activeView").value<QObject*>()) {
            if (auto* object = Find(view, object_name)) return object;
        }
        break;
    }
    return Find(app.window(), object_name);
}

// Check visual and QObject children, including popup overlays and retained pages.
// Return locations only, never the secret itself, in failure diagnostics.
inline QStringList SecretTextLocations(QObject* root, const QString& secret)
{
    QStringList leaks;
    QList<QObject*> pending{root};
    QSet<QObject*> visited;
    while (!pending.isEmpty()) {
        auto* object = pending.takeLast();
        if (!object || visited.contains(object)) continue;
        visited.insert(object);
        for (const char* property : {"text", "errorText", "message"}) {
            if (!secret.isEmpty() && object->property(property).toString().contains(secret)) {
                leaks.append(QStringLiteral("%1.%2").arg(object->objectName(), QString::fromLatin1(property)));
            }
        }
        pending.append(object->children());
        if (auto* window = qobject_cast<QQuickWindow*>(object)) pending.append(window->contentItem());
        if (auto* item = qobject_cast<QQuickItem*>(object)) {
            for (auto* child : item->childItems()) pending.append(child);
        }
    }
    return leaks;
}

inline bool TransitionsFinished(QObject* root)
{
    QList<QObject*> pending{root};
    while (!pending.isEmpty()) {
        auto* object = pending.takeLast();
        if (object->property("busy").toBool()) return false;
        if (object->inherits("QQuickPopup") && object->property("visible").toBool() && !object->property("opened").toBool()) return false;
        pending.append(object->children());
    }
    return true;
}

inline bool WaitForPolish(QQuickItem* item)
{
    if (!item || !item->window()) return false;
    QPointer<QQuickItem> guarded{item};
    SignalWait wait;
    bool dispatched{false};
    // Let deferred QML layout work run once, then wait for rendering only if
    // polish is pending. An already settled item needs no extra frame.
    QMetaObject::invokeMethod(&wait, [&] {
        dispatched = true;
        wait.wake();
    }, Qt::QueuedConnection);
    QObject::connect(item->window(), &QQuickWindow::afterRendering, &wait, &SignalWait::wake, Qt::QueuedConnection);
    wait.destruction(item);
    item->window()->update();
    return wait.run([&] {
        if (!guarded || !dispatched) return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
        return !QQuickTest::qIsPolishScheduled(guarded->window());
#else
        for (auto* current = guarded.data(); current; current = current->parentItem()) {
            if (QQuickTest::qIsPolishScheduled(current)) return false;
        }
        return true;
#endif
    }, 5'000);
}

inline bool ClickWithRetries(QObject* object, bool polished = false)
{
    QPointer<QQuickItem> item = qobject_cast<QQuickItem*>(object);
    if (!item || !item->isVisible() || !item->isEnabled() || (!polished && !WaitForPolish(item)) || !item ||
        item->width() <= 0 || item->height() <= 0) return false;
    const auto click = [&] {
        if (!item || !item->isVisible() || !item->isEnabled() || !item->window()) return false;
        const auto position = item->mapToScene(item->boundingRect().center());
        if (!item->window()->contentItem()->contains(position)) return false;
        QTest::mouseClick(item->window(), Qt::LeftButton, Qt::NoModifier,
                         position.toPoint());
        return true;
    };
    if (item->metaObject()->indexOfSignal("clicked()") < 0) return click();
    QSignalSpy clicked{item, SIGNAL(clicked())};
    const auto pressed = item->metaObject()->indexOfSignal("pressed()") < 0 ? nullptr : std::make_unique<QSignalSpy>(item, SIGNAL(pressed()));
    const auto delivered = [&] { return !clicked.isEmpty(); };
    if (!Await([&] { return TransitionsFinished(item->window()); }, {Ui(item->window())}, 10'000)) return false;
    const QString target = item->objectName();
    int attempts{0};
    const bool success = Await([&] {
        if (delivered() || (pressed && !pressed->isEmpty())) return true;
        ++attempts;
        return click() && (delivered() || (pressed && !pressed->isEmpty()));
    }, {Frame(item->window()), Ui(item->window()), Property(item, "enabled")}, 10'000);
    if (attempts > 1) qWarning() << "Click retries for" << target << "attempts:" << attempts << "delivered:" << delivered();
    return success && delivered();
}

// Retry swallowed input only after a frame or readiness notification. A
// delivered but canceled press still fails without sending another gesture.
inline bool Click(QObject* object)
{
    return ClickWithRetries(object);
}

inline bool OpenSettings(BitcoinQmlApplication& app)
{
    return Click(Find(app, app.window()->property("desktopWalletMode").toBool()
                              ? "desktopWalletSettingsTabButton" : "nodeSettingsButton"));
}

inline QObject* WaitForVisible(BitcoinQmlApplication& app, const char* name)
{
    QObject* object{nullptr};
    const bool ready = Await([&] {
        object = Find(app, name);
        return object && object->property("visible").toBool() && TransitionsFinished(app.window());
    }, {Ui(app.window())}, 10'000);
    if (!ready) qWarning() << "Control did not become ready:" << name << object << "visible:" << (object && object->property("visible").toBool()) << "transitions finished:" << TransitionsFinished(app.window());
    return ready ? object : nullptr;
}

inline bool Click(BitcoinQmlApplication& app, const char* name)
{
    auto* object = WaitForVisible(app, name);
    QPointer<QQuickItem> item = qobject_cast<QQuickItem*>(object);
    if (!item) return false;
    SignalWait wait;
    wait.tree(app.window());
    wait.frame(app.window());
    for (auto* current = item.data(); current; current = current->parentItem()) {
        for (const char* property : {"x", "y", "width", "height", "enabled"}) wait.property(current, property);
        if (current->inherits("QQuickFlickable")) wait.property(current, "contentY");
    }
    const bool ready = wait.run([&] {
        if (!item || !WaitForPolish(item) || !item) return false;
        bool scrolled{false};
        for (auto* parent = item->parentItem(); parent; parent = parent->parentItem()) {
            if (!parent->inherits("QQuickFlickable")) continue;
            const auto rect = item->mapRectToItem(parent, item->boundingRect());
            // Leave a pixel of room: QPoint rounding must not put the gesture
            // on the exclusive bottom edge of a clipped viewport.
            const auto offset = rect.height() > parent->height() - 2
                ? rect.center().y() - parent->height() / 2
                : rect.top() < 1 ? rect.top() - 1 : std::max(0.0, rect.bottom() - parent->height() + 1);
            if (offset != 0) {
                parent->setProperty("contentY", parent->property("contentY").toDouble() + offset);
                scrolled = true;
            }
        }
        if (scrolled || !item->isEnabled() || item->width() <= 0 || item->height() <= 0) return false;
        const auto center = item->boundingRect().center();
        for (auto* parent = item->parentItem(); parent; parent = parent->parentItem()) {
            if (parent->clip() && !parent->contains(item->mapToItem(parent, center))) return false;
        }
        return app.window()->contentItem()->contains(item->mapToItem(app.window()->contentItem(), center));
    }, 5'000);
    if (!ready) qWarning() << "Cannot click" << name;
    return ready && item && ClickWithRetries(item.data(), true);
}

inline bool SetText(BitcoinQmlApplication& app, const char* name, const QString& text)
{
    auto* item = qobject_cast<QQuickItem*>(WaitForVisible(app, name));
    if (!item) return false;
    item->forceActiveFocus();
    if (!QMetaObject::invokeMethod(item, "selectAll")) return false;
    QTest::keyClick(item->window(), Qt::Key_Backspace, Qt::NoModifier, 1);
    QInputMethodEvent edit;
    edit.setCommitString(text);
    QCoreApplication::sendEvent(item, &edit);
    return edit.isAccepted();
}

inline bool SelectSettings(BitcoinQmlApplication& app, const char* section)
{
    auto* settings = qobject_cast<QQuickItem*>(Find(app, "settingsView"));
    if ((!settings || !settings->isVisible()) && !OpenSettings(app)) return false;
    if (!WaitForVisible(app, "settingsView")) return false;
    auto* sidebar = qobject_cast<QQuickItem*>(Find(app, "settingsSidebarList"));
    auto* button = qobject_cast<QQuickItem*>(Find(app, qPrintable(QStringLiteral("settingsSidebar_%1").arg(section))));
    if (!sidebar || !button) return false;
    const auto position_mode = sidebar->metaObject()->enumerator(sidebar->metaObject()->indexOfEnumerator("PositionMode"));
    const int contain = position_mode.keyToValue("Contain");
    const int index = button->parentItem()->property("index").toInt();
    if (contain < 0 || !QMetaObject::invokeMethod(sidebar, "positionViewAtIndex", Q_ARG(int, index), Q_ARG(int, contain))) return false;
    return Await([&] { return sidebar->contains(button->mapToItem(sidebar, button->boundingRect().center())); },
                 {Property(button, "y"), Property(sidebar, "contentY"), Frame(app.window())}) && Click(button);
}

inline bool ReturnToNode(BitcoinQmlApplication& app)
{
    if (app.window()->property("desktopWalletMode").toBool()) return Click(Find(app, "blockClockTabButton"));
    auto* settings = Find(app, "settingsView");
    return !settings || !settings->property("visible").toBool() || Click(Find(app, "settingsDoneButton"));
}
} // namespace qmltest

#endif // BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_UTIL_H
