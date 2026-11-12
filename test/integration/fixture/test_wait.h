// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_WAIT_H
#define BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_WAIT_H

#include <QEventLoop>
#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QSet>
#include <QThread>
#include <QTimer>

#include <cstdlib>
#include <functional>
#include <future>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

class QAbstractItemModel;
class QQuickWindow;

namespace qmltest {
class SignalWait : public QObject
{
    Q_OBJECT
    QEventLoop m_loop;
    QTimer m_timeout;
    std::function<bool()> m_condition;
    bool m_ready{false};
    bool m_destroyed{false};
    bool m_queued{false};
    bool m_checking{false};
    bool m_recheck{false};
    QSet<QObject*> m_watched;

public:
    void property(QObject* object, const char* name);
    void rows(QAbstractItemModel* model);
    void tree(QObject* root);
    void frame(QQuickWindow* window);
    void destruction(QObject* object);
    bool run(std::function<bool()> condition, int timeout);

public Q_SLOTS:
    void wake();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void check();
};

using Notification = std::function<void(SignalWait&)>;

inline Notification Property(QObject* object, const char* name)
{
    return [object = QPointer<QObject>{object}, name](SignalWait& wait) { wait.property(object, name); };
}

inline Notification Rows(QAbstractItemModel* model)
{
    return [model](SignalWait& wait) { wait.rows(model); };
}

inline Notification Ui(QObject* root)
{
    return [root](SignalWait& wait) { wait.tree(root); };
}

inline Notification Frame(QQuickWindow* window)
{
    return [window](SignalWait& wait) { wait.frame(window); };
}

inline Notification Destroyed(QObject* object)
{
    return [object](SignalWait& wait) { wait.destruction(object); };
}

inline bool Await(std::function<bool()> condition, std::initializer_list<Notification> notifications, int timeout = 5'000)
{
    SignalWait wait;
    for (const auto& subscribe : notifications) subscribe(wait);
    return wait.run(std::move(condition), timeout);
}

inline bool AwaitSignal(QSignalSpy& spy, int timeout = 5'000)
{
    return spy.isValid() && (!spy.isEmpty() || spy.wait(timeout));
}

// The worker publishes completion through QThread::finished. Keep result and
// exception transport in a future, without repeatedly checking its readiness.
template <typename Function> auto Async(Function&& function)
{
    using Result = std::invoke_result_t<Function>;
    std::packaged_task<Result()> task{std::forward<Function>(function)};
    auto result = task.get_future();
    std::unique_ptr<QThread> worker{QThread::create([task = std::move(task)]() mutable { task(); })};
    SignalWait finished;
    QObject::connect(worker.get(), &QThread::finished, &finished, &SignalWait::wake);
    worker->start();
    if (!finished.run([&] { return worker->isFinished(); }, 30'000)) {
        // A timed-out worker can still borrow the application. The runner owns
        // this child process and retains its diagnostics; do not destroy those
        // borrowed objects or block indefinitely in a thread destructor.
        qCritical("Integration backend operation timed out after 30000 ms");
        std::_Exit(EXIT_FAILURE);
    }
    worker->wait();
    return result.get();
}
} // namespace qmltest

#define QVERIFY_AWAIT(condition, ...) \
    QVERIFY2(qmltest::Await([&] { return static_cast<bool>(condition); }, {__VA_ARGS__}), "Waiting for " #condition)

#define QCOMPARE_AWAIT(actual, expected, ...) \
    do { \
        QVERIFY_AWAIT((actual) == (expected), __VA_ARGS__); \
        QCOMPARE(actual, expected); \
    } while (false)

#define QVERIFY_AWAIT_TIMEOUT(condition, timeout, ...) \
    QVERIFY2(qmltest::Await([&] { return static_cast<bool>(condition); }, {__VA_ARGS__}, timeout), "Waiting for " #condition)

#define QCOMPARE_AWAIT_TIMEOUT(actual, expected, timeout, ...) \
    do { \
        QVERIFY_AWAIT_TIMEOUT((actual) == (expected), timeout, __VA_ARGS__); \
        QCOMPARE(actual, expected); \
    } while (false)

#endif // BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_WAIT_H
