// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/desktopintentowner.h>
#include <bitcoin-build-config.h> // IWYU pragma: keep
#include <qml/desktopintentargs.h>
#include <qml/models/desktopintentmodel.h>
#ifdef ENABLE_WALLET
#include <qml/models/bitcoinurimodel.h>
#endif
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
    void actualArgumentsKeepCoreOptionsBeforeRequestsOnly()
    {
        QByteArray executable{"bitcoin-core-app"}, option{"-regtest"}, uri{"bitcoin:request"}, injected{"-platform"};
        char* valid[]{executable.data(), option.data(), uri.data()};
        const auto parsed = desktopintent::ParseArguments(3, valid, QStringLiteral("process"), QStringLiteral("/captured"));
        QVERIFY(parsed.error.isEmpty()); QCOMPARE(parsed.options_argc, 2);
        QCOMPARE(parsed.intents.size(), size_t{1}); QCOMPARE(parsed.intents.front().value, QStringLiteral("bitcoin:request"));
        char* invalid[]{executable.data(), option.data(), uri.data(), injected.data()};
        const auto refused = desktopintent::ParseArguments(4, invalid, QStringLiteral("process"), QStringLiteral("/captured"));
        QVERIFY(!refused.error.isEmpty()); QVERIFY(refused.intents.empty());
    }
    void argumentFilesUseCapturedDirectoryAndRejectRemoteAndInvalidUtf8()
    {
        QByteArray executable{"bitcoin-core-app"}, path{"draft.psbt"}, remote{"file://other-host/draft.psbt"}, invalid{1, static_cast<char>(0xff)};
        char* arguments[]{executable.data(), path.data()};
        const auto parsed = desktopintent::ParseArguments(2, arguments, QStringLiteral("process"), QStringLiteral("/captured"));
        QVERIFY(parsed.error.isEmpty()); QVERIFY(parsed.intents.front().kind == desktopintent::Kind::File);
        QCOMPARE(parsed.intents.front().value, QStringLiteral("/captured/draft.psbt"));
        arguments[1] = remote.data();
        QVERIFY(!desktopintent::ParseArguments(2, arguments, QStringLiteral("process"), QStringLiteral("/captured")).error.isEmpty());
        arguments[1] = invalid.data();
        QVERIFY(!desktopintent::ParseArguments(2, arguments, QStringLiteral("process"), QStringLiteral("/captured")).error.isEmpty());
    }
#ifdef ENABLE_WALLET
    void retainedFacadeDoesNotHoldPhysicalShutdownAfterStop()
    {
        ManualRoutedExecution commands;
        std::deque<std::function<void()>> gui;
        const auto post = [&](auto turn) { gui.push_back(std::move(turn)); };
        const auto drain = [&] {
            do {
                commands.drain();
                while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
            } while (!commands.queuedIdle() || !commands.activeIdle());
        };
        BitcoinUriModel factory; factory.setServices({commands.scheduler, post});
        DesktopIntentModel model{{commands.scheduler, post}, true}; model.setFactory(&factory);
        QVERIFY(model.owner()->admit({Input(1)}, {})); model.setReady(true); drain();
        model.inspectNext(); drain(); QVERIFY(model.inspection());
        const QPointer<PaymentImportOperation> inspection{model.inspection()};
        QSignalSpy changed{&model, &DesktopIntentModel::changed};
        QSignalSpy inspection_changed{inspection.data(), &PaymentImportOperation::changed};
        model.closeAdmission();
        QCOMPARE(changed.count(), 0); QCOMPARE(inspection_changed.count(), 0);
        factory.closeAdmission(); drain();
        QVERIFY(inspection); // The cached presentation deliberately remains alive.
        QCOMPARE(commands.scheduler->pending(), size_t{0});
    }
    void creationReceiptSurvivesProjectionLossAndCannotCreateTwice()
    {
        ManualRoutedExecution commands;
        BitcoinUriModel factory; factory.setServices({commands.scheduler, {}});
        QObject presentation;
        const auto receipt = std::make_shared<paymentimport::CreationReceipt>();
        auto* operation = factory.requestImport({paymentimport::Input::Kind::Text, QStringLiteral("bitcoin:invalid")}, {}, &presentation, receipt);
        QVERIFY(operation); const auto original = operation->requestId();
        QVERIFY(receipt->state() == paymentimport::CreationReceipt::State::Accepted); QCOMPARE(receipt->source(), original);
        QVERIFY(!factory.requestImport({paymentimport::Input::Kind::Text, QStringLiteral("bitcoin:another")}, {}, &presentation, receipt));
        delete operation;
        commands.drain();
        QVERIFY(receipt->state() == paymentimport::CreationReceipt::State::Accepted); QCOMPARE(receipt->source(), original);
        const auto unused = std::make_shared<paymentimport::CreationReceipt>(); unused->withdraw();
        QVERIFY(!factory.requestImport({paymentimport::Input::Kind::Text, QStringLiteral("bitcoin:unused")}, {}, &presentation, unused));
        QVERIFY(unused->state() == paymentimport::CreationReceipt::State::NotAccepted);
        factory.closeAdmission(); commands.drain();
    }
    void facadeDefersInspectionUntilReadyAndRetainsOriginalFactoryReceipt()
    {
        ManualRoutedExecution commands;
        std::deque<std::function<void()>> gui;
        const auto post = [&](auto turn) { gui.push_back(std::move(turn)); };
        const auto drain = [&] {
            do {
                commands.drain();
                while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
            } while (!commands.queuedIdle() || !commands.activeIdle());
        };
        BitcoinUriModel factory; factory.setServices({commands.scheduler, post});
        DesktopIntentModel model{{commands.scheduler, post}, true}; model.setFactory(&factory);
        QVERIFY(model.owner()->admit({Input(1)}, {})); drain();
        QCOMPARE(model.pendingCount(), 1); QVERIFY(!model.canInspect()); model.inspectNext(); drain();
        QVERIFY(!model.inspection());
        model.setReady(true); drain(); QVERIFY(model.canInspect());
        model.inspectNext(); drain(); QVERIFY(model.inspection()); QCOMPARE(model.pendingCount(), 0);
        const auto original = model.inspection()->requestId();
        QObject recipient_view;
        auto* operation = model.takeInspection(&recipient_view); QVERIFY(operation); QCOMPARE(operation->requestId(), original);
        QVERIFY(!model.inspection());
        const auto original_owner = model.owner();
        model.closeAdmission(); factory.closeAdmission(); delete operation; drain();
        std::optional<desktopintent::Receipt> receipt;
        QVERIFY(original_owner->lookup(QStringLiteral("1"), [&](auto value) { receipt = value; })); drain();
        QVERIFY(receipt); QCOMPARE(receipt->inspection, static_cast<uint64_t>(original));
    }
#endif
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
