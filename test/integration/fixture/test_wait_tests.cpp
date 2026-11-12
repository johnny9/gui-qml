// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/fixture/test_wait.h>

#include <QTest>

#include <stdexcept>

class SignalWaitTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void capturesSynchronousSignal()
    {
        QObject object;
        QSignalSpy changed{&object, &QObject::objectNameChanged};
        object.setObjectName("ready");
        QVERIFY(qmltest::AwaitSignal(changed));
        QCOMPARE(changed.count(), 1);
    }

    void followsIntermediateValues()
    {
        QObject object;
        QTimer::singleShot(0, &object, [&] {
            object.setObjectName("pending");
            QTimer::singleShot(0, &object, [&] { object.setObjectName("ready"); });
        });
        QVERIFY(qmltest::Await([&] { return object.objectName() == "ready"; }, {qmltest::Property(&object, "objectName")}));
    }

    void unrelatedSignalsDoNotPollThePredicate()
    {
        QObject object;
        QObject other;
        int checks{0};
        QTimer::singleShot(0, &other, [&] {
            other.setObjectName("unrelated");
            QTimer::singleShot(0, &object, [&] {
                QCOMPARE(checks, 1);
                object.setObjectName("ready");
            });
        });
        QVERIFY(qmltest::Await([&] { ++checks; return object.objectName() == "ready"; }, {qmltest::Property(&object, "objectName")}));
        QCOMPARE(checks, 2);
    }

    void timesOutWithoutNotifications()
    {
        QObject object;
        int checks{0};
        QVERIFY(!qmltest::Await([&] { ++checks; return false; }, {qmltest::Property(&object, "objectName")}, 1));
        QCOMPARE(checks, 1);
    }

    void failsWhenAnObservedObjectIsDestroyed()
    {
        auto object = std::make_unique<QObject>();
        auto* pointer = object.get();
        QTimer::singleShot(0, this, [&] { object.reset(); });
        QVERIFY(!qmltest::Await([&] { return pointer->objectName() == "ready"; }, {qmltest::Property(pointer, "objectName")}));
        QVERIFY(!object);
    }

    void canWaitForDestruction()
    {
        auto object = std::make_unique<QObject>();
        QPointer<QObject> guarded{object.get()};
        QTimer::singleShot(0, this, [&] { object.reset(); });
        QVERIFY(qmltest::Await([&] { return guarded.isNull(); }, {qmltest::Destroyed(guarded)}));
    }

    void workerCompletionAndExceptions()
    {
        QCOMPARE(qmltest::Async([] { return 42; }), 42);
        QVERIFY_EXCEPTION_THROWN(qmltest::Async([]() -> int { throw std::runtime_error{"worker failure"}; }), std::runtime_error);
        bool completed{false};
        qmltest::Async([&] { completed = true; });
        QVERIFY(completed);
    }
};

QTEST_GUILESS_MAIN(SignalWaitTests)
#include <test_wait_tests.moc>
