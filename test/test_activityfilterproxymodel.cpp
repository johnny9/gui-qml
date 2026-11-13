// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/models/activityfilterproxymodel.h>
#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <QPersistentModelIndex>
#include <atomic>
#include <thread>
#include <QSemaphore>
#include <qml/models/transactionactivitymodel.h>
#include <qml/models/transaction.h>

#include <QAbstractListModel>
#include <QDateTime>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QUrl>

#include <ctime>

namespace activityfiltertest {
struct ActivityRow {
    QString address;
    QString amount;
    QString date;
    int depth{0};
    QString label;
    int status{Transaction::Confirmed};
    int type{Transaction::Other};
    qint64 timestamp{0};
    QString txid;
    bool pending_request{false};
    bool has_payment_request{false};
    qlonglong net_amount_sat{0};
};

class TestActivityModel : public QAbstractListModel, public activityview::Source
{
    Q_OBJECT
    Q_INTERFACES(activityview::Source)
public:
    TestActivityModel() : m_backend(execution::Limits{}) {}
    ~TestActivityModel() override {
        m_snapshot.reset(); m_backend.shutdown();
        // The actual worker registry owns Native drain after facade destruction.
        // Waiting here would precede QObject::destroyed and deadlock a live
        // proxy's source snapshot. Each test verifies the final common drain.
    }
    std::shared_ptr<const activityview::Hold> preparedSnapshot() const override { return m_snapshot; }
    execution::SnapshotServices activityServices() const override { return {m_backend.scheduler(), m_post}; }
    std::shared_ptr<walletopening::Session> opening() const override { return {}; }
    bool activityLoading() const override { return false; }
    QString activityError() const override { return {}; }
    int activityRowForId(const QString& id) const override {
        if (!m_snapshot) return -1;
        const auto found = m_snapshot->snapshot->source_positions.find(id);
        return found == m_snapshot->snapshot->source_positions.end() ? -1 : found->second;
    }
    QList<QMetaObject::Connection> observeActivity(QObject* receiver, std::function<void()> callback) override {
        return {connect(this, &TestActivityModel::preparedChanged, receiver, std::move(callback))};
    }
Q_SIGNALS:
    void preparedChanged();
public:
    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};

        const ActivityRow& row = m_rows.at(index.row());
        switch (role) {
        case TransactionActivityModel::AddressRole:
            return row.address;
        case TransactionActivityModel::AmountRole:
            return row.amount;
        case TransactionActivityModel::DateTimeRole:
            return row.date;
        case TransactionActivityModel::DepthRole:
            return row.depth;
        case TransactionActivityModel::LabelRole:
            return row.label;
        case TransactionActivityModel::StatusRole:
            return row.status;
        case TransactionActivityModel::TypeRole:
            return row.type;
        case TransactionActivityModel::TimestampRole:
            return row.timestamp;
        case TransactionActivityModel::TxidRole:
            return row.pending_request ? QString{} : row.txid;
        case TransactionActivityModel::IsPendingRequestRole:
            return row.pending_request;
        case TransactionActivityModel::HasPaymentRequestRole:
            return row.pending_request || row.has_payment_request;
        case TransactionActivityModel::IdRole:
            return row.txid.isEmpty() ? row.label : row.txid;
        case TransactionActivityModel::StatusKnownRole:
            return true;
        case TransactionActivityModel::IsPendingRole:
            return row.pending_request || row.status == Transaction::Unconfirmed;
        case TransactionActivityModel::ActivityTypeRole:
            switch (row.type) {
            case Transaction::RecvWithAddress: case Transaction::RecvFromOther: return int(TransactionActivityModel::Receive);
            case Transaction::SendToAddress: case Transaction::SendToOther: return int(TransactionActivityModel::Send);
            case Transaction::SendToSelf: return int(TransactionActivityModel::InternalTransfer);
            case Transaction::Generated: return int(TransactionActivityModel::Mined);
            default: return int(TransactionActivityModel::Other);
            }
        case TransactionActivityModel::ActionsRole: {
            const int direction = row.type == Transaction::RecvWithAddress || row.type == Transaction::RecvFromOther
                ? TransactionActivityModel::ReceiveAction : row.type == Transaction::SendToAddress || row.type == Transaction::SendToOther
                ? TransactionActivityModel::SendAction : TransactionActivityModel::InternalAction;
            return QVariantList{QVariantMap{{"direction", direction}}};
        }
        case TransactionActivityModel::NetAmountSatRole:
            return row.net_amount_sat;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {TransactionActivityModel::AddressRole, "address"},
            {TransactionActivityModel::AmountRole, "amount"},
            {TransactionActivityModel::DateTimeRole, "date"},
            {TransactionActivityModel::DepthRole, "depth"},
            {TransactionActivityModel::LabelRole, "label"},
            {TransactionActivityModel::StatusRole, "status"},
            {TransactionActivityModel::TypeRole, "type"},
            {TransactionActivityModel::TimestampRole, "timestamp"},
            {TransactionActivityModel::TxidRole, "txid"},
            {TransactionActivityModel::IsPendingRequestRole, "isPendingRequest"},
            {TransactionActivityModel::HasPaymentRequestRole, "hasPaymentRequest"},
            {TransactionActivityModel::NetAmountSatRole, "netAmountSat"},
        };
    }

    void setRows(QList<ActivityRow> rows)
    {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
        prepare();
    }

    void setAmount(int row, qint64 amount)
    {
        m_rows[row].net_amount_sat = amount;
        Q_EMIT dataChanged(index(row, 0), index(row, 0), {TransactionActivityModel::NetAmountSatRole});
        prepare();
    }

