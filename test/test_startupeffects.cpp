// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/startupeffects.h>
#include <qml/backendexecutor.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <atomic>
#include <stdexcept>

class StartupEffectsTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void missingSchedulerFailsBeforeAnyBorrowedWork() {
        bool ran{false};
        bool rejected{false};
        try {
            (void)startup::Run({}, [&] { ran = true; return 1; });
        } catch (const std::invalid_argument&) {
            rejected = true;
        } catch (...) {
            QFAIL("Missing scheduler must throw std::invalid_argument");
        }
        QVERIFY(rejected);
        QVERIFY(!ran);
    }
    void throwingPrimaryPostCannotStrandAnActualDomainOutcome_data() {
        QTest::addColumn<bool>("failure"); QTest::newRow("success") << false; QTest::newRow("native-exception") << true;
    }
    void throwingPrimaryPostCannotStrandAnActualDomainOutcome() {
        QFETCH(bool, failure);
        BackendExecutor executor; std::atomic<QThread*> native{nullptr}; std::atomic<int> effects{0}, posts{0};
        const execution::SnapshotServices services{executor.scheduler(), [&](auto) { ++posts; throw std::runtime_error("Primary presentation post rejected"); }};
        bool failed{false};
        try {
            const auto result = startup::Run(services, [&] {
                native = QThread::currentThread(); ++effects;
                if (failure) throw std::runtime_error("original startup failure");
                return 42;
            });
            QCOMPARE(result, 42);
        } catch (const std::runtime_error& error) {
            failed = true; QCOMPARE(QString::fromUtf8(error.what()), QStringLiteral("original startup failure"));
        }
        QCOMPARE(failed, failure); QCOMPARE(effects.load(), 1); QCOMPARE(posts.load(), 1);
        QVERIFY(native.load() != nullptr && native.load() != QThread::currentThread());
        executor.shutdown(); QTRY_VERIFY(executor.isDrained());
    }
};
BITCOINQML_REGISTER_QT_TEST(StartupEffectsTests)
#include <test_startupeffects.moc>
