// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <QtTest/QtTest>
#include <test/mocks/mockwallet.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/admissionhookscheduler.h>
#include <test/qt_test_registry.h>
#include <qml/models/walletqmlmodel.h>
#include <key_io.h>
#include <util/string.h>
#include <addresstype.h>
#include <set>
#include <QPersistentModelIndex>
#include <QThread>
#include <QScopeGuard>
#include <QCoreApplication>
#include <QEvent>
#include <mutex>

namespace {
CTxDestination CoinDestination(uint8_t seed) { return WitnessV0KeyHash{uint160{std::vector<unsigned char>(20, seed)}}; }
class CoinWallet final : public StubWallet {
public:
    std::mutex mutex;
    CoinsList coins;
    bool fail_read{false};
    std::atomic<QThread*> read_thread{nullptr}, lock_thread{nullptr};
    std::atomic<size_t> read_calls{0};
    void setReadFailure(bool failed) { std::lock_guard lock{mutex}; fail_read = failed; }
    void clearCoins() { std::lock_guard lock{mutex}; coins.clear(); }
    std::set<COutPoint> locked;
    std::vector<bool> lock_calls;
    std::vector<bool> lockCalls() { std::lock_guard lock{mutex}; return lock_calls; }
    std::map<CTxDestination, std::string> labels;
    CoinsList listCoins() override { ++read_calls; read_thread = QThread::currentThread(); std::lock_guard lock{mutex}; if (fail_read) throw std::runtime_error{"Coin read failed"}; return coins; }
    bool isLockedCoin(const COutPoint& p) override { std::lock_guard lock{mutex}; return locked.contains(p); }
    bool lockCoin(const COutPoint& p, bool) override { lock_thread = QThread::currentThread(); std::lock_guard lock{mutex}; lock_calls.push_back(true); locked.insert(p); return true; }
    bool unlockCoin(const COutPoint& p) override { lock_thread = QThread::currentThread(); std::lock_guard lock{mutex}; lock_calls.push_back(false); locked.erase(p); return true; }
    bool getAddress(const CTxDestination& d, std::string* name, wallet::AddressPurpose*) override {
        std::lock_guard lock{mutex};
        if (!labels.contains(d)) return false;
        if (name) *name = labels[d];
        return true;
    }
    void add(int n, CAmount amount, int64_t date, uint8_t address, std::string label) {
        const auto dest = CoinDestination(address);
        labels[dest] = label;
        // Deliberately group change under a different ancestor address.
        coins[CoinDestination(99)].emplace_back(COutPoint{Txid{}, static_cast<uint32_t>(n)}, interfaces::WalletTxOut{CTxOut{amount, GetScriptForDestination(dest)}, date, 6, false});
    }
};
struct ManualCoins {
    ManualRoutedExecution commands, retirement;
    std::shared_ptr<CoinWallet> wallet{std::make_shared<CoinWallet>()};
    std::shared_ptr<walletopening::Session> opening;
    std::deque<std::function<void()>> publications;
    std::unique_ptr<CoinsListModel> model;
    explicit ManualCoins(int count = 1, bool initialize = true) {
        for (int i = 0; i < count; ++i) wallet->add(i, 1000 + i, 1700000000 + i, 1 + i % 2, "coin " + util::ToString(i));
        opening = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        opening->start(wallet); commands.drain(); opening->take();
        model = std::make_unique<CoinsListModel>();
        model->setOpening(opening, {commands.scheduler, [this](std::function<void()> turn) { publications.push_back(std::move(turn)); }});
        if (initialize) finish();
    }
    ~ManualCoins() {
        if (model) model->closeAdmission();
        commands.drain(); model.reset(); commands.drain();
        opening->retire(); commands.drain(); retirement.drain();
    }
    bool publishOne() {
        if (publications.empty()) return false;
        auto turn = std::move(publications.front()); publications.pop_front(); turn(); return true;
    }
    void finish() {
        do { commands.drain(); while (publishOne()) {} }
        while (!commands.queuedIdle() || !commands.activeIdle());
    }
};

}
class CoinsListModelTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void cleanupAdmissionKeepsOriginalCoinOwner_data() {
        QTest::addColumn<int>("reservation"); QTest::addColumn<int>("outcome");
        for (int reservation : {1, 2, 3}) for (int outcome : {0, 1, 2})
            QTest::newRow(qPrintable(QString::number(reservation) + QLatin1Char('-') + QString::number(outcome))) << reservation << outcome;
    }
    void cleanupAdmissionKeepsOriginalCoinOwner() {
        QFETCH(int, reservation); QFETCH(int, outcome);
        ManualRoutedExecution commands, retirement;
        auto scheduler = std::make_shared<AdmissionHookScheduler>(commands.scheduler);
        auto wallet = std::make_shared<CoinWallet>(); wallet->add(0, 7000, 1700000000, 1, "original coin");
        auto opening = std::make_shared<walletopening::Session>(scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        opening->start(wallet); commands.drain(); opening->take();
        std::deque<std::function<void()>> old_posts, current_posts;
        size_t old_posted{0}, current_posted{0};
        auto model = std::make_unique<CoinsListModel>();
        const auto settle = [&] {
            for (int turn{0}; turn < 512 && (turn == 0 || !commands.queuedIdle() || !retirement.queuedIdle() || !old_posts.empty() || !current_posts.empty()); ++turn) {
                commands.drain(); retirement.drain();
                for (auto* queue : {&old_posts, &current_posts}) while (!queue->empty()) {
                    auto work = std::move(queue->front()); queue->pop_front(); work();
                }
                QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
        };
        const auto cleanup = qScopeGuard([&] {
            scheduler->disarm(); if (model) model->closeAdmission(); model.reset(); settle();
            opening->retire(); settle();
        });
        auto trigger = scheduler->armCleanup(reservation, [&] {
            if (outcome == 0) model->closeAdmission();
            else if (outcome == 1) model.reset();
            else model->setOpening(opening, {scheduler, [&](auto work) { ++current_posted; current_posts.push_back(std::move(work)); }});
        });
        model->setOpening(opening, {scheduler, [&](auto work) { ++old_posted; old_posts.push_back(std::move(work)); }});
        QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
        settle();
        QCOMPARE(old_posted, size_t{0}); QVERIFY(wallet->lockCalls().empty());
        if (outcome == 2) {
            QVERIFY(model); QCOMPARE(model->rowCount(), 1); QCOMPARE(model->totalSatoshi(), qint64{7000});
            QCOMPARE(model->data(model->index(0), CoinsListModel::LabelRole).toString(), QStringLiteral("original coin"));
            QVERIFY(current_posted > 0); QCOMPARE(wallet->read_calls.load(), size_t{1});
        } else {
            QCOMPARE(wallet->read_calls.load(), size_t{0});
            if (outcome == 0) { QVERIFY(model); QCOMPARE(model->rowCount(), 0); }
            else QVERIFY(!model);
        }
        scheduler->disarm(); if (model) model->closeAdmission(); model.reset(); settle(); opening->retire(); settle();
        QCOMPARE(commands.scheduler->pending(), size_t{0}); QCOMPARE(retirement.scheduler->pending(), size_t{0});
    }
    void quietCloseDuringQtRows_data() {
        QTest::addColumn<int>("mutation"); QTest::addColumn<bool>("about");
        for (int mutation{0}; mutation < 3; ++mutation)
            for (bool about : {false, true}) QTest::newRow(qPrintable(QString::number(mutation) + (about ? "-about" : "-ended"))) << mutation << about;
    }
    void quietCloseDuringQtRows() {
        QFETCH(int, mutation); QFETCH(bool, about); ManualCoins f{3, mutation != 0};
        auto* model = f.model.get(); bool closed{false}; int at_end{-1};
        const auto close = [&] { const auto rows = model->rowCount(); model->closeAdmission(); QCOMPARE(model->rowCount(), rows); closed = true; };
        if (mutation == 0) {
            connect(model, about ? &QAbstractItemModel::rowsAboutToBeInserted : &QAbstractItemModel::rowsInserted, this, close);
            connect(model, &QAbstractItemModel::rowsInserted, this, [&] { at_end = model->rowCount(); });
        } else if (mutation == 1) {
            connect(model, about ? &QAbstractItemModel::rowsAboutToBeRemoved : &QAbstractItemModel::rowsRemoved, this, close);
            connect(model, &QAbstractItemModel::rowsRemoved, this, [&] { at_end = model->rowCount(); }); model->setSearchText("no matching coin");
        } else {
            connect(model, about ? &QAbstractItemModel::rowsAboutToBeMoved : &QAbstractItemModel::rowsMoved, this, close);
            connect(model, &QAbstractItemModel::rowsMoved, this, [&] { at_end = model->rowCount(); }); model->setSortDescending(false);
        }
        f.finish(); QVERIFY(closed); QCOMPARE(at_end, mutation == 0 ? 1 : mutation == 1 ? 2 : 3); QCOMPARE(model->rowCount(), 0);
        f.opening->retire(); f.commands.scheduler->closeAdmission(); f.commands.drain(); f.retirement.drain();
        QCOMPARE(f.commands.scheduler->pending(), size_t{0});
    }
    void quietCloseReleasesIdleSnapshotsBeforeFacadeDestruction() {
        ManualCoins fixture{129};
        fixture.model->setSortBy("amount"); fixture.finish();
        QCOMPARE(fixture.model->rowCount(), 129);
        fixture.model->closeAdmission();
        QCOMPARE(fixture.model->rowCount(), 0);
        QVERIFY(!fixture.model->selectionSnapshot());
        fixture.opening->retire(); fixture.commands.scheduler->closeAdmission();
        fixture.commands.drain(); fixture.retirement.drain();
        // Keeping the closed facade alive must not keep its Native value
        // ownership reservations alive or block the executor's final drain.
        QVERIFY(fixture.model);
        QCOMPARE(fixture.commands.scheduler->pending(), size_t{0});
    }
    void manualNativeDomainAndBoundedGuiPublication() {
        ManualCoins fixture{257, false};
        QCOMPARE(fixture.model->rowCount(), 0); QCOMPARE(fixture.model->coinCount(), 0);
        fixture.commands.drain();
        QCOMPARE(fixture.model->rowCount(), 0); QCOMPARE(fixture.model->totalSatoshi(), 0);
        QVERIFY(fixture.publishOne());
        QVERIFY(fixture.model->rowCount() <= 32); QVERIFY(fixture.model->loading());
        int previous = fixture.model->rowCount();
        while (fixture.publishOne()) {
            QVERIFY(std::abs(fixture.model->rowCount() - previous) <= 32);
            previous = fixture.model->rowCount();
        }
        QCOMPARE(fixture.model->rowCount(), 257); QCOMPARE(fixture.model->coinCount(), 257);
        QCOMPARE(fixture.model->totalSatoshi(), qint64{257 * 1000 + 256 * 257 / 2});
        QVERIFY(!fixture.model->loading());
    }
    void manualReorderPreservesPersistentIdentityAndFencesOldControls() {
        ManualCoins fixture{129}; auto& coins = *fixture.model;
        QPersistentModelIndex persistent{coins.index(0)};
        const auto id = persistent.data(CoinsListModel::CoinIdRole);
        const auto amount = persistent.data(CoinsListModel::AmountSatoshiRole);
        coins.setGroupBy("address"); coins.setSortBy("amount"); coins.setSortDescending(false);
        fixture.commands.drain();
        coins.setSearchText("no matching coin");
        // A held publication from prior controls cannot publish its old list.
        while (fixture.publishOne()) {}
        QCOMPARE(coins.rowCount(), 129);
        fixture.finish(); QCOMPARE(coins.rowCount(), 0); QVERIFY(!persistent.isValid());
        coins.setSearchText(""); fixture.finish();
        QPersistentModelIndex surviving{coins.index(0)};
        const auto survivor = surviving.data(CoinsListModel::CoinIdRole);
        coins.setSortDescending(true); fixture.finish();
        QVERIFY(surviving.isValid()); QCOMPARE(surviving.data(CoinsListModel::CoinIdRole), survivor);
        QVERIFY(!id.toString().isEmpty()); QVERIFY(amount.toLongLong() > 0);
    }
    void iterativeRowEditsPreserveEveryPersistentIdentity() {
        ManualCoins fixture{513};auto& coins=*fixture.model;
        std::vector<QPersistentModelIndex> original;
        std::vector<QVariant> identities;std::vector<qint64> amounts;
        original.reserve(513);identities.reserve(513);amounts.reserve(513);
        for(int row=0;row<coins.rowCount();++row){
            original.emplace_back(coins.index(row));
            identities.push_back(original.back().data(CoinsListModel::CoinIdRole));
            amounts.push_back(original.back().data(CoinsListModel::AmountSatoshiRole).toLongLong());
        }
        coins.setSortBy("amount");coins.setSortDescending(false);fixture.finish();
        QCOMPARE(coins.rowCount(),513);
        for(int row=0;row<coins.rowCount();++row)
            QCOMPARE(coins.data(coins.index(row),CoinsListModel::AmountSatoshiRole).toLongLong(),qint64{1000+row});
        coins.setSortDescending(true);fixture.finish();
        for(size_t row=0;row<original.size();++row){
            QVERIFY(original[row].isValid());
            QCOMPARE(original[row].data(CoinsListModel::CoinIdRole),identities[row]);
        }
        // Remove both leaf and internal nodes from the balanced sequence.
        coins.setMinAmount(1256);fixture.finish();QCOMPARE(coins.rowCount(),257);
        for(size_t row=0;row<original.size();++row){
            if(amounts[row]>=1256){QVERIFY(original[row].isValid());QCOMPARE(original[row].data(CoinsListModel::CoinIdRole),identities[row]);}
            else QVERIFY(!original[row].isValid());
        }
        coins.setMinAmount(-1);fixture.finish();QCOMPARE(coins.rowCount(),513);
        for(int row=0;row<coins.rowCount();++row)
            QCOMPARE(coins.data(coins.index(row),CoinsListModel::AmountSatoshiRole).toLongLong(),qint64{1512-row});
    }
    void rejectedProjectionPreservesTheLastCompleteInventory() {
        ManualCoins fixture{9}; auto& coins = *fixture.model;
        fixture.commands.rejectLane(execution::Lane::Domain);
        coins.setSearchText("no match"); while (fixture.publishOne()) {}
        QCOMPARE(coins.coinCount(), 9); QCOMPARE(coins.rowCount(), 9); QVERIFY(!coins.readError().isEmpty());
        coins.setSearchText("coin"); fixture.finish();
        QCOMPARE(coins.rowCount(), 9); QVERIFY(coins.readError().isEmpty());
    }
    void selectionCommandsUseOriginalDomainOrderAndNullExplicitlyClears() {
        ManualCoins fixture{3}; auto& coins = *fixture.model;
        const auto id = coins.data(coins.index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins.toggleCoinSelectionById(id)); coins.beginSelection();
        QVERIFY(coins.toggleCoinSelectionById(id)); coins.cancelSelection();
        QVERIFY(coins.selectionPending()); QVERIFY(coins.selectionSnapshot()->empty());
        fixture.finish();
        QCOMPARE(coins.selectedCoinsCount(), 1); QCOMPARE(coins.selectionSnapshot()->size(), size_t{1});
        const auto original = coins.selectionSnapshot();
        QVERIFY(coins.requestReplaceSelection({})); fixture.finish();
        QVERIFY(coins.selectionSnapshot()->empty()); QCOMPARE(coins.selectedCoinsCount(), 0);
        QCOMPARE(original->size(), size_t{1});
    }
    void markedBatchRetainsFilteredOutIdentitiesAndClearsOnAdmission() {
        ManualCoins fixture{3}; auto& coins = *fixture.model;
        const auto first = coins.data(coins.index(0), CoinsListModel::CoinIdRole).toString();
        const auto second = coins.data(coins.index(1), CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins.toggleMarkById(first)); QVERIFY(coins.toggleMarkById(second));
        QVERIFY(coins.marksPending()); fixture.finish(); QCOMPARE(coins.markedCount(), 2);
        // Address labels are shared by all coins paying that destination.
        // Filter by the original unique coin identity to hide exactly the
        // other marked coin, independently of repeated fixture destinations.
        coins.setSearchText(second); fixture.finish(); QCOMPARE(coins.rowCount(), 1);
        QCOMPARE(coins.data(coins.index(0), CoinsListModel::CoinIdRole).toString(), second);
        QVERIFY(coins.data(coins.index(0), CoinsListModel::MarkedRole).toBool());
        QCOMPARE(coins.markedCount(), 2);
        QVERIFY(coins.lockMarkedCoins(true)); fixture.finish();
        QCOMPARE(fixture.wallet->lockCalls(), std::vector<bool>({true, true}));
        QCOMPARE(coins.markedCount(), 0); QVERIFY(!coins.lockPending());
        QVERIFY(coins.lockError().isEmpty()); QCOMPARE(coins.lockedCoinsCount(), 2);
    }
    void cancellingMarksOrdersAfterUnpublishedToggleAndPreservesSpendSelection() {
        ManualCoins fixture{3}; auto& coins = *fixture.model;
        const auto id = coins.data(coins.index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins.toggleCoinSelectionById(id)); fixture.finish();
        const auto selected = coins.selectionSnapshot();
        QVERIFY(coins.toggleMarkById(id)); QVERIFY(coins.clearMarks()); fixture.finish();
        QCOMPARE(coins.markedCount(), 0); QCOMPARE(coins.selectionSnapshot(), selected);
        QVERIFY(!coins.data(coins.index(0), CoinsListModel::MarkedRole).toBool());
        QCOMPARE(coins.selectedCoinsCount(), 1);
    }
    void failedNativeReadCannotEraseSelectionOrSuccessfulRows() {
        ManualCoins fixture{3}; auto& coins = *fixture.model;
        const auto id = coins.data(coins.index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins.toggleCoinSelectionById(id)); fixture.finish();
        const auto selected = coins.selectionSnapshot(); QCOMPARE(selected->size(), size_t{1});
        fixture.wallet->setReadFailure(true); coins.update(); fixture.finish();
        QCOMPARE(coins.rowCount(), 3); QCOMPARE(coins.coinCount(), 3);
        QCOMPARE(coins.selectionSnapshot(), selected); QCOMPARE(coins.selectedCoinsCount(), 1);
        QVERIFY(!coins.readError().isEmpty());
    }
    void acceptedSelectionRetainsOriginalOutcomeAfterObserverLoss() {
        ManualCoins fixture{3};
        const COutPoint original{Txid{}, 0};
        QVERIFY(fixture.model->requestSelectionDelta(original, true));
        const auto owner = fixture.model->selectionOperation(); QVERIFY(owner);
        fixture.model->closeAdmission(); fixture.model.reset(); fixture.opening->closeAdmission();
        fixture.commands.drain();
        QVERIFY(owner->result()); QVERIFY(owner->result()->value);
        QVERIFY(owner->result()->value->selection->contains(original));
        QCOMPARE(owner->result()->value->revision, quint64{1});
    }
    void consumedLockCompletionCannotRetainItsOwnCleanupLease() {
        ManualCoins fixture;
        auto* coins=fixture.model.get();
        const auto id=coins->data(coins->index(0),CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins->setCoinsLocked({id},true));
        std::weak_ptr<const execution::OwnedCommand<CoinsListModel::LockOutcome>> completed;
        {
            const auto owner=coins->lockOperation();QVERIFY(owner);completed=owner;
            fixture.finish();
            const auto result=owner->result();QVERIFY(result);QVERIFY(result->value);
            QCOMPARE(result->value->requested,size_t{1});QCOMPARE(result->value->changed,size_t{1});
            QVERIFY(!result->value->failed);QVERIFY(!result->value->uncertain);
            QCOMPARE(fixture.wallet->lockCalls(),std::vector<bool>{true});
        }
        // The GUI may retain the actual outcome until close. Afterward no
        // persistent completion may keep its own result/selection lease alive.
        QVERIFY(!completed.expired());
        coins->closeAdmission();fixture.model.reset();
        fixture.opening->retire();fixture.commands.scheduler->closeAdmission();
        fixture.finish();fixture.retirement.drain();fixture.commands.drain();
        QVERIFY(completed.expired());
        QCOMPARE(fixture.wallet->lockCalls(),std::vector<bool>{true});
        QCOMPARE(fixture.commands.scheduler->pending(),size_t{0});
        QCOMPARE(fixture.retirement.scheduler->pending(),size_t{0});
    }
    void admittedLockBatchDrainsInBoundedChunksAfterObserverClose() {
        ManualCoins fixture{97}; auto& coins = *fixture.model;
        QStringList ids; for (int i = 0; i < coins.rowCount(); ++i) ids.push_back(coins.data(coins.index(i), CoinsListModel::CoinIdRole).toString());
        QVERIFY(coins.setCoinsLocked(ids, true));
        const auto owner = coins.lockOperation(); QVERIFY(owner);
        QCOMPARE(fixture.wallet->lockCalls().size(), size_t{0});
        QVERIFY(fixture.commands.step(execution::Lane::Domain));
        QVERIFY(fixture.commands.step(execution::Lane::Core));
        QCOMPARE(fixture.wallet->lockCalls().size(), size_t{32});
        fixture.model->closeAdmission(); fixture.model.reset(); fixture.opening->closeAdmission();
        fixture.commands.drain();
        QCOMPARE(fixture.wallet->lockCalls().size(), size_t{97});
        QVERIFY(owner->result()); QVERIFY(owner->result()->value);
        QCOMPARE(owner->result()->value->changed, size_t{97});
        QVERIFY(!owner->result()->value->uncertain);
    }
    void rejectedContinuationRetainsActualPartialLockOutcome() {
        ManualCoins fixture{97}; auto& coins = *fixture.model;
        QStringList ids; for (int i = 0; i < coins.rowCount(); ++i) ids.push_back(coins.data(coins.index(i), CoinsListModel::CoinIdRole).toString());
        fixture.commands.rejectCoreNumber(fixture.commands.coreSubmissions() + 2);
        QVERIFY(coins.setCoinsLocked(ids, true)); const auto owner = coins.lockOperation();
        fixture.finish();
        QVERIFY(owner->result()); QVERIFY(owner->result()->value);
        QCOMPARE(owner->result()->value->changed, size_t{32}); QVERIFY(owner->result()->value->failed); QVERIFY(!owner->result()->value->uncertain);
        QCOMPARE(fixture.wallet->lockCalls().size(), size_t{32}); QVERIFY(!coins.lockError().isEmpty());
    }
    void oppositeLockIntentsAreNotSuppressedByTheCachedRow() {
        auto wallet = std::make_unique<CoinWallet>();
        auto* source = wallet.get();
        wallet->add(0, 1000, 1700000000, 1, "First");
        WalletQmlModel model{std::move(wallet)};
        auto* coins = model.coinsListModel();
        QTRY_COMPARE(coins->coinCount(), 1);
        const auto id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(!coins->data(coins->index(0), CoinsListModel::LockedRole).toBool());
        // No GUI publication occurs between these commands. Both refer to the
        // same initially unlocked cached row, regardless of native timing.
        QVERIFY(coins->setCoinsLocked({id}, true));
        QVERIFY(coins->setCoinsLocked({id}, false));
        QTRY_VERIFY(!coins->lockPending());
        QVERIFY2(coins->lockError().isEmpty(), qPrintable(coins->lockError()));
        QCOMPARE(source->lockCalls(), std::vector<bool>({true, false}));
        QVERIFY(source->read_thread.load() != QThread::currentThread());
        QVERIFY(source->lock_thread.load() != QThread::currentThread());
        QTRY_VERIFY(!coins->data(coins->index(0), CoinsListModel::LockedRole).toBool());
    }
    void stableIdentitySurvivesReorderAndRejectsRemovedCoin() {
        auto wallet = std::make_unique<CoinWallet>();
        auto* source = wallet.get();
        wallet->add(0, 1000, 1700000000, 1, "First");
        wallet->add(1, 2000, 1700000010, 2, "Second");
        WalletQmlModel model{std::move(wallet)};
        auto* coins = model.coinsListModel();
        QTRY_COMPARE(coins->coinCount(), 2);
        coins->setSortBy("amount");
        coins->setSortDescending(false);
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::AmountSatoshiRole).toLongLong(), 1000);
        const QString coin_id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();

        coins->setSortDescending(true);
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::AmountSatoshiRole).toLongLong(), 2000);
        QVERIFY(coins->toggleCoinSelectionById(coin_id));
        QTRY_COMPARE(coins->totalSelectedSatoshi(), 1000);

        source->clearCoins();
        coins->update();
        QTRY_COMPARE(coins->coinCount(), 0);
        QVERIFY(!coins->toggleCoinSelectionById(coin_id));
        QTRY_COMPARE(coins->selectedCoinsCount(), 0);
    }

    void filteringKeepsHiddenSelectionAndCancelRestoresInputs() {
        auto wallet = std::make_unique<CoinWallet>();
        wallet->add(0, 1000, 1700000000, 1, "Rent");
        wallet->add(1, 2000, 1700000010, 2, "Savings");
        WalletQmlModel model{std::move(wallet)};
        auto* coins = model.coinsListModel();
        QTRY_COMPARE(coins->coinCount(), 2);
        coins->setSortDescending(false);
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::AddressRole).toString(), QString::fromStdString(EncodeDestination(CoinDestination(1))));
        coins->toggleCoinSelection(0);
        QTRY_COMPARE(coins->totalSelectedSatoshi(), 1000);
        coins->beginSelection();
        coins->setSearchText("SAV");
        QTRY_COMPARE(coins->rowCount(), 1);
        QTRY_COMPARE(coins->totalSelectedSatoshi(), 1000);
        coins->toggleCoinSelection(0);
        QTRY_COMPARE(coins->totalSelectedSatoshi(), 3000);

        coins->setSearchText(QString::fromStdString(EncodeDestination(CoinDestination(1))));
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::AddressRole).toString(), QString::fromStdString(EncodeDestination(CoinDestination(1))));
        QTRY_COMPARE(coins->rowCount(), 1);
        const QString coin_id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();
        coins->setSearchText(coin_id.left(12));
        QTRY_COMPARE(coins->rowCount(), 2); // The mock outputs share the same transaction id.
        coins->cancelSelection();
        QTRY_COMPARE(coins->totalSelectedSatoshi(), 1000);
        coins->setSearchText("SAV");
        QTRY_VERIFY(!coins->loading());
        QTRY_COMPARE(coins->rowCount(), 1);
        coins->beginSelection();
        coins->toggleCoinSelection(0);
        coins->applySelection();
        coins->cancelSelection();
        QTRY_COMPARE(coins->totalSelectedSatoshi(), 3000);
    }
    void sortingGroupingAndLocksUseStableCoinIdentity() {
        auto wallet = std::make_unique<CoinWallet>();
        auto* source = wallet.get();
        wallet->add(0, 3000, 1700000000, 1, "Zulu");
        wallet->add(1, 1000, 1700000010, 1, "Zulu");
        wallet->add(2, 2000, 1700000020, 2, "Alpha");
        WalletQmlModel model{std::move(wallet)};
        auto* coins = model.coinsListModel();
        QTRY_COMPARE(coins->coinCount(), 3);
        coins->setGroupBy("date");
        coins->setSortBy("amount"); coins->setSortDescending(false);
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::AmountSatoshiRole).toLongLong(), 1000);
        coins->setSortDescending(true);
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::AmountSatoshiRole).toLongLong(), 3000);
        coins->setSortDescending(false);
        coins->setSortBy("label");
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::LabelRole).toString(), QString("Alpha"));
        coins->setGroupBy("address");
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::GroupRole), coins->data(coins->index(0), CoinsListModel::AddressRole));
        QTRY_VERIFY(coins->data(coins->index(0), CoinsListModel::GroupFirstRole).toBool());
        QVERIFY(coins->data(coins->index(0), CoinsListModel::GroupLastRole).toBool());
        QVERIFY(coins->data(coins->index(1), CoinsListModel::GroupFirstRole).toBool());
        QVERIFY(!coins->data(coins->index(1), CoinsListModel::GroupLastRole).toBool());
        QVERIFY(coins->data(coins->index(2), CoinsListModel::GroupLastRole).toBool());
        coins->toggleCoinSelection(0);
        const auto id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins->setCoinsLocked({id}, true));
        QTRY_VERIFY(!coins->lockPending());
        QVERIFY2(coins->lockError().isEmpty(), qPrintable(coins->lockError()));
        QTRY_COMPARE(coins->selectedCoinsCount(), 0);
        coins->setFilter("locked"); QTRY_COMPARE(coins->rowCount(), 1);
        coins->toggleCoinSelection(0); QTRY_COMPARE(coins->selectedCoinsCount(), 1);
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString(), id);
        QVERIFY(coins->setCoinsLocked({id}, false));
        QTRY_VERIFY(!coins->lockPending());
        QVERIFY2(coins->lockError().isEmpty(), qPrintable(coins->lockError()));
        QTRY_COMPARE(coins->rowCount(), 0);
        coins->setFilter("spendable"); QTRY_COMPARE(coins->rowCount(), 3);
        coins->setMinAmount(1500); QTRY_COMPARE(coins->rowCount(), 2);
        coins->setMaxAmount(2500); QTRY_COMPARE(coins->rowCount(), 1);
        coins->setMinAmount(-1); coins->setMaxAmount(-1); QTRY_COMPARE(coins->rowCount(), 3);
        coins->setGroupBy("date");
        QVERIFY(!coins->data(coins->index(0), CoinsListModel::GroupRole).toString().isEmpty());
        coins->toggleCoinSelection(0);
        source->clearCoins(); coins->update();
        QTRY_COMPARE(coins->coinCount(), 0);
        QTRY_COMPARE(coins->selectedCoinsCount(), 0);
        QTRY_COMPARE(coins->totalSatoshi(), 0);
    }
};
BITCOINQML_REGISTER_QT_TEST(CoinsListModelTests)
#include <test_coinslistmodel.moc>