private:
    void prepare() {
        // Fixture setup supplies the same immutable boundary as production;
        // filtering, reductions and CSV still run through the real actor.
        auto anchor = std::make_shared<std::shared_ptr<const activityview::Snapshot>>();
        auto admission = activityview::HoldValues(m_backend.scheduler(), [anchor] { anchor->reset(); });
        Q_ASSERT(admission);
        auto value = std::make_shared<activityview::Snapshot>();
        for (int position = 0; position < m_rows.size(); ++position) {
            activityview::Row row;
            for (int role = TransactionActivityModel::AddressRole; role <= TransactionActivityModel::BlocksToMaturityRole; ++role) row[role] = data(index(position, 0), role);
            const auto id = row.value(TransactionActivityModel::IdRole).toString();
            value->source_positions.emplace(id, position); value->rows.emplace(id, std::make_shared<const activityview::Row>(std::move(row)));
        }
        *anchor = value;
        auto hold = std::make_shared<activityview::Hold>(); hold->snapshot = std::move(value);
        hold->lease = std::make_shared<execution::ResourceLease>(std::move(*admission.lease)); m_snapshot = std::move(hold);
        Q_EMIT preparedChanged();
    }
    BackendExecutor m_backend;
    const presentation::Post m_post{presentation::QueuedPost()};
    std::shared_ptr<const activityview::Hold> m_snapshot;
    QList<ActivityRow> m_rows;
};

qint64 TimestampForLocalDate(const QDate& date)
{
    return QDateTime(date, QTime(12, 0)).toSecsSinceEpoch();
}

void TzSet()
{
#ifdef Q_OS_WIN
    _tzset();
#else
    tzset();
#endif
}

ActivityRow MakeRow(QString label, int type, qint64 timestamp, QString txid = {}, QString address = {})
{
    return ActivityRow{
        .address = address.isEmpty() ? QStringLiteral("bc1q%1").arg(label.toLower()) : address,
        .amount = QStringLiteral("BTC"),
        .date = QStringLiteral("date"),
        .label = std::move(label),
        .status = Transaction::Confirmed,
        .type = type,
        .timestamp = timestamp,
        .txid = txid,
        .net_amount_sat = 100'000,
    };
}

bool WaitForProxy(const ActivityFilterProxyModel& proxy) { return QTest::qWaitFor([&proxy] { return !proxy.busy(); }, 5000) && proxy.error().isEmpty(); }
bool Export(ActivityFilterProxyModel& proxy, const QString& path) {
    if (!WaitForProxy(proxy)) return false;
    const auto ticket = proxy.requestExportCsv(path); if (!ticket) return false;
    return QTest::qWaitFor([&proxy] { return !proxy.exportBusy(); }, 5000) && proxy.exportError().isEmpty();
}
bool ContainsLabel(const ActivityFilterProxyModel& proxy, const QString& label)
{
    for (int row = 0; row < proxy.rowCount(); ++row) {
        if (proxy.index(row, 0).data(TransactionActivityModel::LabelRole).toString() == label) {
            return true;
        }
    }
    return false;
}
} // namespace activityfiltertest

using activityfiltertest::ActivityRow;
using activityfiltertest::ContainsLabel;
using activityfiltertest::Export;
using activityfiltertest::MakeRow;
using activityfiltertest::TestActivityModel;
using activityfiltertest::TimestampForLocalDate;
using activityfiltertest::TzSet;
using activityfiltertest::WaitForProxy;

class ActivityFilterProxyModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cleanup()
    {
        bool drained{false};
        BackendExecutor::shutdownAll(this, [&] { drained = true; });
        QTRY_VERIFY_WITH_TIMEOUT(drained, 5000);
    }
    void sourceDestructionRetiresCachedSnapshot();
    void reentrantSourceSwitchPreservesModelTransactions();
    void manualPublicationAndStaleFilterDecisions();
    void acceptedExportRetainsOriginalSnapshotAfterObserverLoss();
    void nativeExportLeavesGuiResponsiveAndReportsFailure();
    void persistentTreePreservesRanksAcrossRotationsAndRemoval();
    void searchMatchesLabelAddressAndTxid();
    void filtersByDateBuckets();
    void filtersByCustomDateRange();
    void customRangeHandlesDstGapDayBoundaries();
    void rejectsInvalidCustomRange();
    void appliesCustomRangeWithOneNotification();
    void filtersByTypeBucketsAndKeepsPendingRequestsExclusive();
    void paymentRequestFilterIncludesAssociatedTransactions();
    void filtersByMinimumAmount();
    void combinesTypeSelections();
    void filtersByInclusiveAmountRange();
    void refiltersWhenSourceAmountChanges();
    void availableMaximumUsesUnfilteredTransactionsAndUpdates();
    void sortsByTimestampDescending();
    void exportsCurrentFilteredRowsToCsv();
    void exportsCsvUsingDisplayUnit();
    void exportsCsvEscapesSignedRowsAndHandlesFailures();
    void exportsCsvNeutralizesFormulaInjection();
};

