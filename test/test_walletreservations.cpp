// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletrequests.h>
#include <qml/walletreceipts.h>
#include <qml/backendexecutor.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>

#include <chainparams.h>
#include <chainparamsbase.h>
#include <key_io.h>

#include <QtTest/QtTest>
#include <QScopeGuard>
#include <QSemaphore>
#include <string_view>

namespace {
using namespace walletrequests;
using execution::Lane;
CTxDestination Destination(unsigned char value)
{
    uint160 key; key.begin()[0] = value; return PKHash{key};
}
QString Address(unsigned char value) { return QString::fromStdString(EncodeDestination(Destination(value))); }
struct Wallet : StubWallet {
    int generated{0}, unlocked{0}, relocked{0}, reads{0}, writes{0};
    std::string expected_password{"correct"};
    bool locked{false}, encrypted{false}, fail_lock{false}, fail_unlock{false}, can_generate{true}, taproot{true};
    std::set<interfaces::WalletTx> transactions;
    std::map<CTxDestination, std::string> book;
    bool canGetAddresses() override { return can_generate; }
    bool taprootEnabled() override { return taproot; }
    bool isCrypted() override { return encrypted; }
    bool isLocked() override { return locked; }
    util::Expected<void, wallet::WalletError> unlock(const SecureString& password) override {
        ++unlocked;
        if (fail_unlock) return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::GenericError, Untranslated("device unavailable")}};
        if (std::string_view{password.data(),password.size()} != std::string_view{expected_password}) return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("incorrect")}};
        locked = false; return {};
    }
    bool lock() override { ++relocked; if (fail_lock) return false; locked = true; return true; }
    OutputType getDefaultAddressType() override { return OutputType::LEGACY; }
    util::Result<CTxDestination> getNewDestination(OutputType, const std::string& label) override {
        ++generated;
        if (locked) return util::Error{Untranslated("locked keypool")};
        const auto destination = Destination(static_cast<unsigned char>(generated));
        book[destination] = label; return destination;
    }
    bool getAddress(const CTxDestination& destination, std::string* label, wallet::AddressPurpose* purpose) override {
        const auto found = book.find(destination); if (found == book.end()) return false;
        if (label) *label = found->second;
        if (purpose) *purpose = wallet::AddressPurpose::RECEIVE;
        return true;
    }
    std::set<interfaces::WalletTx> getWalletTxs() override { ++reads; return transactions; }
    bool setAddressReceiveRequest(const CTxDestination&, const std::string&, const std::string&) override { ++writes; return true; }
};
struct Storage {
    QString current;
    std::set<QString> retired;
    int reads{0}, writes{0}, marks{0};
    bool fail_read{false}, fail_write{false}, fail_retire{false}, fail_clear{false};
    ReservationStorage port() {
        return {
            [this](interfaces::Wallet&, const QString& candidate) {
                ++reads; const auto address = candidate.isEmpty() ? current : candidate;
                return StoredAddress{address, retired.contains(address), !fail_read};
            },
            [this](interfaces::Wallet&, const QString& address) {
                ++writes; if (fail_write) return false; current = address; return true;
            },
            [this](interfaces::Wallet&, const QString& address) {
                ++marks; if (fail_retire) return false; retired.insert(address); return true;
            },
            [this](interfaces::Wallet&, const QString& address) {
                if (fail_clear) return false;
                if (address.isEmpty() || current == address) current.clear();
                return true;
            },
        };
    }
};
Command Reserve(bool next = false, uint64_t view = 1)
{
    Command command; command.kind = Kind::ReserveAddress; command.view = view;
    command.draft.address_type = "legacy"; command.next = next; return command;
}
struct Fixture {
    ManualRoutedExecution commands, cleanup;
    std::shared_ptr<Wallet> wallet{std::make_shared<Wallet>()};
    Storage storage;
    std::shared_ptr<walletopening::Session> opening{std::make_shared<walletopening::Session>(commands.scheduler, cleanup.scheduler,
        std::make_shared<std::atomic_bool>(false), [] {})};
    std::shared_ptr<interfaces::Wallet> client{opening->start(wallet).borrower};
    std::shared_ptr<Store> store;
    Fixture() {
        commands.drain();
        const auto port = storage.port();
        store = std::make_shared<Store>(opening, [] {}, Store::Watch{}, QDateTime::currentDateTimeUtc,
            [] { return true; }, port.clear, port);
        Command load; load.kind = Kind::Reload; settle(load);
        QVERIFY(opening->postDomain([this] { store->observed(std::make_shared<const walletreceipts::Snapshot>(walletreceipts::Snapshot{opening->identity(), {}, {}, opening->transactionRevision()})); })); commands.drain();
    }
    ~Fixture() {
        store->close(); opening->retire(); client.reset(); commands.drain(); cleanup.drain();
        QVERIFY(commands.quiescent() && cleanup.quiescent());
    }
    std::shared_ptr<Operation> settle(Command command) {
        const auto admitted = store->submit(std::move(command));
        if (!admitted) { QTest::qFail("Reservation command was not admitted", __FILE__, __LINE__); return {}; }
        commands.drain(); return admitted.operation;
    }
    Command Resume(const std::shared_ptr<Operation>& operation, const char* password) {
        Command command; command.kind = Kind::ResumeAddress; command.resume = operation->address_intent;
        if (password) command.secret = std::make_shared<SecureString>(password);
        return command;
    }
};
}

class WalletReservationTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { SelectBaseParams(ChainType::MAIN); SelectParams(ChainType::MAIN); }
    void capturedPasswordRetainsOriginalReceiveIntent_data()
    {
        QTest::addColumn<int>("scenario");QTest::newRow("held-result")<<0;QTest::newRow("quiet-close-after-generation")<<1;QTest::newRow("native-admission-refused")<<2;QTest::newRow("capture-budget-refused")<<3;
    }
    void capturedPasswordRetainsOriginalReceiveIntent()
    {
        QFETCH(int,scenario);Fixture f;f.wallet->encrypted=true;f.wallet->locked=true;f.wallet->expected_password=std::string(40'000,'x');
        QString source=QString::fromStdString(f.wallet->expected_password);
        auto captured=secretinput::Capture(f.commands.scheduler,source,scenario==3?32:execution::Limits::Bounded().ordinary.payload_bytes);
        auto command=Reserve();command.captured_secret=captured.input;command.secret_capture_error=captured.error;
        if(scenario==3)QVERIFY(!captured);else QVERIFY(captured);
        if(scenario==2)f.commands.rejectLane(Lane::Core);
        const auto admitted=f.store->submit(std::move(command));source.fill(QChar{'y'});QCOMPARE(f.wallet->unlocked,0);QCOMPARE(f.wallet->generated,0);captured.input.reset();
        if(scenario==3){QVERIFY(!admitted);QCOMPARE(admitted.rejection,execution::Rejection::Capacity);}
        else if(scenario==2){f.commands.drain();QVERIFY(admitted.operation->result());QCOMPARE(admitted.operation->result()->error,Error::Rejected);QCOMPARE(f.wallet->unlocked,0);QCOMPARE(f.wallet->generated,0);}
        else{QVERIFY(admitted);int turns{0};while(f.wallet->generated==0&&!f.commands.queuedIdle()&&turns++<128)QVERIFY(f.commands.stepNext());
            QCOMPARE(f.wallet->unlocked,1);QCOMPARE(f.wallet->generated,1);QCOMPARE(f.wallet->relocked,1);QVERIFY(f.wallet->locked);QVERIFY(!admitted.operation->result());
            if(scenario==1){f.store->close();f.opening->retire();}
            f.commands.drain();QVERIFY(admitted.operation->result());QVERIFY(admitted.operation->result()->address);QCOMPARE(admitted.operation->result()->address->address,Address(1));}
        f.commands.drain();f.cleanup.drain();
    }
    void allocationAndStorageHaveSeparateNativeAndDomainTurns()
    {
        Fixture f;
        const auto admitted = f.store->submit(Reserve()); QVERIFY(admitted);
        QCOMPARE(f.wallet->generated, 0); QCOMPARE(f.storage.reads, 0);
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(f.commands.step(Lane::Core)); QCOMPARE(f.storage.reads, 1); QCOMPARE(f.wallet->generated, 0);
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(f.commands.step(Lane::Core)); QCOMPARE(f.wallet->generated, 1); QCOMPARE(f.storage.writes, 0);
        QVERIFY(!admitted.operation->result());
        while (f.commands.step(Lane::Domain)) {}
        while (f.storage.writes == 0) {
            QVERIFY(f.commands.step(Lane::Core));
            // An independent metadata read may run between Native effects.
            if (f.storage.writes == 0) while (f.commands.step(Lane::Domain)) {}
        }
        QCOMPARE(f.storage.writes, 1);
        QVERIFY(!admitted.operation->result());
        f.commands.drain(); QVERIFY(admitted.operation->result()->address->persisted);
    }
    void unusedAddressIsRestoredWhileNextAllocates()
    {
        Fixture f; f.storage.current = Address(50); f.wallet->book[Destination(50)] = "private";
        const auto restored = f.settle(Reserve());
        QCOMPARE(restored->result()->address->address, Address(50)); QCOMPARE(f.wallet->generated, 0);
        QCOMPARE(restored->result()->address->label, QString{"private"});
        const auto next = f.settle(Reserve(true));
        QCOMPARE(next->result()->address->address, Address(1)); QCOMPARE(f.wallet->generated, 1);
    }
    void persistedRetirementPreventsRestoreWithoutARequestRecord()
    {
        Fixture f; f.storage.current = Address(50); f.storage.retired.insert(Address(50)); f.wallet->book[Destination(50)] = "old";
        const auto next = f.settle(Reserve());
        QCOMPARE(next->result()->address->address, Address(1)); QCOMPARE(f.wallet->generated, 1);
    }
    void twoViewsKeepIndependentUnusedReservations()
    {
        Fixture f;
        const auto first = f.settle(Reserve(false, 1)); const auto second = f.settle(Reserve(false, 2));
        QVERIFY(first->result()->address->address != second->result()->address->address);
        const auto again = f.settle(Reserve(false, 1));
        QCOMPARE(again->result()->address->address, first->result()->address->address);
        QCOMPARE(f.wallet->generated, 2);
    }
    void resetRemainsOrderedWhenANewerReserveIsAlreadyAdmitted()
    {
        Fixture f; f.storage.current = Address(50); f.wallet->book[Destination(50)] = "old";
        const auto old = f.settle(Reserve()); QCOMPARE(old->result()->address->address, Address(50));
        Command reset; reset.kind = Kind::ForgetAddress; reset.clear_reservation = true; reset.draft.address = Address(50);
        const auto forgotten = f.store->submit(reset);
        const auto next = f.store->submit(Reserve()); QVERIFY(forgotten && next);
        QCOMPARE(f.storage.current, Address(50));
        f.commands.drain(); QCOMPARE(forgotten.operation->result()->error, Error::None);
        QCOMPARE(next.operation->result()->address->address, Address(1));
        QCOMPARE(f.wallet->generated, 1); QCOMPARE(f.storage.current, Address(1));
    }
    void failedResetStillRequiresAFreshAddress()
    {
        Fixture f; f.storage.current = Address(50); f.wallet->book[Destination(50)] = "old"; f.storage.fail_clear = true;
        Command reset; reset.kind = Kind::ForgetAddress; reset.clear_reservation = true;
        QCOMPARE(f.settle(reset)->result()->error, Error::ReservationPersistence);
        const auto next = f.settle(Reserve());
        QCOMPARE(next->result()->address->address, Address(1)); QCOMPARE(f.wallet->generated, 1);
    }
    void resettingInvalidatesTheOriginalPasswordQuestion()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto question = f.settle(Reserve()); QCOMPARE(question->result()->error, Error::NeedsUnlock);
        Command reset; reset.kind = Kind::ForgetAddress; reset.clear_reservation = true;
        QCOMPARE(f.settle(reset)->result()->error, Error::None);
        const auto resumed = f.settle(f.Resume(question, "correct"));
        QCOMPARE(resumed->result()->error, Error::Changed); QCOMPARE(f.wallet->unlocked, 0); QCOMPARE(f.wallet->generated, 1);
    }
    void invalidSelectionNeverAllocatesAReplacement()
    {
        Fixture f; Command select; select.kind = Kind::SelectAddress; select.draft.address = Address(42);
        QCOMPARE(f.settle(select)->result()->error, Error::Invalid);
        QCOMPARE(f.wallet->generated, 0); QCOMPARE(f.storage.writes, 0);
    }
    void explicitSelectionUsesItsActualTypeWithoutGenerationCapabilities()
    {
        Fixture f; f.wallet->book[Destination(42)] = "selected"; f.wallet->can_generate = false; f.wallet->taproot = false;
        Command reset; reset.kind = Kind::ForgetAddress; reset.clear_reservation = true; f.settle(reset);
        Command select; select.kind = Kind::SelectAddress; select.draft.address = Address(42);
        select.draft.address_type = "bech32m";
        const auto selected = f.settle(select); QCOMPARE(selected->result()->error, Error::None);
        QCOMPARE(selected->result()->address->address, Address(42));
        QCOMPARE(selected->result()->address->address_type, QStringLiteral("legacy"));
        QCOMPARE(f.wallet->generated, 0); QCOMPARE(f.storage.writes, 1);
    }
    void unlockInfrastructureFailureIsNotReportedAsAWrongPassword()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto question = f.settle(Reserve()); f.wallet->fail_unlock = true;
        const auto resumed = f.settle(f.Resume(question, "correct"));
        QCOMPARE(resumed->result()->error, Error::AddressCreation);
        QCOMPARE(f.wallet->unlocked, 1); QCOMPARE(f.wallet->relocked, 0); QCOMPARE(f.wallet->generated, 1);
    }
    void passwordQuestionRetainsItsOriginalAttemptAndRelocks()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto question = f.settle(Reserve()); QCOMPARE(question->result()->error, Error::NeedsUnlock);
        const auto retry = f.Resume(question, "correct"); const auto secret = retry.secret;
        const auto ready = f.settle(retry);
        QCOMPARE(ready->address_intent, question->address_intent);
        QVERIFY(ready->result()->address->persisted); QVERIFY(secret->empty());
        QCOMPARE(f.wallet->unlocked, 1); QCOMPARE(f.wallet->relocked, 1); QVERIFY(f.wallet->locked);
    }
    void wrongPasswordDoesNotAllocateOrRelock()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto question = f.settle(Reserve()); const int generated = f.wallet->generated;
        auto retry = f.Resume(question, "wrong"); const auto secret = retry.secret;
        const auto refused = f.settle(retry);
        QCOMPARE(refused->result()->error, Error::IncorrectPassphrase); QVERIFY(secret->empty());
        QCOMPARE(f.wallet->generated, generated); QCOMPARE(f.wallet->relocked, 0); QVERIFY(f.wallet->locked);
    }
    void newerIntentRejectsAnOldPasswordBeforeCoreDispatch()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto question = f.settle(Reserve());
        const auto newer = f.store->submit(Reserve(true)); QVERIFY(newer);
        const auto old = f.store->submit(f.Resume(question, "correct")); QVERIFY(old);
        f.commands.drain();
        QCOMPARE(old.operation->result()->error, Error::Changed); QCOMPARE(f.wallet->unlocked, 0);
    }
    void changedIntentAfterCaptureStillCannotUnlock()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto question = f.settle(Reserve());
        auto command = f.Resume(question, "correct"); const auto secret = command.secret;
        const auto approval = f.store->submit(command); QVERIFY(approval);
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(f.commands.step(Lane::Core)); // Capture completed, credentials not used.
        while (f.commands.step(Lane::Domain)) {}
        const auto newer = f.store->submit(Reserve(true)); QVERIFY(newer);
        QVERIFY(f.commands.step(Lane::Core)); // Final Native admission guard sees the newer intent.
        f.commands.drain();
        QCOMPARE(approval.operation->result()->error, Error::Changed);
        QCOMPARE(f.wallet->unlocked, 0); QVERIFY(secret->empty());
    }
    void alreadyUnlockedWalletStaysUnlocked()
    {
        Fixture f; f.wallet->encrypted = true;
        auto command = Reserve(); command.secret = std::make_shared<SecureString>("ignored while unlocked");
        const auto ready = f.settle(command); QVERIFY(ready->result()->address->persisted);
        QCOMPARE(f.wallet->unlocked, 0); QCOMPARE(f.wallet->relocked, 0); QVERIFY(!f.wallet->locked);
    }
    void failedReservationWriteRetriesTheSameGeneratedAddress()
    {
        Fixture f; f.storage.fail_write = true;
        const auto failed = f.settle(Reserve(true));
        QCOMPARE(failed->result()->error, Error::ReservationPersistence);
        QVERIFY(failed->result()->address); QVERIFY(!failed->result()->address->persisted);
        f.storage.fail_write = false;
        const auto retry = f.settle(f.Resume(failed, nullptr));
        QCOMPARE(retry->result()->error, Error::None); QVERIFY(retry->result()->address->persisted);
        QCOMPARE(retry->result()->address->address, failed->result()->address->address);
        QCOMPARE(f.wallet->generated, 1);
    }
    void retirementIsPersistedBeforeRequestWriteEvenIfCleanupFails()
    {
        Fixture f; const auto reserved = f.settle(Reserve());
        const auto address = reserved->result()->address->address;
        f.storage.fail_clear = true;
        Command create; create.kind = Kind::Create; create.consume_address = true; create.reservation = reserved->address_intent;
        create.draft.address = address;
        const auto saved = f.settle(create);
        QVERIFY(saved->result()->request_saved); QCOMPARE(f.wallet->writes, 1);
        QVERIFY(f.storage.retired.contains(address)); QCOMPARE(f.storage.current, address);
        QCOMPARE(saved->result()->error, Error::Partial);
    }
    void failedRetirementDoesNotStartTheRequestWrite()
    {
        Fixture f; const auto reserved = f.settle(Reserve()); f.storage.fail_retire = true;
        Command create; create.kind = Kind::Create; create.consume_address = true; create.reservation = reserved->address_intent;
        create.draft.address = reserved->result()->address->address;
        const auto refused = f.settle(create);
        QCOMPARE(refused->result()->error, Error::ReservationPersistence); QCOMPARE(f.wallet->writes, 0);
    }
    void aQuestionDoesNotPinPhysicalOpeningDrain()
    {
        Fixture f; f.wallet->encrypted = true; f.wallet->locked = true;
        const auto question = f.settle(Reserve()); QCOMPARE(question->result()->error, Error::NeedsUnlock);
        f.store->close(); f.opening->retire(); f.client.reset(); f.commands.drain(); f.cleanup.drain();
        const auto closed = f.opening->take();
        QVERIFY(closed && closed->phase == walletopening::Phase::Closed); QVERIFY(question->address_intent);
    }
    void realWorkersRelockAndRetainTheOutcomeAfterCloseWithoutGuiDelivery_data()
    {
        QTest::addColumn<bool>("captured_long_secret");QTest::newRow("native-secure-string")<<false;QTest::newRow("captured-40000-character-literal")<<true;
    }
    void realWorkersRelockAndRetainTheOutcomeAfterCloseWithoutGuiDelivery()
    {
        QFETCH(bool,captured_long_secret);
        struct State {
            QSemaphore entered, release;
            std::atomic<QThread*> allocated{nullptr}, unlocked{nullptr}, relocked{nullptr}, storage{nullptr}, outcome{nullptr}, destroyed{nullptr};
            std::atomic_bool locked{false};
        };
        struct Native : Wallet {
            explicit Native(std::shared_ptr<State> state) : state(std::move(state)) { encrypted = true; locked = true; }
            ~Native() override { state->destroyed = QThread::currentThread(); }
            util::Expected<void, wallet::WalletError> unlock(const SecureString& password) override {
                state->unlocked = QThread::currentThread(); return Wallet::unlock(password);
            }
            bool lock() override {
                state->relocked = QThread::currentThread(); const bool result = Wallet::lock(); state->locked = locked; return result;
            }
            util::Result<CTxDestination> getNewDestination(OutputType type, const std::string& label) override {
                state->allocated = QThread::currentThread(); state->entered.release(); state->release.acquire();
                return Wallet::getNewDestination(type, label);
            }
            const std::shared_ptr<State> state;
        };
        auto state = std::make_shared<State>();
        const auto unblock = qScopeGuard([state] { state->release.release(); });
        BackendExecutor commands{execution::Limits::Bounded()}, retirement;
        auto wallet = std::make_shared<Native>(state);
        auto opening = std::make_shared<walletopening::Session>(commands.scheduler(), retirement.scheduler(),
            std::make_shared<std::atomic_bool>(false), [] {});
        auto client = opening->start(wallet).borrower;
        ReservationStorage storage;
        storage.read = [](interfaces::Wallet&, const QString&) { return StoredAddress{}; };
        storage.write = [state](interfaces::Wallet&, const QString&) { state->storage = QThread::currentThread(); return true; };
        auto store = std::make_shared<Store>(opening, [] {}, Store::Watch{}, QDateTime::currentDateTimeUtc, [] { return true; }, Store::Release{}, storage);
        Command load; load.kind = Kind::Reload; const auto loaded = store->submit(load); QVERIFY(loaded);
        QTRY_VERIFY(loaded.operation->result().has_value());
        auto command = Reserve();
        if(captured_long_secret){wallet->expected_password=std::string(40'000,'r');auto captured=secretinput::Capture(commands.scheduler(),QString::fromStdString(wallet->expected_password));QVERIFY(captured);command.captured_secret=std::move(captured.input);}
        else command.secret=std::make_shared<SecureString>("correct");
        const auto secret=command.secret;
        const auto admitted = store->submit(std::move(command), [state] { state->outcome = QThread::currentThread(); }); QVERIFY(admitted);
        QTRY_VERIFY(state->entered.available() > 0);
        QVERIFY(state->allocated.load() != qApp->thread());
        store->close(); opening->retire(); client.reset(); wallet.reset();
        bool heartbeat{false}; QMetaObject::invokeMethod(qApp, [&] { heartbeat = true; }, Qt::QueuedConnection);
        QTRY_VERIFY(heartbeat); QVERIFY(!admitted.operation->result());
        state->release.release();
        QTRY_VERIFY(admitted.operation->result().has_value());
        QVERIFY(admitted.operation->result()->address); if(secret)QVERIFY(secret->empty());
        commands.shutdown(); retirement.shutdown(); QTRY_VERIFY(commands.isDrained() && retirement.isDrained());
        QVERIFY(state->locked.load());
        // Physical drain deleted the QThread objects; only their captured
        // addresses remain valid observations for these equality checks.
        QCOMPARE(static_cast<const void*>(state->unlocked.load()),
                 static_cast<const void*>(state->allocated.load()));
        QCOMPARE(static_cast<const void*>(state->relocked.load()),
                 static_cast<const void*>(state->allocated.load()));
        QVERIFY(state->storage.load() && state->storage.load() != qApp->thread());
        QVERIFY(state->outcome.load() && state->outcome.load() != qApp->thread());
        QVERIFY(state->destroyed.load() && state->destroyed.load() != qApp->thread());
    }
};

#include <test_walletreservations.moc>
BITCOINQML_REGISTER_QT_TEST(WalletReservationTests)
