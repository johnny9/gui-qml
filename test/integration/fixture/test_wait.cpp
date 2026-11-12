// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/fixture/test_wait.h>

#include <QAbstractItemModel>
#include <QChildEvent>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>

namespace qmltest {
void SignalWait::wake()
{
    if (m_queued) return;
    m_queued = true;
    // Evaluate after the notifying setter finishes, including dependent QML
    // bindings. Coalesce notifications from a model reset or a layout pass.
    QMetaObject::invokeMethod(this, [this] {
        m_queued = false;
        check();
    }, Qt::QueuedConnection);
}

void SignalWait::check()
{
    if (!m_condition || m_destroyed || m_ready) return;
    if (m_checking) {
        m_recheck = true;
        return;
    }
    m_checking = true;
    m_recheck = false;
    m_ready = m_condition();
    m_checking = false;
    if (m_ready) m_loop.quit();
    else if (m_recheck) wake();
}

bool SignalWait::run(std::function<bool()> condition, int timeout)
{
    m_condition = std::move(condition);
    check();
    if (m_ready || m_destroyed) return m_ready;
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, &m_loop, &QEventLoop::quit);
    m_timeout.start(timeout);
    m_loop.exec();
    return m_ready && !m_destroyed;
}

void SignalWait::property(QObject* object, const char* name)
{
    if (!object) {
        m_destroyed = true;
        return;
    }
    const auto* meta = object->metaObject();
    const int index = meta->indexOfProperty(name);
    if (index < 0 || !meta->property(index).hasNotifySignal()) {
        qWarning() << "Property has no change notification:" << object << name;
        m_destroyed = true;
        return;
    }
    const auto slot = metaObject()->method(metaObject()->indexOfSlot("wake()"));
    connect(object, meta->property(index).notifySignal(), this, slot);
    connect(object, &QObject::destroyed, this, [this] {
        m_destroyed = true;
        m_loop.quit();
    });
}

void SignalWait::rows(QAbstractItemModel* model)
{
    connect(model, &QAbstractItemModel::rowsInserted, this, &SignalWait::wake);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &SignalWait::wake);
    connect(model, &QAbstractItemModel::modelReset, this, &SignalWait::wake);
    connect(model, &QAbstractItemModel::layoutChanged, this, &SignalWait::wake);
    connect(model, &QAbstractItemModel::dataChanged, this, &SignalWait::wake);
    connect(model, &QObject::destroyed, this, [this] {
        m_destroyed = true;
        m_loop.quit();
    });
}

void SignalWait::destruction(QObject* object)
{
    if (object) connect(object, &QObject::destroyed, this, &SignalWait::wake);
}

void SignalWait::frame(QQuickWindow* window)
{
    window->installEventFilter(this);
    connect(window, &QQuickWindow::afterRendering, this, &SignalWait::wake, Qt::QueuedConnection);
    connect(window, &QQuickWindow::afterAnimating, this, &SignalWait::wake);
    window->update();
}

void SignalWait::tree(QObject* root)
{
    if (!root) return;
    QList<QObject*> pending{root};
    QSet<QObject*> visited;
    const auto slot = metaObject()->method(metaObject()->indexOfSlot("wake()"));
    while (!pending.isEmpty()) {
        auto* object = pending.takeLast();
        if (visited.contains(object)) continue;
        visited.insert(object);
        const bool watching = m_watched.contains(object);
        m_watched.insert(object);
        if (!watching) {
            object->installEventFilter(this);
            connect(object, &QObject::destroyed, this, [this, object] { m_watched.remove(object); wake(); });
            const auto* meta = object->metaObject();
            for (const char* name : {"visible", "opened", "busy", "activeView"}) {
                const int index = meta->indexOfProperty(name);
                if (index >= 0 && meta->property(index).hasNotifySignal()) {
                    connect(object, meta->property(index).notifySignal(), this, slot, Qt::UniqueConnection);
                }
            }
            if (auto* item = qobject_cast<QQuickItem*>(object)) {
                connect(item, &QQuickItem::childrenChanged, this, [this, item] { tree(item); wake(); });
            }
        }
        pending.append(object->children());
        if (auto* window = qobject_cast<QQuickWindow*>(object)) pending.append(window->contentItem());
        if (auto* item = qobject_cast<QQuickItem*>(object)) {
            for (auto* child : item->childItems()) pending.append(child);
        }
    }
}

bool SignalWait::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ChildAdded || event->type() == QEvent::ChildPolished) {
        // ChildAdded can arrive before construction has installed its metaobject.
        QMetaObject::invokeMethod(this, [this, object = QPointer<QObject>{watched}] {
            if (object) tree(object);
            wake();
        }, Qt::QueuedConnection);
    } else if (event->type() == QEvent::ChildRemoved || event->type() == QEvent::Expose) {
        wake();
    }
    return false;
}
} // namespace qmltest