void ActivityFilterProxyModelTests::sourceDestructionRetiresCachedSnapshot()
{
    auto source = std::make_unique<TestActivityModel>();
    source->setRows({MakeRow("Original", Transaction::RecvWithAddress, 10, "original")});
    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(source.get()); QVERIFY(WaitForProxy(proxy)); QCOMPARE(proxy.rowCount(), 1);
    const QPersistentModelIndex original{proxy.index(0)};
    source.reset();
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!proxy.sourceModel()); QCOMPARE(proxy.rowCount(), 0);
    QVERIFY(!original.isValid()); QCOMPARE(proxy.availableMaxAmount(), 0);
    QCOMPARE(proxy.captureRowContext(0), quint64{0});
}

void ActivityFilterProxyModelTests::reentrantSourceSwitchPreservesModelTransactions()
{
    TestActivityModel original, successor;
    original.setRows({MakeRow("Original", Transaction::RecvWithAddress, 10, "original")});
    successor.setRows({MakeRow("Successor", Transaction::SendToAddress, 20, "successor")});
    ActivityFilterProxyModel proxy;
    bool inside_insert{false}, nested_reset{false}, switched{false};
    connect(&proxy, &QAbstractItemModel::rowsAboutToBeInserted, this, [&] {
        inside_insert = true;
        if (!std::exchange(switched, true)) proxy.setSourceModel(&successor);
    });
    connect(&proxy, &QAbstractItemModel::rowsInserted, this, [&] { inside_insert = false; });
    connect(&proxy, &QAbstractItemModel::modelAboutToBeReset, this, [&] { nested_reset |= inside_insert; });
    proxy.setSourceModel(&original);
    QTRY_COMPARE(proxy.sourceModel(), static_cast<QAbstractItemModel*>(&successor));
    QVERIFY(WaitForProxy(proxy)); QVERIFY(switched); QVERIFY(!nested_reset);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0).data(TransactionActivityModel::LabelRole).toString(), QStringLiteral("Successor"));
}

