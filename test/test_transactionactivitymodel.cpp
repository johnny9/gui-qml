// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/mocks/sendtest.h>
#include <test/mocks/approvaltest.h>
#include <qml/models/receiverequestoperation.h>
#include <test/mocks/preparationtest.h>
#include <qml/models/activityfilterproxymodel.h>
#include <qml/walletrequestdata.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <QClipboard>
#include <QGuiApplication>
#include <qml/models/transactionactivitymodel.h>
#include <qml/models/activitydetailmodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/guipresentation.h>
#include <test/mocks/mockwallet.h>

#include <core_io.h>
#include <key_io.h>
#include <util/strencodings.h>
#include <script/solver.h>

#include <QAbstractItemModelTester>
#include <QFile>
#include <QPersistentModelIndex>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <thread>
#include <atomic>
#include <QSemaphore>
#include <stdexcept>

namespace {
using Model = TransactionActivityModel;
using Proxy = ActivityFilterProxyModel;

struct Output { CAmount amount; bool mine; bool change{false}; unsigned char key{1}; };

interfaces::WalletTx MakeTx(const std::vector<std::pair<CAmount, bool>>& inputs,
                          const std::vector<Output>& outputs, qint64 time = 1'789'200'000)
{
    interfaces::WalletTx wtx{};
    CMutableTransaction tx;
    for (const auto& [amount, mine] : inputs) {
        tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256{uint8_t(tx.vin.size() + 1)}), 0});
        wtx.txin_is_mine.push_back(mine);
        if (mine) wtx.debit += amount;
    }
    for (const auto& output : outputs) {
        uint160 key;
        key.begin()[0] = output.key;
        const CTxDestination address{PKHash{key}};
        tx.vout.emplace_back(output.amount, GetScriptForDestination(address));
        wtx.txout_address.push_back(address);
        wtx.txout_address_is_mine.push_back(output.mine);
        wtx.txout_is_mine.push_back(output.mine);
        wtx.txout_is_change.push_back(output.change);
        if (output.mine) wtx.credit += output.amount;
        if (output.change) wtx.change += output.amount;
    }
    wtx.tx = MakeTransactionRef(tx);
    wtx.time = time;
    return wtx;
}

QString Id(const interfaces::WalletTx& tx) { return QString::fromStdString(tx.tx->GetHash().ToString()); }
QString Address(const interfaces::WalletTx& tx, int output = 0) { return QString::fromStdString(EncodeDestination(tx.txout_address[output])); }
QString UnpaidAddress(uint8_t marker = 251)
{
    return QString::fromStdString(EncodeDestination(PKHash{uint160{std::vector<unsigned char>(20, marker)}}));
}
qint64 Time(int month, int day, int hour = 12) { return QDateTime(QDate(2026, month, day), QTime(hour, 3)).toSecsSinceEpoch(); }

QmlRecentRequestEntry Request(int id, const QString& address, const QString& label = "Rent", CAmount amount = 50'000)
{
    QmlRecentRequestEntry entry;
    entry.id = id;
    entry.date = QDateTime::fromSecsSinceEpoch(Time(9, 11, 9));
    entry.recipient.address = address.toStdString();
    entry.recipient.label = label.toStdString();
    entry.recipient.amount = amount;
    return entry;
}

struct ReadGate {
    QSemaphore entered, release;
    std::atomic_bool enabled{true};
    void wait() { if (enabled.exchange(false)) { entered.release(); release.acquire(); } }
};

class ActivityWallet : public StubWallet
{
public:
    std::map<Txid, interfaces::WalletTx> transactions;
    std::map<Txid, interfaces::WalletTxStatus> statuses;
    std::map<std::string, std::string> labels;
    std::map<int64_t, std::string> request_blobs;
    std::vector<TransactionChangedFn> callbacks;
    std::map<std::string, wallet::AddressPurpose> purposes;
    std::optional<interfaces::WalletTx> send_draft;
    CTransactionRef previous_tx;
    std::map<Txid, CTransactionRef> flow_parents;
    int parent_reads{0};
    int bump_reads{0};
    std::atomic_bool blocking_read_on_gui{false};
    bool fail_history{false};
    std::shared_ptr<ReadGate> history_gate, parent_gate, bump_gate;
    void blockingRead() { if (QThread::currentThread() == qApp->thread()) blocking_read_on_gui = true; }
    bool external_signer{false};
    int address_writes{0};
    int commits{0};
    std::atomic_bool busy{false};
    bool snapshot_on_worker{false};
    std::atomic<uint8_t> tip{1};
    int status_reads{0};
    int label_reads{0};
    int snapshots{0};

    void put(const interfaces::WalletTx& tx, int depth = 6)
    {
        transactions[tx.tx->GetHash()] = tx;
        auto& status = statuses[tx.tx->GetHash()];
        status = {};
        status.depth_in_main_chain = depth;
        status.is_in_main_chain = depth > 0;
    }
    void notify(const interfaces::WalletTx& tx, ChangeType change)
    {
        for (const auto& callback : callbacks) callback(tx.tx->GetHash(), change);
    }
    std::vector<std::string> getAddressReceiveRequests() override {
        blockingRead(); std::vector<std::string> result; result.reserve(request_blobs.size()); for (const auto& [id, blob] : request_blobs) result.push_back(blob); return result;
    }
    bool setAddressReceiveRequest(const CTxDestination&, const std::string& key, const std::string& value) override {
        const auto id = ToIntegral<int64_t>(key); if (!id) throw std::invalid_argument("Invalid receive request id");
        if (value.empty()) request_blobs.erase(*id); else request_blobs[*id] = value; return true;
    }
    std::set<interfaces::WalletTx> getWalletTxs() override
    {
        blockingRead();
        std::set<interfaces::WalletTx> result;
        for (const auto& [id, tx] : transactions) result.insert(tx);
        if (history_gate) history_gate->wait();
        if (fail_history) throw std::runtime_error("wallet read failed");
        return result;
    }
    interfaces::WalletTx getWalletTx(const Txid& id) override
    {
        blockingRead();
        ++snapshots;
        snapshot_on_worker |= QThread::currentThread() != qApp->thread();
        const auto it = transactions.find(id);
        return it == transactions.end() ? interfaces::WalletTx{} : it->second;
    }
    CTransactionRef getTx(const Txid& id) override
    {
        blockingRead();
        if (parent_gate) parent_gate->wait();
        ++parent_reads;
        const auto it = flow_parents.find(id);
        return it == flow_parents.end() ? CTransactionRef{} : it->second;
    }
    bool tryGetTxStatus(const Txid& id, interfaces::WalletTxStatus& status, int& height, int64_t& time) override
    {
        ++status_reads;
        if (busy || !statuses.contains(id)) return false;
        status = statuses.at(id);
        height = 100;
        time = 1'789'200'000;
        return true;
    }
    bool tryGetBalances(interfaces::WalletBalances& balances, uint256& hash) override
    {
        if (busy) return false;
        hash = uint256{tip.load()};
        balances.balance = 10 * COIN;
        return true;
    }
    bool getAddress(const CTxDestination& address, std::string* label, wallet::AddressPurpose* purpose) override
    {
        blockingRead();
        ++label_reads;
        if (!labels.contains(EncodeDestination(address))) return false;
        if (label) *label = labels.at(EncodeDestination(address));
        if (purpose) *purpose = purposes[EncodeDestination(address)];
        return true;
    }
    bool setAddressBook(const CTxDestination& address, const std::string& label,
                        const std::optional<wallet::AddressPurpose>& purpose) override
    {
        ++address_writes;
        labels[EncodeDestination(address)] = label;
        if (purpose) purposes[EncodeDestination(address)] = *purpose;
        return true;
    }
    CAmount getAvailableBalance(const wallet::CCoinControl&) override { return 10 * COIN; }
    CAmount getBalance() override { return 10 * COIN; }
    bool privateKeysDisabled() override { return external_signer; }
    bool hasExternalSigner() override { return external_signer; }
    util::Result<wallet::CreatedTransactionResult> createTransaction(const std::vector<wallet::CRecipient>&,
        const wallet::CCoinControl&, bool, std::optional<unsigned int>) override
    {
        if (!send_draft) return util::Error{Untranslated("No send draft")};
        return wallet::CreatedTransactionResult{send_draft->tx, 1'000, 3, FeeReason::FEE_RATE_ESTIMATOR};
    }
    std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions& options, size_t*,
                                            PartiallySignedTransaction& psbt, bool& complete) override
    {
        complete = options.sign;
        for (auto& input : psbt.inputs) {
            input.non_witness_utxo = previous_tx;
            if (complete) input.final_script_sig = CScript{} << std::vector<unsigned char>{};
        }
        return std::nullopt;
    }
    void commitTransaction(CTransactionRef tx, const std::vector<std::string>&) override
    {
        ++commits;
        send_draft->tx = std::move(tx);
        put(*send_draft, 0);
        notify(*send_draft, CT_NEW);
    }
    bool transactionCanBeBumped(const Txid&) override
    {
        blockingRead();
        if (bump_gate) bump_gate->wait();
        ++bump_reads;
        return true;
    }
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn callback) override
    {
        callbacks.push_back(std::move(callback));
        return {};
    }
};

class StatusFallbackWallet final : public ActivityWallet
{
public:
    std::atomic<int> fallback_depth{6},blocking_status_reads{0};
    std::shared_ptr<ReadGate> status_gate;
    interfaces::WalletTx getWalletTxDetails(const Txid& id,interfaces::WalletTxStatus& status,
        std::vector<std::string>&,std::vector<std::string>&,bool& in_mempool,int& height) override
    {
        blockingRead();++blocking_status_reads;
        if(status_gate)status_gate->wait();
        const auto found=transactions.find(id);if(found==transactions.end())return {};
        status=statuses.at(id);status.depth_in_main_chain=fallback_depth.load();status.is_in_main_chain=status.depth_in_main_chain>0;
        in_mempool=false;height=100;return found->second;
    }
};

void Wait(Model* model)
{
    QVERIFY2(QTest::qWaitFor([&] { return !model->loading(); }, 5000), "activity worker did not finish");
}

void WaitProxy(Proxy& proxy) { QVERIFY(QTest::qWaitFor([&] { return !proxy.busy(); }, 5000)); QVERIFY2(proxy.error().isEmpty(), qPrintable(proxy.error())); }
bool Export(Proxy& proxy, const QString& path) { WaitProxy(proxy); const auto ticket = proxy.requestExportCsv(path); return ticket && QTest::qWaitFor([&] { return !proxy.exportBusy(); }, 5000) && proxy.exportError().isEmpty(); }

struct Fixture {
    ActivityWallet* state;
    std::weak_ptr<interfaces::Wallet> native_lifetime;
    std::unique_ptr<WalletQmlModel> wallet;
    explicit Fixture(std::shared_ptr<ActivityWallet> backend=std::make_shared<ActivityWallet>())
    {
        state = backend.get(); native_lifetime = backend;
        wallet = std::make_unique<WalletQmlModel>(WalletQmlModel::SharedWallet{backend}, nullptr,
            FeeReadServices{}, psbtfee::Services{}, backup::Services{});
        // Scope the thread assertion to activity operations, after the existing
        // address-book model has loaded during wallet construction.
        state->blocking_read_on_gui = false;
    }
    ~Fixture()
    {
        if (!wallet) return;
        QSignalSpy drained{wallet.get(), &WalletQmlModel::shutdownFinished};
        wallet->beginShutdown();
        if (!wallet->backendExecutor()->isDrained() && drained.empty()) QVERIFY(drained.wait(10000));
    }
    Model* model() { auto* result = wallet->transactionActivityModel(); Wait(result); return result; }
    void requests(const std::vector<QmlRecentRequestEntry>& values, bool replace) {
        auto done = std::make_shared<std::atomic_bool>(false);
        const auto admitted = wallet->opening()->read([values, replace](interfaces::Wallet& backend) {
            auto& state = dynamic_cast<ActivityWallet&>(backend); if (replace) state.request_blobs.clear();
            for (const auto& value : values) state.request_blobs[value.id] = walletrequests::Encode(value);
            return true;
        }, [done](bool) { done->store(true); }, [done](std::exception_ptr) { done->store(true); }, execution::WorkClass::Action);
        QVERIFY(admitted); QVERIFY(QTest::qWaitFor([done] { return done->load(); }, 5000));
        wallet->reloadReceiveRequests();
        QVERIFY(QTest::qWaitFor([&] { const auto snapshot = wallet->receiveRequests()->snapshot(); if (!snapshot) return false; for (const auto& value : values) { const auto* row = snapshot->find(value.id); if (!row || row->entry.recipient.noteSelf != value.recipient.noteSelf || row->entry.recipient.label != value.recipient.label) return false; } return !wallet->property("receiveRequestsBusy").toBool(); }, 5000));
        if (auto* activity = wallet->findChild<Model*>()) Wait(activity);
    }
    void request(const QmlRecentRequestEntry& value) { requests({value}, false); }
};

