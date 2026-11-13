// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/models/bitcoinaddress.h>
#include <qml/backendexecutor.h>
#include <test/mocks/admissionhookscheduler.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/threadrecordingscheduler.h>
#include <QPointer>
#include <QScopeGuard>

class BitcoinAddressTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void acceptedNormalizationCannotAdoptARevokedEditor_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("quiet") << 0;
        QTest::newRow("delete") << 1;
        QTest::newRow("successor-edit") << 2;
    }
    void acceptedNormalizationCannotAdoptARevokedEditor()
    {
        QFETCH(int, change);
        ManualRoutedExecution commands;
        std::deque<std::function<void()>> turns;
        const auto scheduler = std::make_shared<AdmissionHookScheduler>(commands.scheduler);
        auto address = std::make_unique<BitcoinAddress>(QStringLiteral("original"));
        address->setServices({scheduler, [&](auto publication) { turns.push_back(std::move(publication)); }});
        QSignalSpy changed{address.get(), &BitcoinAddress::normalizationChanged};
        const QPointer<BitcoinAddress> original{address.get()};
        const auto settle = [&] {
            commands.drain();
            while (!turns.empty()) {
                auto publication = std::move(turns.front()); turns.pop_front(); publication();
            }
            commands.drain();
        };
        const auto cleanup = qScopeGuard([&] {
            scheduler->disarm();
            if (address) address->closeAdmission();
            settle();
            if (!commands.quiescent()) QTest::qFail("Original editor normalization did not physically drain", __FILE__, __LINE__);
        });
        qsizetype notices{0};
        const auto hook = scheduler->armSubmit(execution::Lane::Domain, 1, [&] {
            if (change == 0) address->closeAdmission();
            else if (change == 1) address.reset();
            else address->setAddress(QStringLiteral("successor"), 9);
            notices = changed.size();
        });
        const QString input = QString(10'000, QLatin1Char{' '}) + QStringLiteral("oldliteral");
        QCOMPARE(address->setAddress(input, static_cast<int>(input.size())), -1);
        QCOMPARE(hook->calls.load(), size_t{1});
        const execution::TaskId task{hook->originalId.load()};
        QVERIFY(task.value != 0);
        QVERIFY(commands.scheduler->phase(task)); // The real original task was accepted.
        QCOMPARE(changed.size(), notices);
        if (change == 1) QVERIFY(!original);
        else {
            QVERIFY(original);
            QVERIFY(!address->normalizationPending());
            QVERIFY(address->normalizationError().isEmpty());
            QCOMPARE(address->address(), change == 2 ? QStringLiteral("successor") : QStringLiteral("original"));
        }
        settle();
        QVERIFY(!commands.scheduler->phase(task));
        QCOMPARE(changed.size(), notices);
        if (address) {
            QVERIFY(!address->normalizationPending());
            QCOMPARE(address->address(), change == 2 ? QStringLiteral("successor") : QStringLiteral("original"));
            address->closeAdmission();
        }
        scheduler->disarm();
        settle();
        QVERIFY(commands.quiescent());
    }
    void longLeadingWhitespaceUsesManualDomainNormalizationWithoutTruncation() {
        ManualRoutedExecution commands; std::deque<std::function<void()>> turns;
        BitcoinAddress address; address.setServices({commands.scheduler, [&](std::function<void()> turn) { turns.push_back(std::move(turn)); }});
        const QString text = QString(10'000, QLatin1Char{' '}) + "bc1qexampleaddress";
        QCOMPARE(address.setAddress(text, text.size()), -1); QVERIFY(address.normalizationPending()); QVERIFY(address.address().isEmpty());
        QVERIFY(commands.step(execution::Lane::Domain)); QVERIFY(address.address().isEmpty());
        QVERIFY(commands.step(execution::Lane::Domain)); QVERIFY(address.address().isEmpty());
        while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); turn(); }
        QCOMPARE(address.address(), QStringLiteral("bc1qexampleaddress")); QVERIFY(!address.normalizationPending());
        QCOMPARE(address.normalizedCursor(), address.formattedAddress().size());
        address.closeAdmission(); commands.drain();
    }
    void onlyTheExistingFirstNinetyValidCharactersMatterForCursorMapping() {
        BitcoinAddress address;
        const QString accepted(90, QLatin1Char{'a'});
        const QString text = accepted + QString(100'000, QLatin1Char{' '});
        const auto cursor = address.setAddress(text, text.size());
        QCOMPARE(address.address(), accepted); QCOMPARE(cursor, address.formattedAddress().size());
        QVERIFY(!address.normalizationPending()); QVERIFY(address.normalizationError().isEmpty());
    }
    void successorEditFencesAnOlderQueuedNormalization() {
        ManualRoutedExecution commands; std::deque<std::function<void()>> turns;
        BitcoinAddress address; address.setServices({commands.scheduler, [&](std::function<void()> turn) { turns.push_back(std::move(turn)); }});
        address.setAddress(QString(10'000, QLatin1Char{' '}) + "original", 10'008);
        QVERIFY(address.normalizationPending()); address.setAddress("successor", 9);
        commands.drain(); while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); turn(); }
        QCOMPARE(address.address(), QStringLiteral("successor")); QVERIFY(!address.normalizationPending());
        address.closeAdmission(); commands.drain();
    }
    void nativePasteCompositionRunsBehindTheOriginalBoundedEditorSnapshot() {
        ManualRoutedExecution commands; std::deque<std::function<void()>> turns;
        BitcoinAddress address{"abcd1234"}; address.setServices({commands.scheduler, [&](std::function<void()> turn) { turns.push_back(std::move(turn)); }});
        const QString pasted = QString(10'000, QLatin1Char{' '}) + "xy";
        QCOMPARE(address.pasteAddress(pasted, 5, 9), -1); QCOMPARE(address.address(), QStringLiteral("abcd1234"));
        commands.drain(); while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); turn(); }
        QCOMPARE(address.address(), QStringLiteral("abcdxy")); QCOMPARE(address.normalizedCursor(), 7);
        address.closeAdmission(); commands.drain();
    }
    void cancellingExactNormalizationCannotCancelSuccessorOrPublishOldLiteral() {
        ManualRoutedExecution commands; std::deque<std::function<void()>> turns;
        BitcoinAddress address{"original"}; address.setServices({commands.scheduler, [&](std::function<void()> turn) { turns.push_back(std::move(turn)); }});
        address.setAddress(QString(8192, ' ') + "oldliteral", 8202);
        const auto old_id = address.normalizationId(); QVERIFY(address.normalizationPending());
        commands.drain(); QVERIFY(address.cancelNormalization(old_id));
        QVERIFY(!address.normalizationPending()); QCOMPARE(address.address(), QStringLiteral("original"));
        address.setAddress(QString(8192, ' ') + "newliteral", 8202);
        const auto new_id = address.normalizationId(); QVERIFY(new_id > old_id);
        QVERIFY(!address.cancelNormalization(old_id)); QVERIFY(address.normalizationPending());
        while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); turn(); }
        QCOMPARE(address.address(), QStringLiteral("original"));
        commands.drain(); while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); turn(); }
        QCOMPARE(address.address(), QStringLiteral("newliteral")); QVERIFY(!address.normalizationPending());
        QVERIFY(!address.cancelNormalization(new_id)); address.closeAdmission(); commands.drain();
    }
    void rejectedLongEditKeepsCanonicalValueAndExposesFailure() {
        ManualRoutedExecution commands;
        BitcoinAddress address{"original"}; address.setServices({commands.scheduler, [](std::function<void()>) {}});
        commands.rejectLane(execution::Lane::Domain);
        QCOMPARE(address.setAddress(QString(10'000, QLatin1Char{' '}) + "successor", 10'009), -1);
        QCOMPARE(address.address(), QStringLiteral("original")); QVERIFY(!address.normalizationPending());
        QVERIFY(!address.normalizationError().isEmpty()); address.closeAdmission(); commands.drain();
    }
    void clearingApplicationServicesClosesCapturedEditorsAndNewSessionGetsFreshGate() {
        BitcoinAddress::setDefaultServices({});
        const auto reset_defaults = qScopeGuard([] { BitcoinAddress::setDefaultServices({}); });
        ManualRoutedExecution commands; std::deque<std::function<void()>> turns;
        const execution::SnapshotServices services{commands.scheduler, [&](std::function<void()> turn) { turns.push_back(std::move(turn)); }};
        BitcoinAddress independent; independent.setServices(services);
        BitcoinAddress::setDefaultServices(services);
        BitcoinAddress old; old.setServices(services);
        BitcoinAddress::setDefaultServices({});
        const QString text = QString(10'000, QLatin1Char{' '}) + "destination";
        QCOMPARE(old.setAddress(text, text.size()), -1); QVERIFY(!old.normalizationPending());
        old.setServices(services); QCOMPARE(old.setAddress(text, text.size()), -1);
        QVERIFY(commands.queuedIdle());
        BitcoinAddress::setDefaultServices(services);
        BitcoinAddress fresh; QCOMPARE(fresh.setAddress(text, text.size()), -1); QVERIFY(fresh.normalizationPending());
        BitcoinAddress::setDefaultServices({});
        QVERIFY(!fresh.normalizationPending());
        QCOMPARE(independent.setAddress(text, text.size()), -1); QVERIFY(independent.normalizationPending());
        commands.drain(); while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); turn(); }
        QVERIFY(fresh.address().isEmpty()); QCOMPARE(independent.address(), QStringLiteral("destination"));
        BitcoinAddress::setDefaultServices(services);
        BitcoinAddress successor; successor.setAddress(text, text.size()); commands.drain();
        while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); turn(); }
        QCOMPARE(successor.address(), QStringLiteral("destination"));
        BitcoinAddress::setDefaultServices({}); independent.closeAdmission(); old.closeAdmission(); fresh.closeAdmission(); successor.closeAdmission(); commands.drain();
    }
    void realLongNormalizationUsesTheInjectedDomainThread() {
        BackendExecutor executor; auto recording = std::make_shared<ThreadRecordingScheduler>(executor.scheduler());
        {
            BitcoinAddress address; address.setServices({recording, {}});
            address.setAddress(QString(10'000, QLatin1Char{' '}) + "destination", 10'011);
            QTRY_VERIFY(!address.normalizationPending()); QCOMPARE(address.address(), QStringLiteral("destination"));
            QVERIFY(recording->thread(execution::Lane::Domain));
            QVERIFY(recording->thread(execution::Lane::Domain) != QThread::currentThread()); address.closeAdmission();
        }
        executor.shutdown(); QTRY_VERIFY(executor.isDrained());
    }
    void setAddress_acceptsSupportedAddressCharacters();
    void setAddress_rejectsUnsupportedCharactersImmediately();
    void formattedAddress_groupsCharactersInFours();
    void ellipsesAddress_keepsVisiblePrefixAndSuffix();
};