void ActivityFilterProxyModelTests::manualPublicationAndStaleFilterDecisions()
{
    TestActivityModel source; source.setRows({MakeRow("Alpha", Transaction::RecvWithAddress, 10, "a"), MakeRow("Beta", Transaction::SendToAddress, 20, "b")});
    ManualExecutionEnvironment env;
    ActivityFilterProxyModel::Services services; services.execution = {env.scheduler, [&env](auto callback) { env.post(std::move(callback)); }};
    ActivityFilterProxyModel proxy{services}; proxy.setSourceModel(&source);
    QCoreApplication::processEvents(); QVERIFY(proxy.busy()); QCOMPARE(proxy.rowCount(), 0);
    proxy.setSearchText("Alpha");
    env.drain(); QCOMPARE(proxy.rowCount(), 0); // Native decisions alone cannot touch the view.
    proxy.setSearchText("Beta"); // Reject Alpha's undelivered result before any GUI publication.
    env.publishAll(); QCoreApplication::processEvents(); env.drain();
    QCOMPARE(proxy.rowCount(), 0);
    env.publishAll(); QCoreApplication::processEvents();
    QVERIFY(!proxy.busy()); QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0).data(TransactionActivityModel::LabelRole).toString(), QStringLiteral("Beta"));
    const QPersistentModelIndex retained{proxy.index(0)};
    source.setAmount(1, -321); QCoreApplication::processEvents(); env.drain(); env.publishAll(); QCoreApplication::processEvents();
    QVERIFY(retained.isValid()); QCOMPARE(retained.data(TransactionActivityModel::NetAmountSatRole).toLongLong(), -321);
    proxy.closeAdmission(); env.drain(); env.publishAll();
}
void ActivityFilterProxyModelTests::acceptedExportRetainsOriginalSnapshotAfterObserverLoss()
{
    TestActivityModel source; source.setRows({MakeRow("Original", Transaction::RecvWithAddress, 10, "original")});
    ManualExecutionEnvironment env;
    int writes{0}; QString exported_label;
    ActivityFilterProxyModel::Services services; services.execution = {env.scheduler, [&env](auto callback) { env.post(std::move(callback)); }};
    services.write = [&](const auto& request) { ++writes; exported_label = request.snapshot->snapshot->rows.begin()->second->value(TransactionActivityModel::LabelRole).toString(); return ActivityFilterProxyModel::CsvResult{true, request.path, {}}; };
    auto proxy = std::make_unique<ActivityFilterProxyModel>(services); proxy->setSourceModel(&source);
    QCoreApplication::processEvents(); env.drain(); env.publishAll(); QCoreApplication::processEvents(); QVERIFY(!proxy->busy());
    const auto ticket = proxy->requestExportCsv("retained.csv"); QVERIFY(ticket); QCOMPARE(writes, 0); QVERIFY(proxy->exportBusy());
    source.setRows({MakeRow("Replacement", Transaction::SendToAddress, 20, "replacement")});
    proxy.reset(); // Native owns the accepted file effect and original immutable rows.
    QCOMPARE(writes, 0); env.drain(); env.publishAll();
    QCOMPARE(writes, 1); QCOMPARE(exported_label, QStringLiteral("Original"));
}
void ActivityFilterProxyModelTests::nativeExportLeavesGuiResponsiveAndReportsFailure()
{
    TestActivityModel source; source.setRows({MakeRow("Original", Transaction::RecvWithAddress, 10, "original")});
    struct Writer { QSemaphore entered, release; std::atomic_bool on_gui{false}; };
    auto writer = std::make_shared<Writer>();
    ActivityFilterProxyModel::Services services;
    services.write = [writer](const auto& request) { writer->on_gui = QThread::currentThread() == qApp->thread(); writer->entered.release(); writer->release.acquire(); return ActivityFilterProxyModel::CsvResult{false, request.path, QStringLiteral("injected commit failure")}; };
    ActivityFilterProxyModel proxy{services}; proxy.setSourceModel(&source); QVERIFY(WaitForProxy(proxy));
    const auto unblock = qScopeGuard([writer] { writer->release.release(); });
    QSignalSpy completed(&proxy, &ActivityFilterProxyModel::exportFinished);
    const auto ticket = proxy.requestExportCsv("blocked.csv"); QVERIFY(ticket); QTRY_VERIFY(writer->entered.available() > 0);
    bool heartbeat{false}; QTimer::singleShot(0, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
    QVERIFY(proxy.exportBusy()); QCOMPARE(completed.size(), 0); QVERIFY(!writer->on_gui);
    writer->release.release(); QTRY_VERIFY(!proxy.exportBusy()); QCOMPARE(completed.size(), 1);
    QCOMPARE(completed.first()[0].toULongLong(), ticket); QVERIFY(!completed.first()[1].toBool());
    QCOMPARE(proxy.exportError(), QStringLiteral("injected commit failure")); QVERIFY(proxy.exportedPath().isEmpty());
}
void ActivityFilterProxyModelTests::persistentTreePreservesRanksAcrossRotationsAndRemoval()
{
    activityview::Tree root;
    std::vector<activityview::Tree> retained;
    for (int i = 1023; i >= 0; --i) { const auto id = QString::number(i).rightJustified(4, '0'); root = activityview::Insert(root, id, std::make_shared<const activityview::Row>(activityview::Row{{Qt::UserRole, i}})); if (i % 128 == 0) retained.push_back(root); }
    QCOMPARE(activityview::Size(root), 1024); QVERIFY(activityview::Height(root) <= 12);
    for (int i = 0; i < 1024; ++i) { const auto id = QString::number(i).rightJustified(4, '0'); QCOMPARE(activityview::Rank(root, id), i); QCOMPARE(activityview::At(root, i)->row->value(Qt::UserRole).toInt(), i); }
    const auto original = root;
    for (int i = 0; i < 1024; i += 2) root = activityview::Erase(root, QString::number(i).rightJustified(4, '0'));
    QCOMPARE(activityview::Size(original), 1024); QCOMPARE(activityview::Size(root), 512); QVERIFY(activityview::Height(root) <= 11);
    for (int i = 0; i < 512; ++i) QCOMPARE(activityview::At(root, i)->row->value(Qt::UserRole).toInt(), 2 * i + 1);
}

void ActivityFilterProxyModelTests::searchMatchesLabelAddressAndTxid()
{
    TestActivityModel source;
    source.setRows({
        MakeRow("Pizza night", Transaction::RecvWithAddress, 10, "aaa", "bc1qpizza"),
        MakeRow("Coffee", Transaction::SendToAddress, 20, "txid-coffee", "bc1qcoffee"),
        MakeRow("Rent", Transaction::SendToOther, 30, "zzz", "bc1qrent"),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    proxy.setSearchText("pizza");
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Pizza night"});

    proxy.setSearchText("bc1qcoffee");
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Coffee"});

    proxy.setSearchText("ZZZ");
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Rent"});
}

void ActivityFilterProxyModelTests::filtersByDateBuckets()
{
    const QDate today = QDate::currentDate();
    const QDate start_of_week = today.addDays(-(today.dayOfWeek() - 1));
    const QDate start_of_month{today.year(), today.month(), 1};
    const QDate start_of_year{today.year(), 1, 1};
    const QDate start_of_next_week = start_of_week.addDays(7);
    const QDate start_of_next_month = start_of_month.addMonths(1);
    const QDate start_of_next_year = start_of_year.addYears(1);

    TestActivityModel source;
    source.setRows({
        MakeRow("Today", Transaction::RecvWithAddress, TimestampForLocalDate(today)),
        MakeRow("Yesterday", Transaction::RecvWithAddress, TimestampForLocalDate(today.addDays(-1))),
        MakeRow("Tomorrow", Transaction::RecvWithAddress, TimestampForLocalDate(today.addDays(1))),
        MakeRow("Week start", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_week)),
        MakeRow("Before week", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_week.addDays(-1))),
        MakeRow("Next week", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_next_week)),
        MakeRow("Month start", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_month)),
        MakeRow("Before month", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_month.addDays(-1))),
        MakeRow("Next month", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_next_month)),
        MakeRow("Year start", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_year)),
        MakeRow("Before year", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_year.addDays(-1))),
        MakeRow("Next year", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_next_year)),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    proxy.setDateFilter(ActivityFilterProxyModel::Today);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Today"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!ContainsLabel(proxy, "Yesterday"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!ContainsLabel(proxy, "Tomorrow"));

    proxy.setDateFilter(ActivityFilterProxyModel::ThisWeek);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Week start"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!ContainsLabel(proxy, "Before week"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!ContainsLabel(proxy, "Next week"));

    proxy.setDateFilter(ActivityFilterProxyModel::ThisMonth);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Month start"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!ContainsLabel(proxy, "Before month"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!ContainsLabel(proxy, "Next month"));

    proxy.setDateFilter(ActivityFilterProxyModel::ThisYear);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Year start"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!ContainsLabel(proxy, "Before year"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!ContainsLabel(proxy, "Next year"));
}