struct ManualActivityFixture {
    ManualExecutionEnvironment& environment;
    std::shared_ptr<ActivityWallet> state;
    std::unique_ptr<WalletQmlModel> wallet;
    std::unique_ptr<Model> activity;
    std::shared_ptr<BackendExecutor> backend;
    std::function<void()> poll;
    ManualActivityFixture(ManualExecutionEnvironment& env, std::shared_ptr<ActivityWallet> original)
        : environment(env), state(std::move(original)) {
        const auto post = [&env](auto callback) { env.post(std::move(callback)); };
        FeeReadServices fees; fees.scheduler = env.scheduler; fees.post = post; fees.timer = std::shared_ptr<FeeReadTimer>(&env, [](auto*) {});
        wallet = std::make_unique<WalletQmlModel>(WalletQmlModel::SharedWallet{state, env.scheduler}, nullptr, std::move(fees), psbtfee::Services{}, backup::Services{});
        backend = wallet->backendExecutor();
        if (!QTest::qWaitFor([&] { settle(); return wallet->capabilitiesKnown() && wallet->property("receiveRequestsLoaded").toBool() && !wallet->receiveRequestReconciliationPending(); }, 5000)) QTest::qFail("Manual activity opening did not settle", __FILE__, __LINE__);
        Model::Services services; services.post = post;
        services.schedule_poll = [this](auto, auto callback) { poll = std::move(callback); }; services.cancel_poll = [this] { poll = {}; };
        activity = std::make_unique<Model>(wallet.get(), std::move(services));
    }
    void settle() {
        for (int round = 0; round < 64; ++round) { environment.drain(); environment.publishAll(); QCoreApplication::processEvents(); if (environment.turnsIdle() && environment.publicationsIdle() && (!activity || !activity->loading())) return; }
    }
    ~ManualActivityFixture() {
        activity.reset(); wallet->beginShutdown();
        if (!QTest::qWaitFor([&] { settle(); return backend->isDrained() && environment.turnsIdle() && environment.publicationsIdle(); }, 10000)) QTest::qFail("Manual activity did not drain", __FILE__, __LINE__);
        wallet.reset(); settle();
    }
};

QModelIndex Find(const QAbstractItemModel& model, const QString& txid)
{
    for (int i = 0; i < model.rowCount(); ++i) {
        const auto index = model.index(i, 0);
        if (index.data(Model::TxidRole).toString() == txid) return index;
    }
    return {};
}
QString DetailEntryField(ActivityDetailModel& view,const QString& field)
{
    QString value;
    auto* rows=view.entryDocument();
    for (int row=0;row<rows->rowCount();++row) {
        const auto entry=rows->data(rows->index(row,0),Qt::UserRole).toMap();
        if (entry.value("field").toString()==field) value+=entry.value("content").toString();
    }
    return value;
}
} // namespace

class TransactionActivityModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void manualProjectionWaitsForOutcomeAndGuiDelivery();
    void manualAcceptedRawCopyRetainsOriginalDocumentAfterFacadeLoss();
    void entryDocumentRetainsCompleteLongTextAndAllRequestMetadata();
    void fullActionModelRetainsRowsBeyondBoundedActivityPreview();
    void historyReadsLeaveTheEventLoopFreeAndDiscardStaleSnapshots();
    void detailReadsDiscardPreviousSelectionsAndHandleMissingTransactions();
    void largeDetailPreviewDoesNotWaitForInputReads();
    void bumpEligibilityIsOnlyReadForTheSelectedTransaction();
    void deletingWalletDoesNotWaitForActivityReads();
    void readFailuresCanBeRetried();
    void failedInitialHistoryRetainsWorkAfterNotification_data();
    void failedInitialHistoryRetainsWorkAfterNotification();
    void opensNewTransactionBeforeItsNotification();
    void confirmationUpdatesAreBatchedAndPreserveAmountCaches();
    void copiesRawTransactionAndPublicPaymentRequest();
    void formatsDisplayAmountsForLocale();
    void exposesParentsAndActionsWithoutAllocatingFees();
    void savesBatchRecipientNotesWhenSending_data();
    void savesBatchRecipientNotesWhenSending();
    void filtersWholeTransactionsAndExportsParentImpact();
    void exportsBatchActionsWithoutDuplicatingFees();
    void malformedStoredRequestStillDrainsTheLiveParent_data();
    void malformedStoredRequestStillDrainsTheLiveParent();
    void groupsPendingMonthsAndDaysAndCountsParents();
    void groupsTransactionsInPendingUntilFirstConfirmation();
    void totalsPendingWalletImpactWithoutRequests();
    void movesReplacedBatchToHistoryAndKeepsReplacementPending();
    void associatesRequestsPerOutputAndUpdatesLive();
    void usesPrivateRequestNotesWithoutChangingStoredData();
    void restoresRequestsOnConflictAbandonmentReplacementAndDeletion();
    void sharesAddressAssociationsWithoutDuplicatingParents();
    void preservesStatusWhenBusyAndRetries();
    void contendedStatusFallbackUpdatesWithoutAdvancingTheManualClock();
    void blockingPublicStatusFallbackKeepsGuiResponsiveAndUpdatesOnRelease();
    void refreshesOnWalletTipAndSameHeightReorg();
    void queuesWalletNotificationsAndRetainsStableIndexes();
    void handlesInternalAndMinedTypes();
    void recognizesSelfSendToPaymentRequest_data();
    void recognizesSelfSendToPaymentRequest();
    void loadsFlowLazilyAndRefreshesOutputAssociations();
};

void TransactionActivityModelTests::manualProjectionWaitsForOutcomeAndGuiDelivery()
{
    ManualExecutionEnvironment env; auto original = std::make_shared<ActivityWallet>();
    const auto tx = MakeTx({{50'000, false}}, {{49'000, true}}); original->put(tx);
    ManualActivityFixture fixture{env, original}; auto* model = fixture.activity.get();
    QCoreApplication::processEvents(); QVERIFY(model->loading()); QCOMPARE(model->rowCount(), 0);
    env.drain(); QCOMPARE(model->rowCount(), 0); // Completed Domain/Core work has no GUI side effects.
    env.publishAll(); QCoreApplication::processEvents(); QCOMPARE(model->transactionCount(), 1); QVERIFY(!model->loading());
    QVERIFY(fixture.poll); const auto stale_deadline = fixture.poll;
    model->stop(); stale_deadline(); env.drain(); env.publishAll();
    QCOMPARE(model->rowCount(), 0); QVERIFY(!model->loading());
}
void TransactionActivityModelTests::manualAcceptedRawCopyRetainsOriginalDocumentAfterFacadeLoss()
{
    ManualExecutionEnvironment env; auto original = std::make_shared<ActivityWallet>();
    const auto tx = MakeTx({{50'000, false}}, {{49'000, true}}); original->put(tx);
    ManualActivityFixture fixture{env, original}; fixture.settle(); auto* model = fixture.activity.get();
    model->requestTransactionDetails(Id(tx)); fixture.settle(); QVERIFY(model->selectedTransactionDetails().value("rawAvailable").toBool());
    const auto expected = QString::fromStdString(EncodeHexTx(*tx.tx));
    QGuiApplication::clipboard()->setText(QStringLiteral("before"));
    const auto ticket = model->requestCopyRawTransaction(Id(tx)); QVERIFY(ticket);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("before"));
    fixture.activity.reset(); original->transactions.clear();
    env.drain(); QCoreApplication::processEvents(); env.publishAll();
    QCOMPARE(QGuiApplication::clipboard()->text(), expected);
}
void TransactionActivityModelTests::entryDocumentRetainsCompleteLongTextAndAllRequestMetadata()
{
    Fixture fixture; const auto tx = MakeTx({{100'000, false}}, {{99'000, true}}); fixture.state->put(tx);
    auto request = Request(1, Address(tx)); request.recipient.noteSelf = std::string(5000, 'n'); request.recipient.message = std::string(6000, 'm'); fixture.request(request);
    auto* model = fixture.model(); model->requestTransactionDetails(Id(tx)); Wait(model);
    QVERIFY(model->requestEntryDetails(QStringLiteral("outputs"), 0));
    QTRY_VERIFY(!model->entryDetailsBusy()); QVERIFY(model->entryDetailsError().isEmpty());
    QString note, message;
    auto* document = model->entryDocument();
    for (int row = 0; row < document->rowCount(); ++row) { const auto value = document->index(row, 0).data(Qt::UserRole).toMap(); const auto text = value.value("content").toString(); QVERIFY(text.size() <= 1024); const auto field = value.value("field").toString(); if (field == QLatin1String("paymentRequests[0].noteSelf")) note += text; if (field == QLatin1String("paymentRequests[0].message")) message += text; }
    QCOMPARE(note, QString(5000, QLatin1Char('n'))); QCOMPARE(message, QString(6000, QLatin1Char('m')));
    QVERIFY(model->detailOutputs()->index(0, 0).data(Qt::UserRole).toMap().value("textTruncated").toBool());
}

