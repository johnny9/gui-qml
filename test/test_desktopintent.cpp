// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/desktopintentowner.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>

namespace {
desktopintent::Input Input(int id, QString value = QStringLiteral("bitcoin:request"))
{
    return {QString::number(id), desktopintent::Kind::Text, std::move(value), desktopintent::Origin::Arguments};
}
}
class DesktopIntentTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void failedBatchHasNoAcceptedPrefixOrConsumedIdentity()
    {
        desktopintent::Queue queue;
        std::vector<desktopintent::Input> full;
        for (int index = 1; index <= 15; ++index) full.push_back(Input(index));
        QVERIFY(queue.admit(full));
        QVERIFY(queue.admit({Input(16), Input(17)}).error == desktopintent::Error::Capacity);
        QCOMPARE(queue.snapshot().pending, size_t{15});
        QVERIFY(!queue.lookup(QStringLiteral("16")));
        const auto last = queue.admit({Input(16)}); QVERIFY(last);
        QCOMPARE(last.receipts.front().delivery, uint64_t{16});
        const auto duplicate = queue.admit({Input(16)}); QVERIFY(duplicate);
        QCOMPARE(duplicate.receipts.front().delivery, uint64_t{16});
        QVERIFY(queue.admit({Input(16, QStringLiteral("bitcoin:different"))}).error == desktopintent::Error::Invalid);
        QCOMPARE(queue.snapshot().pending, size_t{16});
    }
    void unaccountedDeliverySurvivesReadinessAndStop()
    {
        desktopintent::Queue queue; QVERIFY(queue.admit({Input(1)}));
        queue.readiness(1, true);
        const auto original = queue.deliver(41); QVERIFY(original);
        QVERIFY(queue.unknown(original->ticket));
        for (int index = 2; index <= 16; ++index) QVERIFY(queue.admit({Input(index)}));
        QVERIFY(!queue.deliver(99));
        auto wrong = original->ticket; ++wrong.route;
        QVERIFY(!queue.accepted(wrong, 100));
        QVERIFY(queue.snapshot().active == original->ticket);
        queue.stop();
        QVERIFY(queue.snapshot().phase == desktopintent::Phase::Stopping);
        QCOMPARE(queue.snapshot().pending, size_t{1});
        QVERIFY(queue.lookup(QStringLiteral("2"))->disposition == desktopintent::Disposition::DroppedOnStop);
        QVERIFY(queue.accepted(original->ticket, 700));
        QVERIFY(queue.snapshot().phase == desktopintent::Phase::Stopped);
        QCOMPARE(queue.lookup(QStringLiteral("1"))->inspection, uint64_t{700});
        QVERIFY(queue.admit({Input(17)}).error == desktopintent::Error::Closed);
    }
    void byteBudgetAndOldReadinessCannotAuthorizeDelivery()
    {
        desktopintent::Queue queue;
        std::vector<desktopintent::Input> batch;
        for (int index = 1; index <= 4; ++index)
            batch.push_back(Input(index, QString(desktopintent::Queue::MAX_INPUT_CHARS, QLatin1Char{'x'})));
        QVERIFY(queue.admit(batch));
        QVERIFY(queue.admit({Input(5)}).error == desktopintent::Error::Capacity);
        queue.readiness(2, false); queue.readiness(1, true); QVERIFY(!queue.deliver(4));
        queue.readiness(3, true);
        const auto active = queue.deliver(4); QVERIFY(active);
        QVERIFY(queue.rejected(active->ticket));
        QCOMPARE(queue.snapshot().pending, size_t{4}); QVERIFY(!queue.deliver(5));
        queue.readiness(4, true);
        const auto retry = queue.deliver(5); QVERIFY(retry);
        QCOMPARE(retry->ticket.delivery, active->ticket.delivery);
        QVERIFY(retry->ticket.route != active->ticket.route);
        QVERIFY(!queue.accepted(active->ticket, 1));
    }
    void lostFactoryAcknowledgmentQueriesOnlyTheOriginalSink()
    {
        ManualRoutedExecution transport;
        desktopintent::Services::Time now{};
        desktopintent::Snapshot snapshot;
        auto actor = desktopintent::Owner::Create({transport.scheduler, [&] { return now; }, {}, std::chrono::milliseconds{10}},
            [&](auto value) { snapshot = value; });
        QVERIFY(actor);
        auto sink = std::make_shared<desktopintent::Sink>();
        int created{0}, queried{0}, retired{0};
        uint64_t inspect_target{0}, query_target{0}, retired_target{0};
        desktopintent::Sink::Completed late;
        sink->identity = 10;
        sink->inspect = [&](auto delivery, auto reply) {
            ++created; late = std::move(reply); inspect_target = delivery.ticket.target;
            return execution::Admission{};
        };
        sink->query = [&](auto ticket, auto reply) {
            ++queried; query_target = ticket.target;
            reply({desktopintent::Knowledge::Accepted, 900}); return execution::Admission{};
        };
        sink->retire = [&](auto ticket) { ++retired; retired_target = ticket.target; };
        QVERIFY(actor->admit({Input(1)}, {})); QVERIFY(actor->readiness(1, true)); QVERIFY(actor->review(sink));
        transport.drain(); QCOMPARE(created, 1);
        QVERIFY(snapshot.phase == desktopintent::Phase::Delivering);
        now += std::chrono::milliseconds{11}; QVERIFY(actor->tick()); transport.drain();
        QVERIFY(snapshot.phase == desktopintent::Phase::ReconcilingDelivery);
        auto other = std::make_shared<desktopintent::Sink>(*sink); other->identity = 99;
        QVERIFY(actor->readiness(2, true)); QVERIFY(actor->review(other)); transport.drain();
        QCOMPARE(created, 1);
        QVERIFY(actor->stop()); transport.drain();
        QCOMPARE(queried, 1); QCOMPARE(retired, 1);
        QCOMPARE(inspect_target, uint64_t{10}); QCOMPARE(query_target, uint64_t{10}); QCOMPARE(retired_target, uint64_t{10});
        QVERIFY(snapshot.phase == desktopintent::Phase::Stopped);
        late({desktopintent::Knowledge::NotAccepted, 0}); transport.drain();
        QVERIFY(snapshot.phase == desktopintent::Phase::Stopped);
        std::optional<desktopintent::Receipt> receipt;
        QVERIFY(actor->lookup(QStringLiteral("1"), [&](auto value) { receipt = value; })); transport.drain();
        QVERIFY(receipt); QCOMPARE(receipt->inspection, uint64_t{900});
        late = {}; sink.reset(); other.reset(); actor.reset(); transport.drain();
        QVERIFY(transport.quiescent());
    }
};
BITCOINQML_REGISTER_QT_TEST(DesktopIntentTests)
#include <test_desktopintent.moc>