void ActivityFilterProxyModelTests::filtersByTypeBucketsAndKeepsPendingRequestsExclusive()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    ActivityRow request = MakeRow("Request", Transaction::RecvWithAddress, now);
    request.pending_request = true;

    TestActivityModel source;
    source.setRows({
        MakeRow("Received", Transaction::RecvFromOther, now),
        MakeRow("Sent", Transaction::SendToAddress, now),
        MakeRow("Self", Transaction::SendToSelf, now),
        MakeRow("Mined", Transaction::Generated, now),
        MakeRow("Other", Transaction::Other, now),
        request,
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    proxy.setTypeFilter(ActivityFilterProxyModel::Received);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Received"});

    proxy.setTypeFilter(ActivityFilterProxyModel::Sent);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Sent"});

    proxy.setTypeFilter(ActivityFilterProxyModel::Other);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Other"});

    proxy.setTypeFilter(ActivityFilterProxyModel::SentToSelf);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Self"});

    proxy.setTypeFilter(ActivityFilterProxyModel::Mined);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Mined"});

    proxy.setTypeFilter(ActivityFilterProxyModel::PaymentRequest);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Request"});
}

void ActivityFilterProxyModelTests::paymentRequestFilterIncludesAssociatedTransactions()
{
    auto received = MakeRow("Received", Transaction::RecvWithAddress, 30, "tx-received");
    received.has_payment_request = true;
    auto pending = MakeRow("Pending", Transaction::RecvWithAddress, 20);
    pending.pending_request = true;
    TestActivityModel source;
    source.setRows({received, pending, MakeRow("Sent", Transaction::SendToAddress, 10, "tx-sent")});
    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 3);
    proxy.setTypeFilter(ActivityFilterProxyModel::PaymentRequest);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 2);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Received"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Pending"));
    proxy.setTypeFilter(ActivityFilterProxyModel::Received);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Received"));
}