void BitcoinAddressTests::setAddress_acceptsSupportedAddressCharacters()
{
    const QString input{QStringLiteral("0123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")};
    BitcoinAddress address;

    address.setAddress(input, static_cast<int>(input.size()));

    QCOMPARE(address.address(), input);
}

void BitcoinAddressTests::setAddress_rejectsUnsupportedCharactersImmediately()
{
    BitcoinAddress address{QStringLiteral("bc1qexampleaddress")};
    QSignalSpy formatted_address_changed{&address, &BitcoinAddress::formattedAddressChanged};
    const QString input{address.formattedAddress() + QStringLiteral(".[]IO")};

    const int cursor_position{address.setAddress(input, static_cast<int>(input.size()))};

    QCOMPARE(address.address(), QStringLiteral("bc1qexampleaddress"));
    QCOMPARE(address.formattedAddress(), QStringLiteral("bc1q exam plea ddre ss"));
    QCOMPARE(cursor_position, static_cast<int>(address.formattedAddress().size()));
    QCOMPARE(formatted_address_changed.count(), 1);
}

void BitcoinAddressTests::formattedAddress_groupsCharactersInFours()
{
    QCOMPARE(
        BitcoinAddress::formattedAddress("abcd1234efgh5678"),
        QString("abcd 1234 efgh 5678"));
}

void BitcoinAddressTests::ellipsesAddress_keepsVisiblePrefixAndSuffix()
{
    QCOMPARE(
        BitcoinAddress::ellipsesAddress("abcd1234efgh5678ijkl"),
        QString("abcd 1234 ... 5678 ijkl"));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BitcoinAddressTests)
#else
QTEST_MAIN(BitcoinAddressTests)
#endif
#include <test_bitcoinaddress.moc>