void TransactionActivityModelTests::fullActionModelRetainsRowsBeyondBoundedActivityPreview()
{
    Fixture fixture; std::vector<Output> outputs; outputs.reserve(100);
    for (int i = 0; i < 100; ++i) outputs.push_back({1000, false, false, static_cast<unsigned char>(i + 1)});
    const auto tx = MakeTx({{110'000, true}}, outputs); fixture.state->put(tx);
    fixture.state->labels[Address(tx, 99).toStdString()] = std::string(5000, 'z');
    auto* model = fixture.model(); const auto row = Find(*model, Id(tx));
    QCOMPARE(row.data(Model::ActionCountRole).toInt(), 100);
    QCOMPARE(row.data(Model::ActionsRole).toList().size(), 64);
    QVERIFY(row.data(Model::PresentationLimitedRole).toBool());
    model->requestTransactionDetails(Id(tx)); Wait(model);
    QCOMPARE(model->detailActions()->rowCount(), 100);
    QVERIFY(model->detailActions()->index(99, 0).data(Qt::UserRole).toMap().value("textTruncated").toBool());
    QVERIFY(model->requestEntryDetails(QStringLiteral("actions"), 99)); QTRY_VERIFY(!model->entryDetailsBusy());
    QString label; auto* document = model->entryDocument();
    for (int position = 0; position < document->rowCount(); ++position) { const auto value = document->index(position, 0).data(Qt::UserRole).toMap(); if (value.value("field") == QStringLiteral("label")) label += value.value("content").toString(); }
    QCOMPARE(label, QString(5000, QLatin1Char('z')));
}

void TransactionActivityModelTests::formatsDisplayAmountsForLocale()
{
    const QLocale previous_locale;
    const auto restore_locale = qScopeGuard([previous_locale] { QLocale::setDefault(previous_locale); });
    QLocale::setDefault(QLocale{"de_DE"});

    Fixture f;
    const auto batch = MakeTx({{120'000, true}}, {{60'000, false}, {40'000, false, false, 2}, {19'000, true, true, 3}});
    CMutableTransaction parent;
    parent.vout.emplace_back(120'000, batch.tx->vout[2].scriptPubKey);
    f.state->flow_parents[batch.tx->vin[0].prevout.hash] = MakeTransactionRef(parent);
    f.state->put(batch);
    auto* model = f.model();

    auto row = Find(*model, Id(batch));
    QCOMPARE(row.data(Model::AmountRole).toString(), QString("0,00101000 BTC"));
    QCOMPARE(model->transactionDetails(Id(batch)).value("amount").toString(), QString("0,00101000 BTC"));

    f.wallet->setDisplayUnit(3);
    Wait(model);
    row = Find(*model, Id(batch));
    QCOMPARE(row.data(Model::AmountRole).toString(), QString("101.000 sats"));
    QCOMPARE(row.data(Model::ActionsRole).toList()[0].toMap().value("amount").toString(), QString("60.000 sats"));
    QCOMPARE(model->transactionDetails(Id(batch)).value("amount").toString(), QString("101.000 sats"));

    model->requestTransactionDetails(Id(batch));
    Wait(model);
    const auto flow = model->transactionDetails(Id(batch), true).value("flow").toMap();
    QCOMPARE(flow.value("inputs").toList()[0].toMap().value("amount").toString(), QString("120.000 sats"));
    QCOMPARE(flow.value("outputs").toList()[0].toMap().value("amount").toString(), QString("60.000 sats"));
    QCOMPARE(flow.value("feeAmount").toString(), QString("1.000 sats"));
}
void TransactionActivityModelTests::historyReadsLeaveTheEventLoopFreeAndDiscardStaleSnapshots()
{
    Fixture f;
    const auto old_tx = MakeTx({{50'000, false}}, {{49'000, true}});
    const auto new_tx = MakeTx({{60'000, false}}, {{59'000, true}});
    f.state->put(old_tx);
    auto gate = std::make_shared<ReadGate>();
    f.state->history_gate = gate;
    const auto release = qScopeGuard([&] { gate->release.release(); });
    // Construction schedules work; it must not enter the wallet synchronously.
    auto* model = f.wallet->transactionActivityModel();
    QVERIFY(model->loading());
    QCOMPARE(model->rowCount(), 0);
    QTRY_VERIFY(gate->entered.available() > 0);
    bool heartbeat{false};
    QTimer::singleShot(0, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(model->loading());
    QVERIFY(f.wallet->opening()->read([state = f.state, new_tx](interfaces::Wallet&) {
        state->transactions.clear();
        state->put(new_tx); return true;
    }, [](bool) {}, {}, execution::WorkClass::Action));
    model->reload();
    bool inserted_stale{false};
    connect(model, &Model::rowsInserted, model, [&] {
        inserted_stale |= Find(*model, Id(old_tx)).isValid();
        QCOMPARE(QThread::currentThread(), qApp->thread());
    });
    gate->release.release();
    Wait(model);
    QVERIFY(!inserted_stale);
    QVERIFY(Find(*model, Id(new_tx)).isValid());
    QVERIFY(!f.state->blocking_read_on_gui);
}

void TransactionActivityModelTests::detailReadsDiscardPreviousSelectionsAndHandleMissingTransactions()
{
    Fixture f;
    const auto a = MakeTx({{50'000, true}}, {{49'000, false}});
    const auto b = MakeTx({{60'000, true}}, {{59'000, false}});
    f.state->put(a);
    f.state->put(b);
    auto* model = f.model();
    auto gate = std::make_shared<ReadGate>();
    f.state->parent_gate = gate;
    const auto release = qScopeGuard([&] { gate->release.release(); });
    model->requestTransactionDetails(Id(a));
    QTRY_VERIFY(gate->entered.available() > 0);
    model->requestTransactionDetails(Id(b));
    QTRY_VERIFY(model->transactionDetails(Id(b), true).contains("flow"));
    const auto pending = model->transactionDetails(Id(b), true);
    QVERIFY(pending.value("detailsLoading").toBool());
    QCOMPARE(pending.value("flow").toMap().value("outputs").toList().first().toMap().value("amountSat").toLongLong(), 59'000);
    QVERIFY(!pending.value("flow").toMap().value("complete").toBool());
    QVERIFY(model->rawTransaction(Id(b)).isEmpty());
    bool showed_stale{false};
    connect(model, &Model::transactionDetailsChanged, model, [&] {
        const auto details = model->transactionDetails(Id(b), true);
        if (details.value("rawAvailable").toBool())
            showed_stale |= model->rawTransaction(Id(b)) != QString::fromStdString(EncodeHexTx(*b.tx));
    });
    gate->release.release();
    Wait(model);
    QVERIFY(!showed_stale);
    QVERIFY(!model->transactionDetails(Id(b), true).value("detailsLoading").toBool());
    QCOMPARE(model->rawTransaction(Id(b)), QString::fromStdString(EncodeHexTx(*b.tx)));
    QVERIFY(!model->transactionDetails(Id(a), true).contains("flow"));
    // An unrelated update may discover that a previously resolved detail has
    // disappeared. Retrying its cached row must still resolve the full flow.
    f.state->transactions.erase(b.tx->GetHash());
    Q_EMIT f.wallet->transactionChanged(Id(a), CT_UPDATED);
    QTRY_VERIFY(!model->transactionDetails(Id(b), true).value("detailsError").toString().isEmpty());
    Wait(model);
    f.state->put(b);
    model->requestTransactionDetails(Id(b));
    QVERIFY(model->rawTransaction(Id(b)).isEmpty());
    Wait(model);
    QVERIFY(model->transactionDetails(Id(b), true).value("detailsError").toString().isEmpty());
    QCOMPARE(model->rawTransaction(Id(b)), QString::fromStdString(EncodeHexTx(*b.tx)));
    model->requestTransactionDetails(QString(64, '0'));
    Wait(model);
    QVERIFY(!model->transactionDetails(QString(64, '0'), true).value("detailsError").toString().isEmpty());
    QVERIFY(!f.state->blocking_read_on_gui);
}

void TransactionActivityModelTests::largeDetailPreviewDoesNotWaitForInputReads()
{
    Fixture f;
    std::vector<Output> outputs(1001, {1'000, false});
    outputs[500] = {2'000, true, false, 2};
    const auto tx = MakeTx({{1'003'000, false}}, outputs);
    CMutableTransaction parent;
    parent.vout.emplace_back(1'003'000, tx.tx->vout[0].scriptPubKey);
    f.state->flow_parents[tx.tx->vin[0].prevout.hash] = MakeTransactionRef(parent);
    f.state->put(tx);
    auto request = Request(91, Address(tx, 500), "Invoice", 2'000);
    request.recipient.noteSelf = "My payment";
    f.request(request);
    auto* model = f.model();
    auto gate = std::make_shared<ReadGate>();
    f.state->parent_gate = gate;
    const auto release = qScopeGuard([&] { gate->release.release(); });
    QObject view_owner;
    std::unique_ptr<ActivityDetailModel> view{model->createDetailView(&view_owner)};
    view->setActive(true);
    view->setTransactionId(Id(tx));
    QTRY_VERIFY(view->selectedTransactionDetails().contains("flow"));
    const auto preview = view->selectedTransactionDetails();
    const auto flow = preview.value("flow").toMap();
    const auto shown = flow.value("outputs").toList();
    QVERIFY(preview.value("detailsLoading").toBool());
    QCOMPARE(view->state(),ActivityDetailModel::Loading);
    QVERIFY(!preview.value("canBump").toBool());
    QVERIFY(!f.state->blocking_read_on_gui);
    QCOMPARE(flow.value("outputCount").toInt(), 1001);
    QCOMPARE(shown.size(), 2);
    QCOMPARE(shown[0].toMap().value("kind").toString(), QString("output-group"));
    QCOMPARE(shown[0].toMap().value("outputCount").toInt(), 1000);
    QCOMPARE(shown[0].toMap().value("amountSat").toLongLong(), 1'000'000);
    QCOMPARE(shown[1].toMap().value("id").toString(), QString("output:500"));
    QCOMPARE(shown[1].toMap().value("address").toString(), Address(tx, 500));
    QCOMPARE(shown[1].toMap().value("label").toString(), QString("My payment"));
    QCOMPARE(shown[1].toMap().value("paymentRequests").toList().size(), 1);
    QVERIFY(!flow.value("inputs").toList().first().toMap().value("amountKnown").toBool());
    QVERIFY(!flow.value("feeKnown").toBool());
    QTRY_VERIFY(gate->entered.available() > 0);
    // The collapsed preview's row1 is original output500. Its known, cached
    // fields remain readable while the independent Core parent lookup waits.
    QVERIFY(view->requestEntryDetails("outputs",1));
    QTRY_VERIFY(!view->entryDetailsBusy());
    QCOMPARE(DetailEntryField(*view,"id"),QStringLiteral("output:500"));
    QCOMPARE(DetailEntryField(*view,"index"),QStringLiteral("500"));
    QCOMPARE(DetailEntryField(*view,"amountSat"),QStringLiteral("2000"));
    QCOMPARE(DetailEntryField(*view,"address"),Address(tx,500));
    QVERIFY(view->pending());
    bool heartbeat{false};
    QTimer::singleShot(0, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(view->selectedTransactionDetails().value("detailsLoading").toBool());
    gate->release.release();
    QTRY_VERIFY(!view->pending());
    QVERIFY(view->error().isEmpty());
    const auto complete = view->selectedTransactionDetails();
    const auto resolved = complete.value("flow").toMap();
    QVERIFY(!complete.value("detailsLoading").toBool());
    QVERIFY(resolved.value("outputs").toList().size() <= 16);
    QCOMPARE(view->detailOutputs()->rowCount(), 1001);
    QCOMPARE(view->detailOutputs()->index(500, 0).data(Qt::UserRole).toMap().value("label").toString(), QString("My payment"));
    QCOMPARE(view->detailOutputs()->index(500, 0).data(Qt::UserRole).toMap().value("paymentRequests").toList().size(), 1);
    CAmount shown_total{0}; for (const auto& row : resolved.value("outputs").toList()) shown_total += row.toMap().value("amountSat").toLongLong();
    QCOMPARE(shown_total, 1'002'000);
    QVERIFY(resolved.value("complete").toBool());
    QCOMPARE(resolved.value("feeSat").toLongLong(), 1'000);
    QVERIFY(!f.state->blocking_read_on_gui);
}

void TransactionActivityModelTests::bumpEligibilityIsOnlyReadForTheSelectedTransaction()
{
    Fixture f;
    for (int i = 0; i < 100; ++i) f.state->put(MakeTx({{100'000, true}}, {{99'000 - i, false}}), 0);
    auto* model = f.model();
    model->refreshStatuses();
    Wait(model);
    QCOMPARE(f.state->bump_reads, 0);
    const auto txid = model->index(0).data(Model::TxidRole).toString();
    auto gate = std::make_shared<ReadGate>();
    f.state->bump_gate = gate;
    const auto release = qScopeGuard([&] { gate->release.release(); });
    model->requestTransactionDetails(txid);
    QTRY_VERIFY(gate->entered.available() > 0);
    QVERIFY(model->transactionDetails(txid, true).value("detailsLoading").toBool());
    QVERIFY(!model->transactionDetails(txid, true).value("canBump").toBool());
    gate->release.release();
    Wait(model);
    QCOMPARE(f.state->bump_reads, 1);
    QVERIFY(model->transactionDetails(txid, true).value("canBump").toBool());
    model->requestTransactionDetails("");
    model->refreshStatuses();
    Wait(model);
    QCOMPARE(f.state->bump_reads, 1);
    QVERIFY(!f.state->blocking_read_on_gui);
}

void TransactionActivityModelTests::deletingWalletDoesNotWaitForActivityReads()
{
    Fixture f;
    auto gate = std::make_shared<ReadGate>();
    f.state->history_gate = gate;
    const auto release = qScopeGuard([&] { gate->release.release(); });
    // Observe the actual native owner, rather than the facade borrower alias.
    const auto lifetime = f.native_lifetime;
    auto* model = f.wallet->transactionActivityModel();
    QTRY_VERIFY(gate->entered.available() > 0);
    QSignalSpy changes(model, &Model::countChanged);
    Q_EMIT f.wallet->walletUnloaded();
    QVERIFY(!model->loading());
    // A synchronous join here would deadlock: release happens after deletion.
    f.wallet.reset();
    QVERIFY(!lifetime.expired());
    gate->release.release();
    QTRY_VERIFY(lifetime.expired());
    QCOMPARE(changes.count(), 0);
}

void TransactionActivityModelTests::opensNewTransactionBeforeItsNotification()
{
    Fixture f;
    auto* model = f.model();
    const auto tx = MakeTx({{50'000, true}}, {{49'000, false}});
    f.state->put(tx, 0);
    model->requestTransactionDetails(Id(tx));
    QCOMPARE(model->rowCount(), 0);
    QVERIFY(model->transactionDetails(Id(tx), true).value("detailsLoading").toBool());
    Wait(model);
    QCOMPARE(model->rowCount(), 1);
    QVERIFY(model->transactionDetails(Id(tx), true).contains("flow"));
    QVERIFY(!f.state->blocking_read_on_gui);
}

void TransactionActivityModelTests::readFailuresCanBeRetried()
{
    Fixture f;
    f.state->fail_history = true;
    auto* model = f.model();
    QVERIFY(!model->loadError().isEmpty());
    f.state->fail_history = false;
    f.state->put(MakeTx({{50'000, false}}, {{49'000, true}}));
    model->reload();
    Wait(model);
    QVERIFY(model->loadError().isEmpty());
    QCOMPARE(model->rowCount(), 1);
}

void TransactionActivityModelTests::failedInitialHistoryRetainsWorkAfterNotification_data()
{
    QTest::addColumn<bool>("retry_fails");
    QTest::newRow("retry-succeeds") << false;
    QTest::newRow("retry-fails") << true;
}

void TransactionActivityModelTests::failedInitialHistoryRetainsWorkAfterNotification()
{
    QFETCH(bool, retry_fails);
    Fixture f;
    const auto old_tx = MakeTx({{50'000, false}}, {{49'000, true}});
    const auto new_tx = MakeTx({{60'000, false}}, {{59'000, true}});
    f.state->put(old_tx);
    f.state->fail_history = true;
    auto gate = std::make_shared<ReadGate>();
    f.state->history_gate = gate;
    const auto release = qScopeGuard([&] { gate->release.release(); });
    auto* model = f.wallet->transactionActivityModel();
    QTRY_VERIFY(gate->entered.available() > 0);
    QVERIFY(f.wallet->opening()->read([state = f.state, new_tx, retry_fails](interfaces::Wallet&) {
        state->put(new_tx);
        state->fail_history = retry_fails; return true;
    }, [](bool) {}, {}, execution::WorkClass::Action));
    Q_EMIT f.wallet->transactionChanged(Id(new_tx), CT_NEW);
    gate->release.release();
    Wait(model);
    if (retry_fails) {
        QVERIFY(!model->loadError().isEmpty());
        QCOMPARE(model->rowCount(), 0);
        f.state->fail_history = false;
        model->reload();
        Wait(model);
    }
    QVERIFY(model->loadError().isEmpty());
    QCOMPARE(model->rowCount(), 2);
    QVERIFY(Find(*model, Id(old_tx)).isValid());
    QVERIFY(Find(*model, Id(new_tx)).isValid());
    QVERIFY(!f.state->blocking_read_on_gui);
}

void TransactionActivityModelTests::confirmationUpdatesAreBatchedAndPreserveAmountCaches()
{
    Fixture f;
    for (int i = 0; i < 2000; ++i) f.state->put(MakeTx({{100'000, true}}, {{99'000 - i, false}}), 6);
    auto* source = f.model();
    Proxy proxy;
    proxy.setSourceModel(source);
    WaitProxy(proxy);
    QSignalSpy changes(source, &Model::dataChanged);
    QSignalSpy pending(&proxy, &Proxy::pendingBalanceChanged);
    QSignalSpy maximum(&proxy, &Proxy::availableMaxAmountChanged);
    const auto before = proxy.availableMaxAmount();
    for (auto& [id, status] : f.state->statuses) ++status.depth_in_main_chain;
    source->refreshStatuses();
    Wait(source);
    WaitProxy(proxy);
    QCOMPARE(changes.size(), 2000);
    for (const auto& change : changes) {
        QCOMPARE(change.at(0).value<QModelIndex>().row(), change.at(1).value<QModelIndex>().row());
        QCOMPARE(change.at(2).value<QList<int>>(), QList<int>{Model::DepthRole});
    }
    WaitProxy(proxy);
    QCOMPARE(pending.size(), 0);
    WaitProxy(proxy);
    QCOMPARE(maximum.size(), 0);
    WaitProxy(proxy);
    QCOMPARE(proxy.availableMaxAmount(), before);
    WaitProxy(proxy);
    QCOMPARE(proxy.pendingBalanceSat(), 0);
}

void TransactionActivityModelTests::copiesRawTransactionAndPublicPaymentRequest()
{
    Fixture f;
    const auto tx = MakeTx({{100'000, true}}, {{99'000, false}});
    f.state->put(tx);
    auto request = Request(1, Address(tx), "Public label", 10'000);
    request.recipient.message = "Public message";
    request.recipient.noteSelf = "Private note";
    f.request(request);
    auto* model = f.model();
    model->requestTransactionDetails(Id(tx)); Wait(model);
    QCOMPARE(model->rawTransaction(Id(tx)), QString::fromStdString(EncodeHexTx(*tx.tx)));
    QVERIFY(model->rawTransaction("missing").isEmpty());
    const auto uri = model->paymentRequestUri("1");
    QCOMPARE(uri, ReceiveRequestHistoryModel::BuildBitcoinUri(Address(tx), 10'000, "Public label", "Public message"));
    QVERIFY(!uri.contains("Private"));
    QVERIFY(model->paymentRequestUri("missing").isEmpty());
}

void TransactionActivityModelTests::loadsFlowLazilyAndRefreshesOutputAssociations()
{
    Fixture f;
    const auto tx = MakeTx({{100'000, true}}, {{70'000, false}, {29'000, true, true, 2}});
    CMutableTransaction parent;
    parent.vout.emplace_back(100'000, tx.tx->vout[1].scriptPubKey);
    f.state->flow_parents[tx.tx->vin[0].prevout.hash] = MakeTransactionRef(parent);
    const QString input_address = Address(tx, 1);
    f.state->labels[input_address.toStdString()] = "Savings";
    f.state->put(tx, 2);
    f.state->statuses[tx.tx->GetHash()].block_height = 100;
    auto* model = f.model();
    QVERIFY(!model->transactionDetails(Id(tx)).contains("flow"));
    QCOMPARE(f.state->parent_reads, 0);
    model->requestTransactionDetails(Id(tx));
    Wait(model);
    auto details = model->transactionDetails(Id(tx), true);
    QCOMPARE(f.state->parent_reads, 1);
    QCOMPARE(model->rawTransaction(Id(tx)), QString::fromStdString(EncodeHexTx(*tx.tx)));
    QVERIFY(details.value("rawAvailable").toBool());
    QVERIFY(!model->transactionDetails(Id(tx), true).contains("rawTransaction"));
    QVERIFY(!model->transactionDetails(Id(tx)).contains("rawTransaction"));
    QCOMPARE(details.value("blockHeight").toInt(), 100);
    QCOMPARE(details.value("feeSat").toLongLong(), 1'000);
    QVERIFY(details.value("feeRateSatPerVb").toDouble() > 0);
    QCOMPARE(details.value("size").toLongLong(), qint64(GetSerializeSize(TX_WITH_WITNESS(*tx.tx))));
    QCOMPARE(details.value("version").toLongLong(), qint64(tx.tx->version));
    QCOMPARE(details.value("lockTime").toLongLong(), qint64(tx.tx->nLockTime));
    QCOMPARE(details.value("weight"), details.value("flow").toMap().value("weight"));
    QVERIFY(details.value("weight").toLongLong() > 0);
    QCOMPARE(details.value("flow").toMap().value("outputs").toList().size(), 2);
    const auto input = details.value("flow").toMap().value("inputs").toList()[0].toMap();
    QCOMPARE(input.value("address").toString(), input_address);
    QCOMPARE(input.value("label").toString(), QString("Savings"));
    QVERIFY(input.value("paymentRequests").toList().isEmpty());
    QCOMPARE(details.value("actions").toList().size(), 1);
    model->refreshStatuses();
    Wait(model);
    model->transactionDetails(Id(tx), true);
    QCOMPARE(f.state->parent_reads, 1);

    // Address edits refresh the input title without reloading transaction data.
    QSignalSpy input_changed(model, &Model::transactionDetailsChanged);
    f.state->labels[input_address.toStdString()] = "Long-term savings";
    Q_EMIT f.wallet->addressListChanged();
    Wait(f.wallet->transactionActivityModel());
    QVERIFY(!input_changed.isEmpty());
    details = model->transactionDetails(Id(tx), true);
    QCOMPARE(details.value("flow").toMap().value("inputs").toList()[0].toMap().value("label").toString(),
        QString("Long-term savings"));
    QCOMPARE(f.state->parent_reads, 1);
    f.state->labels[input_address.toStdString()].clear();
    Q_EMIT f.wallet->addressListChanged();
    Wait(f.wallet->transactionActivityModel());
    QVERIFY(model->transactionDetails(Id(tx), true).value("flow").toMap().value("inputs").toList()[0].toMap().value("label").toString().isEmpty());

    auto request = Request(91, Address(tx, 1), "Public name");
    request.recipient.noteSelf = "Private note";
    QSignalSpy changed(model, &Model::transactionDetailsChanged);
    f.request(request);
    QVERIFY(!changed.isEmpty());
    details = model->transactionDetails(Id(tx), true);
    auto output = details.value("flow").toMap().value("outputs").toList()[1].toMap();
    QCOMPARE(output.value("label").toString(), QString("Private note"));
    QCOMPARE(output.value("paymentRequests").toList().size(), 1);
    request.recipient.noteSelf.clear();
    f.request(request);
    f.wallet->setDisplayUnit(3);
    Wait(model);
    output = model->transactionDetails(Id(tx), true).value("flow").toMap().value("outputs").toList()[1].toMap();
    QVERIFY(output.value("label").toString().isEmpty());
    QVERIFY(output.value("amount").toString().endsWith(" sats"));
    QCOMPARE(f.state->parent_reads, 1);
    QCOMPARE(f.state->address_writes, 0);
    QSignalSpy notification(f.wallet.get(), &WalletQmlModel::transactionChanged);
    f.state->notify(tx, CT_UPDATED);
    // Request/display edits already emitted detail changes. Wait for the
    // actual background notification before joining its Activity refresh.
    QTRY_COMPARE(notification.size(), 1);
    Wait(model);
    QVERIFY(changed.size() > 2);
    model->transactionDetails(Id(tx), true);
    QCOMPARE(f.state->parent_reads, 2);
    QVERIFY(model->transactionDetails("not a txid", true).isEmpty());
}

void TransactionActivityModelTests::exposesParentsAndActionsWithoutAllocatingFees()
{
    Fixture f;
    const auto batch = MakeTx({{120'000, true}}, {{60'000, false}, {40'000, false, false, 2}, {19'000, true, true, 3}});
    f.state->put(batch);
    f.state->labels[Address(batch).toStdString()] = "Robert";
    f.state->labels[Address(batch, 1).toStdString()] = "Elisabeth";
    auto* source = f.model();
    QAbstractItemModelTester tester(source, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(source->rowCount(), 1);
    QCOMPARE(source->transactionCount(), 1);
    QCOMPARE(source->requestCount(), 0);
    const auto row = source->index(0);
    QCOMPARE(row.data(Model::ActivityTypeRole).toInt(), int(Model::Multiple));
    QCOMPARE(row.data(Model::NetAmountSatRole).toLongLong(), -101'000);
    QCOMPARE(row.data(Model::FeeSatRole).toLongLong(), 1'000);
    QVERIFY(row.data(Model::FeeKnownRole).toBool());
    QCOMPARE(row.data(Model::AmountRole).toString(), QString("0.00101000 BTC"));
    QVERIFY(row.data(Model::AddressRole).toString().isEmpty());
    const auto actions = row.data(Model::ActionsRole).toList();
    QCOMPARE(actions.size(), 2);
    QCOMPARE(actions[0].toMap().value("label").toString(), QString("Robert"));
    QCOMPARE(actions[0].toMap().value("amountSat").toLongLong(), 60'000);
    QCOMPARE(actions[1].toMap().value("amountSat").toLongLong(), 40'000);
    QCOMPARE(actions[1].toMap().value("outputIndex").toInt(), 1);
    QVERIFY(actions[0].toMap().value("actionId") != actions[1].toMap().value("actionId"));

    const int reads = f.state->status_reads + f.state->label_reads + f.state->snapshots;
    for (auto role : source->roleNames().keys()) source->data(row, role);
    QCOMPARE(source->transactionDetails(Id(batch)).value("actions").toList(), actions);
    QCOMPARE(f.state->status_reads + f.state->label_reads + f.state->snapshots, reads);
    f.wallet->setDisplayUnit(3);
    Wait(source);
    QVERIFY(row.data(Model::AmountRole).toString().endsWith(" sats"));
    QVERIFY(row.data(Model::ActionsRole).toList()[0].toMap().value("amount").toString().endsWith(" sats"));
    QCOMPARE(row.data(Model::NetAmountSatRole).toLongLong(), -101'000);
}

void TransactionActivityModelTests::savesBatchRecipientNotesWhenSending_data()
{
    QTest::addColumn<bool>("externalSigner");
    QTest::newRow("local-signing") << false;
    QTest::newRow("external-signing") << true;
}

void TransactionActivityModelTests::savesBatchRecipientNotesWhenSending()
{
    QFETCH(bool, externalSigner);
    Fixture f;
    auto batch = MakeTx({{130'000, true}},
        {{60'000, false}, {40'000, false, false, 2}, {10'000, false, false, 3}, {19'000, true, true, 4}});
    CMutableTransaction previous;
    previous.vout.emplace_back(130'000, CScript{} << OP_DROP << OP_TRUE);
    f.state->previous_tx = MakeTransactionRef(previous);
    CMutableTransaction spending{*batch.tx};
    spending.vin[0].prevout = COutPoint{f.state->previous_tx->GetHash(), 0};
    batch.tx = MakeTransactionRef(spending);
    f.state->send_draft = batch;
    f.state->external_signer = externalSigner;
    const auto second_address = Address(batch, 1).toStdString();
    f.state->labels[second_address] = "Previous label";
    f.state->purposes[second_address] = wallet::AddressPurpose::RECEIVE;
    auto request = Request(1, Address(batch, 1), "Public request name");
    request.recipient.noteSelf = "Private request note";
    f.request(request);

    auto* source = f.model();
    auto* recipients = f.wallet->sendRecipientList();
    const QStringList notes{QStringLiteral("Robert · September"), QStringLiteral("Elisabeth · September"), QString{}};
    for (int i = 0; i < notes.size(); ++i) {
        if (i > 0) recipients->add();
        auto* recipient = recipients->currentRecipient();
        recipient->setAddress(Address(batch, i));
        recipient->setLabel(notes[i]);
        recipient->amount()->setSatoshi(batch.tx->vout[i].nValue);
    }
    QVERIFY2(AwaitPreparation(*f.wallet), qPrintable(f.wallet->transactionError()));
    QCOMPARE(f.state->address_writes, 0); // Reviewing/cancelling must not save notes.
    if (externalSigner) {
        QSignalSpy approved(f.wallet.get(), &WalletQmlModel::externalSignerApprovalSucceeded);
        QVERIFY(AwaitSignerApprovalOutcome(*f.wallet));
        QCOMPARE(f.wallet->signerApprovalState(), WalletQmlModel::ApprovalState::ApprovalSigned);
        QCOMPARE(approved.count(), 1);
    }
    // The committed notes belong to the reviewed transaction, not a new draft.
    recipients->clear();
    QVERIFY(AwaitCommittedSend(*f.wallet));
    QCOMPARE(f.state->commits, 1);
    // Committing and saving the original reviewed notes have separate receipts.
    const QString committed_txid = f.wallet->sendResultTxid();
    QVERIFY(!committed_txid.isEmpty());
    QTRY_COMPARE(f.wallet->recipientLabelsTransactionId(), committed_txid);
    QTRY_VERIFY(!f.wallet->recipientLabelsPending());
    QVERIFY2(f.wallet->recipientLabelsError().isEmpty(), qPrintable(f.wallet->recipientLabelsError()));
    QCOMPARE(f.state->address_writes, 2);
    QCOMPARE(f.state->labels.at(Address(batch).toStdString()), notes[0].toStdString());
    QCOMPARE(f.state->labels.at(second_address), notes[1].toStdString());
    QCOMPARE(f.state->purposes.at(Address(batch).toStdString()), wallet::AddressPurpose::SEND);
    QCOMPARE(f.state->purposes.at(second_address), wallet::AddressPurpose::RECEIVE);
    QCOMPARE(f.wallet->receiveRequests()->matchingEntriesForAddress(Address(batch, 1)).first().toMap().value("noteSelf").toString(),
             QStringLiteral("Private request note"));

    const QString txid = Id(*f.state->send_draft);
    QTRY_VERIFY(Find(*source, txid).isValid());
    auto actions = Find(*source, txid).data(Model::ActionsRole).toList();
    QCOMPARE(actions.size(), 3);
    for (int i = 0; i < notes.size(); ++i) QCOMPARE(actions[i].toMap().value("label").toString(), notes[i]);
    source->reload();
    Wait(source);
    QCOMPARE(Find(*source, txid).data(Model::ActionsRole).toList(), actions);
}

void TransactionActivityModelTests::filtersWholeTransactionsAndExportsParentImpact()
{
    Fixture f;
    const auto mixed = MakeTx({{100'000, true}, {50'000, false}}, {{120'000, false}, {29'000, true, true, 2}}, Time(8, 12, 22));
    f.state->put(mixed);
    f.state->labels[Address(mixed, 1).toStdString()] = "Savings";
    Proxy proxy;
    proxy.setSourceModel(f.model());
    QAbstractItemModelTester tester(&proxy, QAbstractItemModelTester::FailureReportingMode::QtTest);
    proxy.setSearchText("savings");
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1);
    WaitProxy(proxy);
    QCOMPARE(proxy.index(0, 0).data(Model::ActionCountRole).toInt(), 2);
    WaitProxy(proxy);
    QVERIFY(!proxy.index(0, 0).data(Model::FeeKnownRole).toBool());
    WaitProxy(proxy);
    QVERIFY(!proxy.index(0, 0).data(Model::FeeSatRole).isValid());
    const auto actions = proxy.index(0, 0).data(Model::ActionsRole).toList();
    WaitProxy(proxy);
    QCOMPARE(actions[0].toMap().value("source").toInt(), int(Model::WalletInputs));
    WaitProxy(proxy);
    QCOMPARE(actions[0].toMap().value("inputs").toList().size(), 1);
    WaitProxy(proxy);
    QCOMPARE(actions[1].toMap().value("amount").toString(), QString("+0.00029000 BTC"));
    for (auto type : {Proxy::Sent, Proxy::Received, Proxy::Multiple}) {
        proxy.setTypeFilter(type);
    WaitProxy(proxy);
        QCOMPARE(proxy.rowCount(), 1);
    WaitProxy(proxy);
        QCOMPARE(proxy.index(0, 0).data(Model::ActionsRole).toList(), actions);
    }
    proxy.setTypeFilters({Proxy::Sent, Proxy::Received, Proxy::Multiple});
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1); // Matching several selected types never duplicates a parent.
    WaitProxy(proxy);
    QCOMPARE(proxy.index(0, 0).data(Model::ActionsRole).toList(), actions);
    WaitProxy(proxy);
    QVERIFY(proxy.setAmountRange(71'000, 71'000));
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1); // Use the net parent amount, not a child output.
    WaitProxy(proxy);
    QCOMPARE(proxy.index(0, 0).data(Model::ActionsRole).toList(), actions);
    proxy.setMaxAmount(-1);
    proxy.setMinAmount(71'001);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 0);
    proxy.setMinAmount(71'000);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1);
    WaitProxy(proxy);
    QVERIFY(proxy.applyCustomRange("2026-08-12", "2026-08-12"));
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1);
    QTemporaryDir dir;
    const auto path = dir.filePath("activity.csv");
    WaitProxy(proxy);
    QVERIFY(Export(proxy, path));
    QFile file(path);
    WaitProxy(proxy);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto csv = file.readAll();
    WaitProxy(proxy);
    QCOMPARE(csv.count('\n'), 4); // Header, parent wallet impact, and both actions.
    WaitProxy(proxy);
    QVERIFY(csv.contains("Action amount (BTC)"));
    WaitProxy(proxy);
    QVERIFY(csv.contains("-0.00100000"));
    WaitProxy(proxy);
    QVERIFY(csv.contains("0.00029000"));
    WaitProxy(proxy);
    QCOMPARE(csv.count("-0.00071000"), 1);
    WaitProxy(proxy);
    QVERIFY(csv.contains(Address(mixed, 1).toUtf8()));
    WaitProxy(proxy);
    QVERIFY(csv.contains("Multiple actions"));
    WaitProxy(proxy);
    QVERIFY(csv.contains("-0.00071000"));
    proxy.setTypeFilter(Proxy::Mined);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 0);
}

void TransactionActivityModelTests::exportsBatchActionsWithoutDuplicatingFees()
{
    Fixture f;
    const auto batch = MakeTx({{100'000, true}}, {{40'000, false}, {59'000, false, false, 2}}, Time(8, 12));
    f.state->put(batch);
    f.state->labels[Address(batch, 0).toStdString()] = "=Bob";
    f.state->labels[Address(batch, 1).toStdString()] = "Alice, savings";
    Proxy proxy;
    proxy.setSourceModel(f.model());
    proxy.setDisplayUnit(3);
    proxy.setSearchText("Alice"); // Export every action of the matching transaction.
    QTemporaryDir dir;
    const auto path = dir.filePath("batch.csv");
    WaitProxy(proxy);
    QVERIFY(Export(proxy, path));
    QFile file(path);
    WaitProxy(proxy);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto csv = file.readAll();
    WaitProxy(proxy);
    QCOMPARE(csv.count('\n'), 4);
    WaitProxy(proxy);
    QVERIFY(csv.contains("Action amount (sat)"));
    WaitProxy(proxy);
    QVERIFY(csv.contains("\"'=Bob\""));
    WaitProxy(proxy);
    QVERIFY(csv.contains("\"Alice, savings\""));
    WaitProxy(proxy);
    QVERIFY(csv.contains(Address(batch, 0).toUtf8()));
    WaitProxy(proxy);
    QVERIFY(csv.contains(Address(batch, 1).toUtf8()));
    const auto lines = csv.split('\n');
    // The parent carries the 1,000 sat fee once. Child Amount cells are empty;
    // their separate Action amount cells contain only the recipient amounts.
    WaitProxy(proxy);
    QVERIFY(lines[1].contains("\"-100000\""));
    WaitProxy(proxy);
    QVERIFY(lines[2].endsWith("\"-40000\""));
    WaitProxy(proxy);
    QVERIFY(lines[3].endsWith("\"-59000\""));
    const QByteArray empty_amount_and_id = "\",\"\",\"" + Id(batch).toUtf8();
    WaitProxy(proxy);
    QVERIFY(lines[2].contains(empty_amount_and_id));
    WaitProxy(proxy);
    QVERIFY(lines[3].contains(empty_amount_and_id));
}

void TransactionActivityModelTests::malformedStoredRequestStillDrainsTheLiveParent_data()
{
    QTest::addColumn<bool>("invalid_address");
    QTest::newRow("decoded-invalid-address") << true;
    QTest::newRow("malformed-serialized-record") << false;
}
void TransactionActivityModelTests::malformedStoredRequestStillDrainsTheLiveParent()
{
    QFETCH(bool, invalid_address);
    Fixture f;
    const auto malformed = invalid_address ? walletrequests::Encode(Request(1, "unpaid-address")) : std::string{"not-a-serialized-request"};
    auto stored = std::make_shared<std::atomic_bool>(false);
    QVERIFY(f.wallet->opening()->read([malformed](interfaces::Wallet& backend) {
        auto& wallet = dynamic_cast<ActivityWallet&>(backend);
        wallet.request_blobs.clear(); wallet.request_blobs.emplace(1, malformed); return true;
    }, [stored](bool) { stored->store(true); }, {}, execution::WorkClass::Action));
    QTRY_VERIFY(stored->load());
    f.wallet->reloadReceiveRequests();
    QTRY_VERIFY(([&] {
        const auto snapshot = f.wallet->receiveRequests()->snapshot();
        return snapshot && snapshot->malformed == 1 && snapshot->records.empty() &&
            f.wallet->property("receiveRequestsLoaded").toBool() && !f.wallet->property("receiveRequestsBusy").toBool();
    })());
    QCOMPARE(f.wallet->receiveRequests()->count(), 0);
    auto* activity = f.model(); QCOMPARE(activity->requestCount(), 0); QCOMPARE(activity->rowCount(), 0);
    const auto scheduler = f.wallet->opening()->scheduler();
    QSignalSpy finished{f.wallet.get(), &WalletQmlModel::shutdownFinished};
    f.wallet->beginShutdown();
    // Keep the parent facade alive: destruction must not be needed to release
    // malformed-load resources or complete the original Native retirement.
    QTRY_COMPARE(finished.count(), 1);
    QTRY_VERIFY(f.wallet->backendExecutor()->isDrained());
    QCOMPARE(scheduler->pending(), size_t{0});
    QVERIFY(f.native_lifetime.expired());
}

void TransactionActivityModelTests::groupsPendingMonthsAndDaysAndCountsParents()
{
    Fixture f;
    const auto incoming = MakeTx({{50'000, false}}, {{49'000, true}}, Time(8, 12));
    const auto august = MakeTx({{90'000, true}}, {{40'000, false}, {49'000, false, false, 2}}, Time(8, 13));
    const auto september = MakeTx({{30'000, true}}, {{29'000, false}}, Time(9, 14));
    f.state->put(incoming, 0);
    f.state->put(august);
    f.state->put(september, 0);
    f.request(Request(1, UnpaidAddress()));
    Proxy proxy;
    proxy.setSourceModel(f.model());
    WaitProxy(proxy);
    QCOMPARE(proxy.count(), 4);
    WaitProxy(proxy);
    QCOMPARE(proxy.transactionCount(), 3);
    WaitProxy(proxy);
    QCOMPARE(proxy.requestCount(), 1);
    WaitProxy(proxy);
    QCOMPARE(proxy.index(0, 0).data(Proxy::SectionKeyRole).toString(), QString("pending"));
    WaitProxy(proxy);
    QCOMPARE(proxy.index(1, 0).data(Proxy::SectionKeyRole).toString(), QString("pending"));
    WaitProxy(proxy);
    QCOMPARE(proxy.index(2, 0).data(Proxy::SectionKeyRole).toString(), QString("pending"));
    WaitProxy(proxy);
    QCOMPARE(proxy.index(0, 0).data(Model::TxidRole).toString(), Id(september));
    WaitProxy(proxy);
    QCOMPARE(proxy.index(3, 0).data(Proxy::SectionKeyRole).toString(), QString("2026-08"));
    proxy.setGroupBy(Proxy::Day);
    WaitProxy(proxy);
    QCOMPARE(Find(proxy, Id(august)).data(Proxy::SectionKeyRole).toString(), QString("2026-08-13"));
    WaitProxy(proxy);
    QCOMPARE(Find(proxy, Id(august)).data(Proxy::DateTimeLabelRole).toString(), QLocale().toString(QDateTime::fromSecsSinceEpoch(august.time), "h:mm AP"));
    WaitProxy(proxy);
    QCOMPARE(Find(proxy, Id(incoming)).data(Proxy::SectionKeyRole).toString(), QString("pending"));
    WaitProxy(proxy);
    QVERIFY(Find(proxy, Id(incoming)).data(Proxy::DateTimeLabelRole).toString().contains("12"));
    f.state->statuses[incoming.tx->GetHash()].depth_in_main_chain = 1;
    f.model()->refreshStatuses();
    Wait(f.model());
    WaitProxy(proxy);
    QCOMPARE(Find(proxy, Id(incoming)).data(Proxy::SectionKeyRole).toString(), QString("2026-08-12"));
    WaitProxy(proxy);
    QCOMPARE(proxy.index(0, 0).data(Model::TxidRole).toString(), Id(september));
    proxy.setTypeFilter(Proxy::Sent);
    WaitProxy(proxy);
    QCOMPARE(proxy.transactionCount(), 2);
    WaitProxy(proxy);
    QCOMPARE(proxy.requestCount(), 0);
    WaitProxy(proxy);
    QCOMPARE(proxy.count(), 2); // The batch's two children count as one.
}

void TransactionActivityModelTests::groupsTransactionsInPendingUntilFirstConfirmation()
{
    Fixture f;
    const std::vector<interfaces::WalletTx> transactions{
        MakeTx({{100'000, false}}, {{99'000, true, false, 5}}, Time(9, 15)), // Receive.
        MakeTx({{100'000, true}}, {{99'000, false}}, Time(9, 15)), // Single send.
        MakeTx({{120'000, true}}, {{60'000, false}, {40'000, false, false, 2}, {19'000, true, true, 3}}, Time(9, 15)), // Batch.
        MakeTx({{50'000, true}, {50'000, true}}, {{99'000, true}}, Time(9, 15)), // Consolidation.
        MakeTx({{100'000, true}}, {{49'000, true}, {50'000, true, false, 2}}, Time(9, 15)), // Split.
        MakeTx({{100'000, true}}, {{99'000, true, false, 4}}, Time(9, 15)), // Internal transfer.
    };
    for (const auto& tx : transactions) f.state->put(tx, 0);
    auto* source = f.model();
    Proxy proxy;
    proxy.setSourceModel(source);
    WaitProxy(proxy);
    QCOMPARE(proxy.transactionCount(), int(transactions.size()));

    for (const int depth : {0, 1, 2, 5, 6, 0}) {
        for (const auto& tx : transactions) f.state->statuses[tx.tx->GetHash()].depth_in_main_chain = depth;
        source->refreshStatuses();
        Wait(source);
        for (const auto& tx : transactions) {
            const auto row = Find(proxy, Id(tx));
    WaitProxy(proxy);
            QCOMPARE(row.data(Model::IsPendingRole).toBool(), depth == 0);
    WaitProxy(proxy);
            QCOMPARE(row.data(Proxy::SectionKeyRole).toString(), depth == 0 ? QString("pending") : QString("2026-09"));
        }
    }
}

void TransactionActivityModelTests::totalsPendingWalletImpactWithoutRequests()
{
    Fixture f;
    const auto receive = MakeTx({{50'000, false}}, {{49'000, true}}, Time(9, 15));
    const auto batch = MakeTx({{120'000, true}},
        {{60'000, false}, {40'000, false, false, 2}, {19'000, true, true, 3}}, Time(9, 15));
    const auto confirmed = MakeTx({{20'000, false}}, {{19'000, true, false, 4}}, Time(9, 15));
    f.state->put(receive, 0);
    f.state->put(batch, 0);
    f.state->put(confirmed, 1);
    f.request(Request(1, UnpaidAddress(), "Invoice", 1'000'000));
    auto* source = f.model();
    Proxy proxy;
    proxy.setSourceModel(source);
    QSignalSpy changed(&proxy, &Proxy::pendingBalanceChanged);
    WaitProxy(proxy);
    QCOMPARE(proxy.pendingBalanceSat(), -52'000); // Includes the batch fee, once.
    proxy.setDisplayUnit(3);
    WaitProxy(proxy);
    QCOMPARE(proxy.pendingBalance(), QLocale().toString(qint64{-52'000}) + " sats");
    WaitProxy(proxy);
    QVERIFY(!changed.empty());

    proxy.setTypeFilter(Proxy::Received);
    WaitProxy(proxy);
    QCOMPARE(proxy.pendingBalanceSat(), 49'000);
    WaitProxy(proxy);
    QCOMPARE(proxy.pendingBalance(), QLocale().positiveSign() + QLocale().toString(qint64{49'000}) + " sats");
    proxy.setTypeFilter(Proxy::PaymentRequest);
    WaitProxy(proxy);
    QCOMPARE(proxy.pendingBalanceSat(), 0);
    proxy.setTypeFilter(Proxy::TypeAll);
    WaitProxy(proxy);
    QCOMPARE(proxy.pendingBalanceSat(), -52'000);

    changed.clear();
    f.state->statuses[batch.tx->GetHash()].depth_in_main_chain = 1;
    source->refreshStatuses();
    Wait(source);
    WaitProxy(proxy);
    QCOMPARE(proxy.pendingBalanceSat(), 49'000);
    WaitProxy(proxy);
    QVERIFY(!changed.empty());
    f.state->statuses[receive.tx->GetHash()].depth_in_main_chain = 1;
    source->refreshStatuses();
    Wait(source);
    WaitProxy(proxy);
    QCOMPARE(proxy.pendingBalanceSat(), 0);

    // A same-height reorg returns the outgoing impact to pending.
    f.state->statuses[batch.tx->GetHash()].depth_in_main_chain = 0;
    source->refreshStatuses();
    Wait(source);
    WaitProxy(proxy);
    QCOMPARE(proxy.pendingBalanceSat(), -101'000);
    proxy.setSourceModel(nullptr);
    WaitProxy(proxy);
    QCOMPARE(proxy.pendingBalanceSat(), 0);
}

void TransactionActivityModelTests::movesReplacedBatchToHistoryAndKeepsReplacementPending()
{
    Fixture f;
    auto original = MakeTx({{120'000, true}},
        {{60'000, false}, {40'000, false, false, 2}, {19'000, true, true, 3}}, Time(9, 15));
    auto replacement = MakeTx({{120'000, true}},
        {{60'000, false}, {40'000, false, false, 2}, {18'000, true, true, 3}}, Time(9, 15, 13));
    f.state->put(original, 0);
    auto* source = f.model();
    Proxy proxy;
    proxy.setSourceModel(source);
    WaitProxy(proxy);
    QCOMPARE(Find(proxy, Id(original)).data(Proxy::SectionKeyRole).toString(), QString("pending"));

    original.replaced_by_txid = Txid::FromHex(Id(replacement).toStdString());
    replacement.replaces_txid = Txid::FromHex(Id(original).toStdString());
    f.state->put(original, 0);
    f.state->notify(original, CT_UPDATED);
    f.state->put(replacement, 0);
    f.state->notify(replacement, CT_NEW);
    WaitProxy(proxy);
    QTRY_COMPARE(proxy.transactionCount(), 2);
    WaitProxy(proxy);
    QVERIFY(Find(proxy, Id(original)).data(Model::IsInactiveRole).toBool());
    WaitProxy(proxy);
    QVERIFY(!Find(proxy, Id(original)).data(Model::IsPendingRole).toBool());
    WaitProxy(proxy);
    QCOMPARE(Find(proxy, Id(original)).data(Proxy::SectionKeyRole).toString(), QString("2026-09"));
    WaitProxy(proxy);
    QCOMPARE(Find(proxy, Id(replacement)).data(Proxy::SectionKeyRole).toString(), QString("pending"));
    WaitProxy(proxy);
    QCOMPARE(proxy.index(0, 0).data(Model::TxidRole).toString(), Id(replacement));
    WaitProxy(proxy);
    QCOMPARE(Find(proxy, Id(replacement)).data(Model::ActionCountRole).toInt(), 2);
    proxy.setGroupBy(Proxy::Day);
    WaitProxy(proxy);
    QCOMPARE(Find(proxy, Id(original)).data(Proxy::SectionKeyRole).toString(), QString("2026-09-15"));
    WaitProxy(proxy);
    QCOMPARE(Find(proxy, Id(replacement)).data(Proxy::SectionKeyRole).toString(), QString("pending"));

    QTemporaryDir dir;
    const auto path = dir.filePath("replaced.csv");
    proxy.setDisplayUnit(3);
    WaitProxy(proxy);
    QVERIFY(Export(proxy, path));
    QFile file(path);
    WaitProxy(proxy);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto csv = file.readAll();
    WaitProxy(proxy);
    QVERIFY(csv.contains("\"Status\""));
    const QByteArray original_parent = "\"0\",\"" + Id(original).toUtf8() + "\",\"Transaction\",\"\",\"Replaced\"";
    const QByteArray replacement_parent = "\"-102000\",\"" + Id(replacement).toUtf8() + "\",\"Transaction\",\"\",\"Unconfirmed\"";
    WaitProxy(proxy);
    QVERIFY(csv.contains(original_parent));
    WaitProxy(proxy);
    QVERIFY(csv.contains(replacement_parent));
    WaitProxy(proxy);
    QCOMPARE(csv.count("\"Replaced\""), 3); // Parent and both historical actions.
    WaitProxy(proxy);
    QVERIFY(csv.contains("\"Replaced\",\"-60000\""));
    WaitProxy(proxy);
    QVERIFY(!csv.contains("\"-101000\""));
    file.close();

    f.state->statuses[replacement.tx->GetHash()].depth_in_main_chain = 1;
    source->refreshStatuses();
    Wait(source);
    WaitProxy(proxy);
    QCOMPARE(Find(proxy, Id(replacement)).data(Proxy::SectionKeyRole).toString(), QString("2026-09-15"));

    // An original that confirms must not be zeroed by stale replacement metadata.
    f.state->statuses[original.tx->GetHash()].depth_in_main_chain = 6;
    source->refreshStatuses();
    Wait(source);
    WaitProxy(proxy);
    QVERIFY(Export(proxy, path));
    WaitProxy(proxy);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto confirmed_csv = file.readAll();
    const QByteArray confirmed_parent = "\"-101000\",\"" + Id(original).toUtf8() + "\",\"Transaction\",\"\",\"Confirmed\"";
    WaitProxy(proxy);
    QVERIFY(confirmed_csv.contains(confirmed_parent));
    WaitProxy(proxy);
    QVERIFY(!confirmed_csv.contains("\"Replaced\""));
}

void TransactionActivityModelTests::associatesRequestsPerOutputAndUpdatesLive()
{
    Fixture f;
    const auto tx = MakeTx({{120'000, false}}, {{50'000, true}, {69'000, true, false, 2}});
    auto request = Request(1, Address(tx), "Public name");
    request.recipient.noteSelf = "Rent";
    f.request(request);
    auto* source = f.model();
    QAbstractItemModelTester tester(source, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(source->requestCount(), 1);
    f.state->put(tx, 0);
    f.state->notify(tx, CT_NEW);
    QTRY_COMPARE(source->transactionCount(), 1);
    QCOMPARE(source->requestCount(), 0);
    auto actions = Find(*source, Id(tx)).data(Model::ActionsRole).toList();
    QCOMPARE(actions[0].toMap().value("paymentRequests").toList().size(), 1);
    QVERIFY(actions[1].toMap().value("paymentRequests").toList().isEmpty());
    QCOMPARE(actions[0].toMap().value("label").toString(), QString("Rent"));
    QCOMPARE(source->transactionDetails(Id(tx)).value("paymentRequests").toList().size(), 1);
    Proxy proxy;
    proxy.setSourceModel(source);
    proxy.setTypeFilter(Proxy::PaymentRequest);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1); // Paid request is represented by its transaction.
    request.recipient.noteSelf = "September rent";
    f.request(request);
    proxy.setSearchText("September rent");
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1);
    WaitProxy(proxy);
    QCOMPARE(Find(*source, Id(tx)).data(Model::ActionsRole).toList()[0].toMap().value("label").toString(), QString("September rent"));
    WaitProxy(proxy);
    const std::unique_ptr<ReceiveRequestOperation> erased{f.wallet->removeReceiveRequest("1")};
    QVERIFY(erased); QTRY_VERIFY(!erased->pending());
    QCOMPARE(erased->error(), ReceiveRequestOperation::Error::None); QVERIFY(erased->erased());
    QTRY_VERIFY(!Find(*source, Id(tx)).data(Model::HasPaymentRequestRole).toBool());
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 0);
    WaitProxy(proxy);
    QVERIFY(!Find(*source, Id(tx)).data(Model::HasPaymentRequestRole).toBool());
    WaitProxy(proxy);
    QCOMPARE(source->rowCount(), 1);
}

void TransactionActivityModelTests::usesPrivateRequestNotesWithoutChangingStoredData()
{
    Fixture f;
    const auto tx = MakeTx({{50'000, false}}, {{49'000, true}});
    f.state->labels[Address(tx).toStdString()] = "Address book name";
    auto request = Request(1, Address(tx), "Public request name");
    f.request(request);
    auto* source = f.model();
    QVERIFY(source->index(0).data(Model::LabelRole).toString().isEmpty());

    request.recipient.noteSelf = "Private September rent";
    f.request(request);
    QCOMPARE(source->index(0).data(Model::LabelRole).toString(), QString("Private September rent"));
    Proxy proxy;
    proxy.setSourceModel(source);
    proxy.setSearchText("Private September");
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1);

    f.state->put(tx);
    source->reload();
    Wait(source);
    WaitProxy(proxy);
    QCOMPARE(source->requestCount(), 0);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1);
    WaitProxy(proxy);
    QCOMPARE(source->transactionDetails(Id(tx)).value("label").toString(), QString("Private September rent"));

    // Clearing a note must not revive the public name or address-book label.
    request.recipient.noteSelf.clear();
    f.request(request);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 0);
    const auto details = source->transactionDetails(Id(tx));
    WaitProxy(proxy);
    QVERIFY(details.value("label").toString().isEmpty());
    WaitProxy(proxy);
    QVERIFY(details.value("actions").toList()[0].toMap().value("label").toString().isEmpty());
    source->refreshStatuses();
    Wait(source);
    source->reload();
    Wait(source);
    const auto stored = f.wallet->receiveRequests()->entryById("1");
    WaitProxy(proxy);
    QVERIFY(stored.has_value());
    WaitProxy(proxy);
    QVERIFY(stored->recipient.noteSelf.empty());
    WaitProxy(proxy);
    QCOMPARE(stored->recipient.label, std::string("Public request name"));
    WaitProxy(proxy);
    QCOMPARE(f.state->labels.at(Address(tx).toStdString()), std::string("Address book name"));
    proxy.setSearchText("Public request name");
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1); // Public metadata remains searchable.

    Fixture unnamed;
    unnamed.state->put(tx);
    unnamed.state->labels[Address(tx).toStdString()] = "Public address name";
    WaitProxy(proxy);
    QVERIFY(unnamed.model()->transactionDetails(Id(tx)).value("label").toString().isEmpty());
}

void TransactionActivityModelTests::restoresRequestsOnConflictAbandonmentReplacementAndDeletion()
{
    Fixture f;
    auto tx = MakeTx({{50'000, false}}, {{49'000, true}});
    f.state->put(tx, 0);
    f.request(Request(1, Address(tx)));
    auto* source = f.model();
    QCOMPARE(source->requestCount(), 0);
    auto& status = f.state->statuses[tx.tx->GetHash()];
    status.depth_in_main_chain = -1;
    source->refreshStatuses();
    Wait(source);
    QCOMPARE(source->requestCount(), 1);
    QVERIFY(Find(*source, Id(tx)).data(Model::IsInactiveRole).toBool());
    QVERIFY(!Find(*source, Id(tx)).data(Model::IsPendingRole).toBool());
    status.depth_in_main_chain = 0;
    status.is_abandoned = true;
    source->refreshStatuses();
    Wait(source);
    QCOMPARE(source->requestCount(), 1);
    QCOMPARE(Find(*source, Id(tx)).data(Model::StatusRole).toInt(), int(Transaction::Abandoned));
    status.is_abandoned = false;
    source->refreshStatuses();
    Wait(source);
    QCOMPARE(source->requestCount(), 0);

    auto replacement = MakeTx({{50'000, false}}, {{48'000, true}});
    tx.replaced_by_txid = Txid::FromHex(Id(replacement).toStdString());
    f.state->put(tx, 0);
    f.state->notify(tx, CT_UPDATED);
    QTRY_COMPARE(source->requestCount(), 1);
    QCOMPARE(Find(*source, Id(tx)).data(Model::ReplacedByTxidRole).toString(), Id(replacement));
    replacement.replaces_txid = Txid::FromHex(Id(tx).toStdString());
    f.state->put(replacement, 0);
    f.state->notify(replacement, CT_NEW);
    QTRY_COMPARE(source->transactionCount(), 2);
    QCOMPARE(source->requestCount(), 0);
    QCOMPARE(Find(*source, Id(replacement)).data(Model::ReplacesTxidRole).toString(), Id(tx));
    f.state->transactions.erase(replacement.tx->GetHash());
    f.state->statuses.erase(replacement.tx->GetHash());
    f.state->notify(replacement, CT_DELETED);
    QTRY_COMPARE(source->transactionCount(), 1);
    QCOMPARE(source->requestCount(), 1);
    source->reload();
    Wait(source);
    QCOMPARE(source->requestCount(), 1);
    // The original can win a race (or confirm after a reorg). Historical RBF
    // metadata must not override its current confirmed state.
    f.state->statuses[tx.tx->GetHash()].depth_in_main_chain = 1;
    source->refreshStatuses();
    Wait(source);
    QCOMPARE(source->requestCount(), 0);
    QVERIFY(!Find(*source, Id(tx)).data(Model::IsInactiveRole).toBool());
}

void TransactionActivityModelTests::sharesAddressAssociationsWithoutDuplicatingParents()
{
    Fixture f;
    const auto tx = MakeTx({{100'000, false}}, {{50'000, true}, {49'000, true}});
    f.state->put(tx);
    f.request(Request(1, Address(tx), "First"));
    f.request(Request(2, Address(tx), "Second", 0));
    auto* source = f.model();
    QCOMPARE(source->rowCount(), 1);
    QCOMPARE(source->transactionDetails(Id(tx)).value("paymentRequests").toList().size(), 2);
    for (const auto& action : source->index(0).data(Model::ActionsRole).toList()) {
        QCOMPARE(action.toMap().value("paymentRequests").toList().size(), 2);
    }
    // Match policy is address-based, including partial/unspecified amounts and
    // several requests for a reused address; it makes no one-to-one claim.
    f.requests({Request(3, UnpaidAddress(252), "New request", 100)}, true);
    QCOMPARE(source->requestCount(), 1);
    QVERIFY(!Find(*source, Id(tx)).data(Model::HasPaymentRequestRole).toBool());
    Proxy proxy;
    proxy.setSourceModel(source);
    proxy.setTypeFilter(Proxy::PaymentRequest);
    proxy.setMinAmount(101);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 0);
    proxy.setMinAmount(100);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1);
}

void TransactionActivityModelTests::contendedStatusFallbackUpdatesWithoutAdvancingTheManualClock()
{
    ManualExecutionEnvironment env;auto native=std::make_shared<StatusFallbackWallet>();
    const auto tx=MakeTx({{50'000,false}},{{49'000,true}});native->put(tx,2);
    ManualActivityFixture f{env,native};f.settle();
    const QPersistentModelIndex row{Find(*f.activity,Id(tx))};QVERIFY(row.isValid());QCOMPARE(row.data(Model::DepthRole).toInt(),2);
    native->busy=true;f.activity->refreshStatuses();QCoreApplication::processEvents();
    env.publishAll();env.drain();QCOMPARE(native->blocking_status_reads.load(),1);
    QCOMPARE(row.data(Model::DepthRole).toInt(),2); // Actual result remains held off GUI.
    env.publishAll();f.settle();
    QCOMPARE(row.data(Model::DepthRole).toInt(),6);QCOMPARE(row.data(Model::StatusRole).toInt(),int(Transaction::Confirmed));
    QCOMPARE(native->blocking_status_reads.load(),1); // No poll callback or clock advancement.
}

void TransactionActivityModelTests::blockingPublicStatusFallbackKeepsGuiResponsiveAndUpdatesOnRelease()
{
    auto native=std::make_shared<StatusFallbackWallet>();
    const auto tx=MakeTx({{50'000,false}},{{49'000,true}});native->put(tx,2);
    Fixture f{native};
    Model::Services services;services.post=presentation::QueuedPost();
    int poll_arms{0};services.schedule_poll=[&](auto,auto){++poll_arms;};services.cancel_poll=[]{};
    auto source=std::make_unique<Model>(f.wallet.get(),std::move(services));Wait(source.get());
    const QPersistentModelIndex row{Find(*source,Id(tx))};QVERIFY(row.isValid());QCOMPARE(row.data(Model::DepthRole).toInt(),2);
    auto gate=std::make_shared<ReadGate>();native->status_gate=gate;
    const auto release=qScopeGuard([&]{gate->release.release();});
    const auto armed=poll_arms;native->busy=true;source->refreshStatuses();
    QTRY_VERIFY(gate->entered.available()>0);
    bool heartbeat{false};QTimer::singleShot(0,source.get(),[&]{heartbeat=true;});QTRY_VERIFY(heartbeat);
    QCOMPARE(row.data(Model::DepthRole).toInt(),2);QVERIFY(source->loading());
    gate->release.release();Wait(source.get());
    QCOMPARE(row.data(Model::DepthRole).toInt(),6);QCOMPARE(row.data(Model::StatusRole).toInt(),int(Transaction::Confirmed));
    QCOMPARE(native->blocking_status_reads.load(),1);QCOMPARE(poll_arms,armed);
    QVERIFY(!native->blocking_read_on_gui.load());source.reset();
}

void TransactionActivityModelTests::preservesStatusWhenBusyAndRetries()
{
    Fixture f;
    const auto tx = MakeTx({{50'000, false}}, {{49'000, true}});
    f.state->put(tx, 2);
    f.request(Request(1, Address(tx)));
    auto* source = f.model();
    const QPersistentModelIndex row{Find(*source, Id(tx))};
    f.state->busy = true;
    source->refreshStatuses();
    Wait(source);
    source->reload();
    Wait(source);
    QCOMPARE(row.data(Model::DepthRole).toInt(), 2);
    QCOMPARE(row.data(Model::StatusRole).toInt(), int(Transaction::Confirming));
    QCOMPARE(source->requestCount(), 0);
    f.state->statuses[tx.tx->GetHash()].depth_in_main_chain = 6;
    f.state->busy = false;
    QTRY_COMPARE(row.data(Model::DepthRole).toInt(), 6);
    QVERIFY(!row.data(Model::IsPendingRole).toBool());

    Fixture unknown;
    unknown.state->put(tx);
    unknown.state->busy = true;
    auto* uninitialized = unknown.model();
    QVERIFY(!uninitialized->index(0).data(Model::StatusKnownRole).toBool());
    QVERIFY(!uninitialized->index(0).data(Model::IsInactiveRole).toBool());
    unknown.state->busy = false;
    QTRY_VERIFY(uninitialized->index(0).data(Model::StatusKnownRole).toBool());
}

void TransactionActivityModelTests::refreshesOnWalletTipAndSameHeightReorg()
{
    Fixture f;
    const auto tx = MakeTx({{50'000, false}}, {{49'000, true}});
    f.state->put(tx, 5);
    auto* source = f.model();
    f.state->statuses[tx.tx->GetHash()].depth_in_main_chain = 6;
    QTRY_COMPARE(source->index(0).data(Model::DepthRole).toInt(), 6);
    f.state->statuses[tx.tx->GetHash()].depth_in_main_chain = 0;
    f.state->tip = 2; // Same height, different wallet-processed block.
    QTRY_COMPARE(source->index(0).data(Model::DepthRole).toInt(), 0);
    QVERIFY(source->index(0).data(Model::IsPendingRole).toBool());
}

void TransactionActivityModelTests::queuesWalletNotificationsAndRetainsStableIndexes()
{
    Fixture f;
    auto* source = f.model();
    QAbstractItemModelTester tester(source, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const auto tx = MakeTx({{50'000, false}}, {{49'000, true}});
    f.state->put(tx, 2);
    std::thread notify([&] { f.state->notify(tx, CT_NEW); });
    notify.join();
    QCOMPARE(source->rowCount(), 0);
    QTRY_COMPARE(source->rowCount(), 1);
    QVERIFY(f.state->snapshot_on_worker);
    const QPersistentModelIndex row{source->index(0)};
    const auto id = row.data(Model::IdRole);
    QSignalSpy resets(source, &QAbstractItemModel::modelReset);
    QSignalSpy changes(source, &QAbstractItemModel::dataChanged);
    source->reload();
    Wait(source);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(changes.count(), 0);
    f.state->labels[Address(tx).toStdString()] = "Updated label";
    Q_EMIT f.wallet->addressListChanged();
    Wait(f.wallet->transactionActivityModel());
    QVERIFY(row.data(Model::LabelRole).toString().isEmpty());
    QVERIFY(row.data(Model::SearchTextRole).toString().contains("Updated label"));
    QVERIFY(changes.count() > 0);
    QCOMPARE(row.data(Model::IdRole), id);
    f.request(Request(1, UnpaidAddress()));
    QVERIFY(row.isValid());
    QCOMPARE(row.data(Model::IdRole), id);
    source->reload();
    Wait(source);
    QCOMPARE(source->rowCount(), 2);
    f.state->transactions.clear();
    f.state->statuses.clear();
    source->reload();
    Wait(source);
    QVERIFY(!row.isValid());
    QCOMPARE(source->requestCount(), 1);
}

void TransactionActivityModelTests::recognizesSelfSendToPaymentRequest_data()
{
    QTest::addColumn<bool>("multiple_inputs");
    QTest::newRow("single-input") << false;
    QTest::newRow("multiple-inputs") << true;
}

void TransactionActivityModelTests::recognizesSelfSendToPaymentRequest()
{
    QFETCH(bool, multiple_inputs);
    Fixture f;
    const std::vector<std::pair<CAmount, bool>> inputs = multiple_inputs
        ? std::vector<std::pair<CAmount, bool>>{{60'000, true}, {40'000, true}}
        : std::vector<std::pair<CAmount, bool>>{{100'000, true}};
    const auto tx = MakeTx(inputs, {{39'000, true, true}, {60'000, true, false, 2}});
    auto request = Request(1, Address(tx, 1), "Public label", 60'000);
    request.recipient.noteSelf = "Personal savings";
    f.request(request);
    f.state->put(tx);
    auto* model = f.model();
    const auto row = Find(*model, Id(tx));
    QVERIFY(row.isValid());
    QCOMPARE(row.data(Model::ActivityTypeRole).toInt(), int(Model::InternalTransfer));
    QCOMPARE(row.data(Model::TypeRole).toInt(), int(Transaction::SendToSelf));
    QCOMPARE(row.data(Model::NetAmountSatRole).toLongLong(), -1'000);
    QCOMPARE(row.data(Model::LabelRole).toString(), QString("Personal savings"));
    QCOMPARE(row.data(Model::AddressRole).toString(), Address(tx, 1));
    QCOMPARE(model->requestCount(), 0);
    QVERIFY(row.data(Model::HasPaymentRequestRole).toBool());
    const auto actions = row.data(Model::ActionsRole).toList();
    QCOMPARE(actions.size(), 1);
    QCOMPARE(actions[0].toMap().value("outputIndex").toInt(), 1);
    QCOMPARE(actions[0].toMap().value("amountSat").toLongLong(), 60'000);
    QCOMPARE(actions[0].toMap().value("direction").toInt(), int(Model::InternalAction));
    QVERIFY(actions[0].toMap().value("hasPaymentRequest").toBool());
    Proxy proxy;
    proxy.setSourceModel(model);
    proxy.setTypeFilter(Proxy::Split);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 0);
    proxy.setTypeFilter(Proxy::Consolidation);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 0);
    proxy.setTypeFilter(Proxy::SentToSelf);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1);
}

void TransactionActivityModelTests::handlesInternalAndMinedTypes()
{
    Fixture f;
    const auto consolidation = MakeTx({{50'000, true}, {50'000, true}}, {{99'000, true}});
    const auto split = MakeTx({{100'000, true}}, {{50'000, true}, {49'000, true, false, 2}});
    const auto internal = MakeTx({{60'000, true}}, {{59'000, true}});
    auto mined = MakeTx({{0, false}}, {{312'500'000, true}});
    mined.is_coinbase = true;
    for (const auto& tx : {consolidation, split, internal, mined}) f.state->put(tx);
    f.state->statuses[mined.tx->GetHash()].blocks_to_maturity = 94;
    Proxy proxy;
    proxy.setSourceModel(f.model());
    proxy.setTypeFilter(Proxy::SentToSelf);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 3);
    WaitProxy(proxy);
    QVERIFY(Find(proxy, Id(consolidation)).isValid());
    WaitProxy(proxy);
    QVERIFY(Find(proxy, Id(split)).isValid());
    WaitProxy(proxy);
    QVERIFY(Find(proxy, Id(internal)).isValid());
    WaitProxy(proxy);
    QVERIFY(!Find(proxy, Id(mined)).isValid());
    proxy.setTypeFilter(Proxy::Consolidation);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1);
    WaitProxy(proxy);
    QCOMPARE(proxy.index(0, 0).data(Model::NetAmountSatRole).toLongLong(), -1'000);
    WaitProxy(proxy);
    QCOMPARE(proxy.index(0, 0).data(Model::ActionsRole).toList()[0].toMap().value("amountSat").toLongLong(), 99'000);
    WaitProxy(proxy);
    QVERIFY(proxy.index(0, 0).data(Model::LabelRole).toString().isEmpty());
    proxy.setTypeFilter(Proxy::Split);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1);
    WaitProxy(proxy);
    QCOMPARE(proxy.index(0, 0).data(Model::ActionCountRole).toInt(), 2);
    WaitProxy(proxy);
    QVERIFY(proxy.index(0, 0).data(Model::LabelRole).toString().isEmpty());
    proxy.setTypeFilter(Proxy::Mined);
    WaitProxy(proxy);
    QCOMPARE(proxy.rowCount(), 1);
    WaitProxy(proxy);
    QCOMPARE(proxy.index(0, 0).data(Model::StatusRole).toInt(), int(Transaction::Immature));
    WaitProxy(proxy);
    QCOMPARE(proxy.index(0, 0).data(Model::BlocksToMaturityRole).toInt(), 94);
    f.state->statuses[mined.tx->GetHash()].is_in_main_chain = false;
    f.model()->refreshStatuses();
    Wait(f.model());
    WaitProxy(proxy);
    QCOMPARE(proxy.index(0, 0).data(Model::StatusRole).toInt(), int(Transaction::NotAccepted));
    WaitProxy(proxy);
    QVERIFY(proxy.index(0, 0).data(Model::IsInactiveRole).toBool());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(TransactionActivityModelTests)
#else
QTEST_MAIN(TransactionActivityModelTests)
#endif
#include <test_transactionactivitymodel.moc>