void ActivityFilterProxyModelTests::sortsByTimestampDescending()
{
    TestActivityModel source;
    source.setRows({
        MakeRow("Old", Transaction::RecvWithAddress, 10),
        MakeRow("New", Transaction::RecvWithAddress, 30),
        MakeRow("Middle", Transaction::RecvWithAddress, 20),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"New"});
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(1, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Middle"});
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(2, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Old"});
}

void ActivityFilterProxyModelTests::exportsCurrentFilteredRowsToCsv()
{
    const qint64 timestamp = TimestampForLocalDate(QDate::currentDate());
    ActivityRow request = MakeRow("Alice", Transaction::RecvWithAddress, timestamp, {}, "bc1qalice");
    request.pending_request = true;
    request.net_amount_sat = 10'000;
    request.status = Transaction::Unconfirmed;

    TestActivityModel source;
    source.setRows({
        request,
        MakeRow("Bob", Transaction::SendToAddress, timestamp - 1, "txid-bob", "bc1qbob"),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setTypeFilter(ActivityFilterProxyModel::PaymentRequest);

    QTemporaryDir temp_dir;
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("activity.csv");
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(Export(proxy, path));

    QFile file(path);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString csv = QString::fromUtf8(file.readAll());
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.startsWith("\"Confirmed\",\"Date\",\"Type\",\"Label\",\"Address\",\"Amount (BTC)\",\"ID\",\"Record\",\"Action ID\",\"Status\",\"Action amount (BTC)\"\n"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"false\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"Payment request\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"Alice\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"bc1qalice\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"0.00010000\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!csv.contains("txid-bob"));
}

void ActivityFilterProxyModelTests::exportsCsvUsingDisplayUnit()
{
    const qint64 timestamp = TimestampForLocalDate(QDate::currentDate());
    ActivityRow row = MakeRow("Alice", Transaction::RecvWithAddress, timestamp, "txid-alice", "bc1qalice");
    row.net_amount_sat = 123'456'789;

    TestActivityModel source;
    source.setRows({row});

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setDisplayUnit(3);

    QTemporaryDir temp_dir;
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("activity.csv");
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(Export(proxy, path));

    QFile file(path);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString csv = QString::fromUtf8(file.readAll());
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.startsWith("\"Confirmed\",\"Date\",\"Type\",\"Label\",\"Address\",\"Amount (sat)\",\"ID\",\"Record\",\"Action ID\",\"Status\",\"Action amount (sat)\"\n"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"123456789\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!csv.contains("\"1.23456789\""));
}

void ActivityFilterProxyModelTests::exportsCsvEscapesSignedRowsAndHandlesFailures()
{
    const qint64 timestamp = TimestampForLocalDate(QDate::currentDate());
    ActivityRow sent = MakeRow("Bob \"Builder\"", Transaction::SendToAddress, timestamp, "txid-bob", "bc1q,bob");
    sent.net_amount_sat = -123'456'789;

    TestActivityModel source;
    source.setRows({
        sent,
        MakeRow("Carol", Transaction::RecvWithAddress, timestamp - 1, "txid-carol", "bc1qcarol"),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    QTemporaryDir temp_dir;
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("activity.csv");
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(Export(proxy, QUrl::fromLocalFile(path).toString()));

    QFile file(path);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString csv = QString::fromUtf8(file.readAll());
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"Sent\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"Bob \"\"Builder\"\"\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"bc1q,bob\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"-1.23456789\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"txid-bob\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"true\""));

    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!Export(proxy, temp_dir.filePath("missing/activity.csv")));
}

void ActivityFilterProxyModelTests::filtersByCustomDateRange()
{
    const QDate start{2025, 6, 10};
    const QDate end{2025, 6, 20};

    TestActivityModel source;
    source.setRows({
        MakeRow("Before", Transaction::RecvWithAddress, TimestampForLocalDate(start.addDays(-1))),
        MakeRow("On start", Transaction::RecvWithAddress, TimestampForLocalDate(start)),
        MakeRow("Inside", Transaction::RecvWithAddress, TimestampForLocalDate(QDate(2025, 6, 15))),
        MakeRow("On end", Transaction::RecvWithAddress, TimestampForLocalDate(end)),
        MakeRow("After", Transaction::RecvWithAddress, TimestampForLocalDate(end.addDays(1))),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(proxy.applyCustomRange(start.toString(Qt::ISODate), end.toString(Qt::ISODate)));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.dateFilter(), ActivityFilterProxyModel::CustomRange);

    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 3);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "On start")); // lower bound inclusive
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Inside"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "On end"));   // upper bound inclusive (whole day)
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!ContainsLabel(proxy, "Before"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!ContainsLabel(proxy, "After"));
}

void ActivityFilterProxyModelTests::customRangeHandlesDstGapDayBoundaries()
{
    // America/Sao_Paulo entered daylight saving at midnight on 2018-11-04:
    // clocks jumped from 00:00 to 01:00, so that day has no midnight.
    // QDateTime{date, QTime(0, 0)} is invalid for it and compares before
    // every valid time, which silently broke both range boundaries; the
    // date's startOfDay() is the correct first instant. In a zone without
    // the gap this degenerates to an ordinary range check and still passes.
    const QByteArray previous_tz = qgetenv("TZ");
    qputenv("TZ", "America/Sao_Paulo");
    TzSet();
    const auto restore_tz = qScopeGuard([&previous_tz] {
        if (previous_tz.isEmpty()) {
            qunsetenv("TZ");
        } else {
            qputenv("TZ", previous_tz);
        }
        TzSet();
    });

    const QDate gap_day{2018, 11, 4};

    TestActivityModel source;
    source.setRows({
        MakeRow("Before", Transaction::RecvWithAddress, TimestampForLocalDate(gap_day.addDays(-1))),
        MakeRow("On gap day", Transaction::RecvWithAddress, TimestampForLocalDate(gap_day)),
        MakeRow("After", Transaction::RecvWithAddress, TimestampForLocalDate(gap_day.addDays(1))),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    // A range starting on the gap day must still exclude earlier rows.
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(proxy.applyCustomRange(gap_day.toString(Qt::ISODate),
                                   gap_day.addDays(1).toString(Qt::ISODate)));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 2);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!ContainsLabel(proxy, "Before"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "On gap day"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "After"));

    // A range whose exclusive upper bound lands on the gap day (inclusive
    // end date the day before) must not collapse to rejecting every row.
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(proxy.applyCustomRange(gap_day.addDays(-1).toString(Qt::ISODate),
                                   gap_day.addDays(-1).toString(Qt::ISODate)));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Before"));
}

void ActivityFilterProxyModelTests::rejectsInvalidCustomRange()
{
    TestActivityModel source;
    source.setRows({
        MakeRow("Inside", Transaction::RecvWithAddress, TimestampForLocalDate(QDate(2025, 6, 15))),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    // An unparseable, empty, or inverted range leaves the model untouched: the
    // previous filter keeps applying rather than collapsing to an empty list.
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!proxy.applyCustomRange("not-a-date", "2025-06-20"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!proxy.applyCustomRange("2025-06-10", ""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!proxy.applyCustomRange("2025-06-20", "2025-06-10"));

    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.dateFilter(), ActivityFilterProxyModel::DateAll);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!proxy.rangeStart().isValid());
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!proxy.rangeEnd().isValid());
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);

    // A single-day range is not inverted and is accepted.
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(proxy.applyCustomRange("2025-06-15", "2025-06-15"));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);
}

void ActivityFilterProxyModelTests::appliesCustomRangeWithOneNotification()
{
    TestActivityModel source;
    source.setRows({
        MakeRow("Inside", Transaction::RecvWithAddress, TimestampForLocalDate(QDate(2025, 6, 15))),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QVERIFY(WaitForProxy(proxy));

    QSignalSpy range_spy(&proxy, &ActivityFilterProxyModel::rangeChanged);
    QSignalSpy date_filter_spy(&proxy, &ActivityFilterProxyModel::dateFilterChanged);
    QSignalSpy count_spy(&proxy, &ActivityFilterProxyModel::countChanged);

    // Setting both ends and the date filter together notifies once, not once
    // per end, so no observer sees a half-applied range.
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(proxy.applyCustomRange("2025-06-10", "2025-06-20"));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(range_spy.count(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(date_filter_spy.count(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(count_spy.count(), 1);

    // Re-applying the same range is a no-op and stays silent.
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(proxy.applyCustomRange("2025-06-10", "2025-06-20"));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(range_spy.count(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(date_filter_spy.count(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(count_spy.count(), 1);

    // A rejected range notifies nothing either.
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!proxy.applyCustomRange("2025-06-20", "2025-06-10"));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(range_spy.count(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(date_filter_spy.count(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(count_spy.count(), 1);

    // Moving only the range while the filter is already CustomRange notifies
    // the range but not the filter.
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(proxy.applyCustomRange("2025-06-11", "2025-06-21"));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(range_spy.count(), 2);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(date_filter_spy.count(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(count_spy.count(), 2);
}

void ActivityFilterProxyModelTests::filtersByMinimumAmount()
{
    ActivityRow small = MakeRow("Small", Transaction::RecvWithAddress, 10);
    small.net_amount_sat = 50'000;
    ActivityRow big_receive = MakeRow("Big receive", Transaction::RecvWithAddress, 20);
    big_receive.net_amount_sat = 200'000;
    ActivityRow big_send = MakeRow("Big send", Transaction::SendToAddress, 30);
    big_send.net_amount_sat = -200'000;

    TestActivityModel source;
    source.setRows({small, big_receive, big_send});

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    proxy.setMinAmount(100'000);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 2);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Big receive"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Big send")); // absolute value matches the threshold
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!ContainsLabel(proxy, "Small"));

    proxy.setMinAmount(-1); // clearing restores every row
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 3);
}

void ActivityFilterProxyModelTests::exportsCsvNeutralizesFormulaInjection()
{
    const qint64 timestamp = TimestampForLocalDate(QDate::currentDate());
    ActivityRow hostile = MakeRow("=HYPERLINK(\"http://evil.test\",\"click\")",
                                  Transaction::RecvWithAddress, timestamp,
                                  "txid-hostile", "@bc1qhostile");
    ActivityRow negative = MakeRow("-2+3+cmd", Transaction::SendToAddress, timestamp - 1,
                                   "txid-negative", "bc1qplain");
    negative.net_amount_sat = -123'456'789;
    ActivityRow linefeed = MakeRow("\n=1+2", Transaction::RecvWithAddress, timestamp - 2,
                                   "txid-linefeed", "bc1qlinefeed");

    TestActivityModel source;
    source.setRows({hostile, negative, linefeed});

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    QTemporaryDir temp_dir;
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("activity.csv");
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(Export(proxy, QUrl::fromLocalFile(path).toString()));

    QFile file(path);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString csv = QString::fromUtf8(file.readAll());

    // Text cells with a dangerous leading character are neutralized...
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"'=HYPERLINK(\"\"http://evil.test\"\",\"\"click\"\")\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"'@bc1qhostile\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"'-2+3+cmd\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"'\n=1+2\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!csv.contains("\"=HYPERLINK"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!csv.contains("\"\n=1+2"));

    // ...while the numeric amount column keeps its minus sign untouched.
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"-1.23456789\""));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(csv.contains("\"bc1qplain\""));
}

void ActivityFilterProxyModelTests::combinesTypeSelections()
{
    auto request = MakeRow("Request", Transaction::Other, 40);
    request.pending_request = true;
    TestActivityModel source;
    source.setRows({MakeRow("Receive", Transaction::RecvWithAddress, 10),
                    MakeRow("Send", Transaction::SendToAddress, 20),
                    MakeRow("Mined", Transaction::Generated, 30), request});
    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setTypeFilters({ActivityFilterProxyModel::Sent, ActivityFilterProxyModel::Received, ActivityFilterProxyModel::Sent});
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 2);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.typeFilters().size(), 2);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Send"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Receive"));
    proxy.setTypeFilters({ActivityFilterProxyModel::Sent, ActivityFilterProxyModel::PaymentRequest});
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 2);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Request"));
    proxy.setTypeFilters({});
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.typeFilter(), ActivityFilterProxyModel::TypeAll);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 4); // All transactions and unpaid requests are visible.
    proxy.setTypeFilter(ActivityFilterProxyModel::Mined); // Legacy single selection still works.
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.typeFilters(), QList<int>{ActivityFilterProxyModel::Mined});
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);
}

void ActivityFilterProxyModelTests::filtersByInclusiveAmountRange()
{
    auto send = MakeRow("Send", Transaction::SendToAddress, 10);
    auto receive = MakeRow("Receive", Transaction::RecvWithAddress, 20);
    auto larger = MakeRow("Larger", Transaction::RecvWithAddress, 30);
    send.net_amount_sat = -200;
    receive.net_amount_sat = 200;
    larger.net_amount_sat = 201;
    TestActivityModel source;
    source.setRows({send, receive, larger});
    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(proxy.setAmountRange(200, 200));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 2);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Send"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Receive"));
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(!proxy.setAmountRange(201, 200));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.minAmount(), 200);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.maxAmount(), 200);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(proxy.setAmountRange(-1, 200));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 2);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(proxy.setAmountRange(-1, -1));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 3);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(proxy.setAmountRange(0, 0));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 0);
}

void ActivityFilterProxyModelTests::refiltersWhenSourceAmountChanges()
{
    auto send = MakeRow("Send", Transaction::SendToAddress, 10, "tx-send");
    auto receive = MakeRow("Receive", Transaction::RecvWithAddress, 20, "tx-receive");
    send.status = receive.status = Transaction::Unconfirmed;
    send.net_amount_sat = -150;
    receive.net_amount_sat = 250;
    TestActivityModel source;
    source.setRows({send, receive});

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(proxy.setAmountRange(100, 200));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.count(), 1);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Send"));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.pendingBalanceSat(), -150);

    int notified_count{-1};
    connect(&proxy, &ActivityFilterProxyModel::countChanged, &proxy, [&] {
        notified_count = proxy.count();
    });

    // Only NetAmountSatRole changes: keep the filter fixed and do not reset
    // the source model or explicitly invalidate the proxy from the test.
    source.setAmount(0, -50);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.count(), 0);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(notified_count, 0);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.pendingBalanceSat(), 0);

    source.setAmount(1, 175);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.count(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(notified_count, 1);
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(ContainsLabel(proxy, "Receive"));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::NetAmountSatRole).toLongLong(), 175);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.pendingBalanceSat(), 175);

    source.setAmount(0, -125);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.count(), 2);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(notified_count, 2);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::TxidRole).toString(), QString{"tx-receive"});
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(1, 0).data(TransactionActivityModel::TxidRole).toString(), QString{"tx-send"});
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.pendingBalanceSat(), 50);

    // An already-visible row also exposes its updated data and clears the
    // cached pending balance even when the filtered row count stays the same.
    source.setAmount(1, 180);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.count(), 2);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::NetAmountSatRole).toLongLong(), 180);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.pendingBalanceSat(), 55);
}

void ActivityFilterProxyModelTests::availableMaximumUsesUnfilteredTransactionsAndUpdates()
{
    auto send = MakeRow("Send", Transaction::SendToAddress, 10);
    auto receive = MakeRow("Receive", Transaction::RecvWithAddress, 20);
    auto request = MakeRow("Request", Transaction::Other, 30);
    send.net_amount_sat = -500;
    receive.net_amount_sat = 200;
    request.pending_request = true;
    request.net_amount_sat = 9000;
    TestActivityModel source;
    source.setRows({send, receive, request});
    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.availableMaxAmount(), 500);
    proxy.setSearchText("Receive");
    QVERIFY(WaitForProxy(proxy));
    QVERIFY(proxy.setAmountRange(0, 300));
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.availableMaxAmount(), 500);
    source.setAmount(0, -700);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.availableMaxAmount(), 700);
    source.setRows({receive, request});
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.availableMaxAmount(), 200);
    TestActivityModel other;
    proxy.setSourceModel(&other);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.availableMaxAmount(), 0);
    source.setRows({send}); // A previous wallet must no longer affect the range.
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.availableMaxAmount(), 0);
    other.setRows({send});
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.availableMaxAmount(), 500);
    proxy.setSourceModel(nullptr);
    QVERIFY(WaitForProxy(proxy));
    QCOMPARE(proxy.availableMaxAmount(), 0);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ActivityFilterProxyModelTests)
#else
QTEST_MAIN(ActivityFilterProxyModelTests)
#endif
#include <test_activityfilterproxymodel.moc>
