// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <qml/models/nodemodel.h>
#include <qml/guipresentation.h>
#include <qml/retainedvalue.h>

#include <common/args.h>
#include <common/system.h>
#include <chainparams.h>
#include <interfaces/node.h>
#include <net.h>
#include <net_processing.h>
#include <netbase.h>
#include <node/interface_ui.h>
#include <util/fs.h>
#include <util/time.h>
#include <validation.h>

#include <cassert>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <vector>
#include <mutex>
#include <optional>
#include <utility>
#include <atomic>

#include <QDateTime>
#include <QCoreApplication>
#include <QMetaObject>
#include <QScopedValueRollback>
#include <QThread>
#include <QTimer>
#include <QVariantMap>

using namespace std::chrono_literals;

static constexpr int MEMPOOL_INFO_POLLING_INTERVAL_MS{3000};
static constexpr int HEADER_HEIGHT_DELTA_SYNC{24};

namespace {
QStringList SplitWarnings(const QString& warnings)
{
    QString normalized{warnings};
    normalized.replace(QStringLiteral("<hr/>"), QStringLiteral("<hr />"), Qt::CaseInsensitive);
    normalized.replace(QStringLiteral("<hr>"), QStringLiteral("<hr />"), Qt::CaseInsensitive);

    QStringList result;
    for (const QString& warning : normalized.split(QStringLiteral("<hr />"), Qt::SkipEmptyParts)) {
        const QString trimmed{warning.trimmed()};
        if (!trimmed.isEmpty()) {
            result.push_back(trimmed);
        }
    }
    return result;
}

QString RuntimeDialogTitle(unsigned int style)
{
    if (style & CClientUIInterface::ICON_ERROR) {
        return QObject::tr("Error");
    }
    if (style & CClientUIInterface::ICON_WARNING) {
        return QObject::tr("Warning");
    }
    return QObject::tr("Information");
}

QString RuntimeDialogIcon(unsigned int style)
{
    if (style & CClientUIInterface::ICON_ERROR) {
        return QStringLiteral("image://images/error");
    }
    if (style & CClientUIInterface::ICON_WARNING) {
        return QStringLiteral("image://images/alert-filled");
    }
    return QStringLiteral("image://images/info-filled");
}

unsigned int RuntimeDialogButtons(unsigned int style)
{
    const unsigned int buttons{style & CClientUIInterface::BTN_MASK};
    return buttons ? buttons : CClientUIInterface::BTN_OK;
}

bool HasMultipleRuntimeDialogButtons(unsigned int buttons)
{
    return (buttons & (buttons - 1)) != 0;
}

QVariant InformationRow(const QString& label, const QString& value)
{
    QVariantMap row;
    row.insert(QStringLiteral("label"), label);
    row.insert(QStringLiteral("value"), value);
    return QVariant::fromValue(row);
}
} // namespace

// Core callbacks share a bounded mailbox and coalesce changes into one pending
// GUI delivery. The mutex also protects the receiver until shutdown detaches it.
struct NodeModel::Notifications : std::enable_shared_from_this<Notifications> {
    struct Block { SynchronizationState state; interfaces::BlockTip tip; double progress; };
    struct Header { interfaces::BlockTip tip; bool presync; };
    std::mutex mutex;
    NodeModel* receiver{nullptr};
    std::shared_ptr<execution::Scheduler> scheduler;
    std::shared_ptr<std::atomic_bool> diagnostic_loss;
    std::shared_ptr<startupdiagnostics::TextBudget> text_budget;
    std::function<void(std::function<void()>)> post;
    bool delivery_pending{false};
    bool ready{false};
    std::optional<bool> subscribed;
    QString subscription_error;
    int64_t header_spacing{600};
    std::deque<std::shared_ptr<RuntimeDialogRequest>> dialogs;
    std::vector<std::weak_ptr<RuntimeDialogRequest>> runtime_owned;
    bool runtime_overflow{false};
    bool request(QString message, unsigned int style, bool question) {
        const bool gui = QThread::currentThread() == QCoreApplication::instance()->thread();
        // A synchronous Core answer cannot be awaited on GUI. Production Core
        // work belongs to a background lane; accidental GUI questions reject.
        if (gui && question) return false;
        { std::lock_guard lock{mutex}; if (!receiver) return false; }
        auto request = std::make_shared<RuntimeDialogRequest>();
        request->message = message.left(16384);
        if (message.size() > 16384) request->message += QStringLiteral(" ") + QObject::tr("[additional message text omitted]");
        request->full_message = startupdiagnostics::RetainText(scheduler, text_budget, std::move(message));
        if (!request->full_message) {
            { std::lock_guard lock{mutex}; if (receiver && diagnostic_loss) *diagnostic_loss = true; }
            request->message += QStringLiteral(" ") + QObject::tr("[complete message unavailable: diagnostic capacity exhausted]");
        }
        request->style = style; request->question = question;
        {
            std::lock_guard lock{mutex};
            if (!receiver) return false;
            std::erase_if(runtime_owned, [](const auto& value) { return value.expired(); });
            if (runtime_owned.size() >= 32) { runtime_overflow = true; schedule(); return false; }
            runtime_owned.emplace_back(request); dialogs.push_back(request); schedule();
        }
        if (gui || (!(style & CClientUIInterface::MODAL) && !question)) return false;
        std::unique_lock lock{request->mutex};
        request->condition.wait(lock, [&] { return request->answered; });
        return request->answer;
    }
    std::optional<Block> block;
    std::optional<Header> header;
    std::optional<int> peers;
    std::optional<int> last_peer_total;
    std::optional<bool> network_active;
    bool warnings{false};
    bool bans{false};
    uint64_t status_generation{0};
    uint64_t network_generation{0};

    // Called with mutex held, so detaching the receiver cannot race dispatch.
    void schedule()
    {
        if (!receiver || delivery_pending) return;
        delivery_pending = true;
        post([target = receiver, state = shared_from_this()] {
            {
                std::lock_guard lock{state->mutex};
                if (state->receiver != target) return;
                state->delivery_pending = false;
            }
            target->publishNotifications();
        });
    }
};

struct NodeModel::StatusSnapshot {
    int inbound{0};
    int outbound{0};
    QString warnings;
    std::optional<bool> network_active;
    bool mempool_available{false};
    uint64_t generation{0}, network_generation{0};
    quint64 received{0}, sent{0};
    QString startup_time, user_agent;
    QStringList warning_list;
    QString local_addresses, data_dir, blocks_dir, network;
    int header_height{0}, block_height{0};
    int64_t header_time{0}, block_time{0};
};

struct NodeModel::WorkLedger {
    std::mutex admission;
    std::atomic_size_t pending{0};
    std::function<void()> notify;
};
class NodeModel::TrackedScheduler final : public execution::Scheduler {
    struct ScopeLifetime {
        std::shared_ptr<WorkLedger> ledger;
        explicit ScopeLifetime(std::shared_ptr<WorkLedger> value) : ledger(std::move(value)) { ++ledger->pending; }
        ~ScopeLifetime() { --ledger->pending; try { ledger->notify(); } catch (...) {} }
    };
    struct Packet final : execution::Task {
        std::shared_ptr<execution::Task> task;
        std::shared_ptr<WorkLedger> ledger;
        Packet(std::shared_ptr<execution::Task> value, std::shared_ptr<WorkLedger> state)
            : task(std::move(value)), ledger(std::move(state)) { ++ledger->pending; }
        ~Packet() override { task.reset(); --ledger->pending; try { ledger->notify(); } catch (...) {} }
        void execute() override { task->execute(); }
        void deliver() override { task->deliver(); }
        void notStarted(execution::NotStarted::Cause cause) override { task->notStarted(cause); }
    };
    std::shared_ptr<ScopeLifetime> m_scope_lifetime;
    std::shared_ptr<execution::Scheduler> m_upstream;
    std::shared_ptr<WorkLedger> m_ledger;
    std::atomic_bool m_closed{false};
public:
    TrackedScheduler(std::shared_ptr<execution::Scheduler> upstream, std::shared_ptr<WorkLedger> ledger, std::shared_ptr<ScopeLifetime> scope = {})
        : m_scope_lifetime(std::move(scope)), m_upstream(std::move(upstream)), m_ledger(std::move(ledger)) {}
    std::shared_ptr<execution::Scheduler> openActionScope(size_t bytes = 256) override {
        std::shared_ptr<ScopeLifetime> lifetime;
        {
            std::lock_guard lock{m_ledger->admission};
            if (m_closed) return {};
            // Register before touching the upstream port. Quiet close can then
            // fence new origins without waiting for an upstream admission call.
            lifetime = std::make_shared<ScopeLifetime>(m_ledger);
        }
        auto scope = m_upstream->openActionScope(bytes);
        if (!scope) return {};
        return std::make_shared<TrackedScheduler>(std::move(scope), m_ledger, std::move(lifetime));
    }
    execution::Admission submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override {
        std::shared_ptr<Packet> packet;
        {
            std::lock_guard lock{m_ledger->admission};
            if (m_closed) return {{}, execution::Rejection::Closed};
            if (!task) return {{}, execution::Rejection::Invalid};
            packet = std::make_shared<Packet>(std::move(task), m_ledger);
        }
        return m_upstream->submit(std::move(packet), spec);
    }
    execution::Admission reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override {
        std::shared_ptr<Packet> packet;
        {
            std::lock_guard lock{m_ledger->admission};
            if (m_closed) return {{}, execution::Rejection::Closed};
            if (!task) return {{}, execution::Rejection::Invalid};
            packet = std::make_shared<Packet>(std::move(task), m_ledger);
        }
        return m_upstream->reserveCleanup(std::move(packet), spec);
    }
    bool activateCleanup(execution::TaskId id) override { return m_upstream->activateCleanup(id); }
    execution::BorrowAdmission borrow(execution::ResourceKey key) override { return m_closed ? execution::BorrowAdmission{{}, execution::Rejection::Closed} : m_upstream->borrow(key); }
    execution::ResourceKey allocateKey() override { return m_upstream->allocateKey(); }
    size_t pending() const override { return m_ledger->pending; }
    void closeOrdinaryAdmission() override { std::lock_guard lock{m_ledger->admission}; m_closed = true; }
    bool cancelQueued(execution::TaskId id) override { return m_upstream->cancelQueued(id); }
};
struct NodeModel::Subscriptions {
    std::vector<std::unique_ptr<interfaces::Handler>> handlers;
};
struct NodeModel::ShutdownSnapshot { bool requested{false}; };

NodeModel::NodeModel(interfaces::Node& node, bool backend_ready)
    : NodeModel(node, backend_ready, Services{}) {}

NodeModel::NodeModel(interfaces::Node& node, bool backend_ready, Services services)
    : m_notifications(std::make_shared<Notifications>()), m_services(std::move(services)),
      m_snapshots(m_services.execution.scheduler ? nullptr
          : std::make_unique<BackendExecutor>(execution::Limits::Bounded(), this)),
      m_work_ledger(std::make_shared<WorkLedger>()), m_subscriptions(std::make_shared<Subscriptions>()),
      m_backend_queries_ready(backend_ready), m_node(node)
{
    m_notifications->receiver = this;
    if (!m_services.wall_seconds) m_services.wall_seconds = [] {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    };
    if (!m_services.monotonic_milliseconds) {
        const auto start = std::chrono::steady_clock::now();
        m_services.monotonic_milliseconds = [start] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(); };
    }
    if (!m_services.execution.scheduler) m_services.execution.scheduler = m_snapshots->scheduler();
    const QPointer<NodeModel> observer{this};
    if (!m_services.execution.post) m_services.execution.post = presentation::QueuedPost();
    const auto post = m_services.execution.post;
    m_notifications->post = post;
    m_work_ledger->notify = [observer, post] { post([observer] { if (observer) observer->finishDrain(); }); };
    m_query_scheduler = std::make_shared<TrackedScheduler>(m_services.execution.scheduler, m_work_ledger);
    m_retirement_scheduler = std::make_shared<TrackedScheduler>(m_services.execution.scheduler, m_work_ledger);
    m_notifications->scheduler = m_query_scheduler;
    m_notifications->diagnostic_loss = m_services.diagnostic_loss;
    m_notifications->text_budget = m_dialog_text_budget;
    m_startup_diagnostics = new StartupDiagnosticsModel({m_query_scheduler, post}, this);
    connect(m_startup_diagnostics, &StartupDiagnosticsModel::changed, this, &NodeModel::startupDiagnosticsChanged);
    connect(m_startup_diagnostics, &StartupDiagnosticsModel::finished, this, &NodeModel::finishStartupDiagnostics);
    initializeMempoolInfoPolling();
    initializeSubscriptions();
    if (m_snapshots) connect(m_snapshots.get(), &BackendExecutor::drained, this, &NodeModel::finishDrain);
    refreshStatus();
    refreshMempoolInfo();
}

NodeModel::~NodeModel()
{
    m_destroying = true;
    beginShutdown();
}

void NodeModel::closeAdmission()
{
    m_admission_closed = true;
    if (m_startup_diagnostics) m_startup_diagnostics->closeAdmission();
    if (m_runtime_diagnostics && m_runtime_diagnostics != m_startup_diagnostics) m_runtime_diagnostics->closeAdmission();
    if (m_query_scheduler) m_query_scheduler->closeOrdinaryAdmission();
}

void NodeModel::beginShutdown()
{
    if (m_workers_stopping) return;
    if (m_startup_diagnostics) m_startup_diagnostics->closeAdmission();
    if (m_runtime_diagnostics && m_runtime_diagnostics != m_startup_diagnostics) m_runtime_diagnostics->closeAdmission();
    m_initialization_result.reset();
    detachNotifications();
    clearRuntimeDialogs(false);
    setMempoolInfoPollingActive(false);
    m_workers_stopping = true;
    if (m_status_query) m_status_query->close();
    if (m_mempool_query) m_mempool_query->close();
    if (m_shutdown_query) m_shutdown_query->close();
    m_query_scheduler->closeOrdinaryAdmission();
    m_retirement_scheduler->closeOrdinaryAdmission();
    m_subscription_lease.reset();
    if (!m_destroying) Q_EMIT networkActionReadyChanged();
    if (m_destroying) { m_node_ready = false; std::lock_guard lock{m_notifications->mutex}; m_notifications->ready = false; }
    else setNodeReady(false);
    stopShutdownPolling();
    if (m_snapshots) m_snapshots->shutdown();
    finishDrain();
}

bool NodeModel::retireValues(std::function<void()> release)
{
    return bool(execution::Submit(*m_retirement_scheduler, std::move(release), [] {}, {},
        {execution::Lane::Cleanup, 1024, {}, execution::WorkClass::Action}));
}

void NodeModel::finishDrain()
{
    if (!m_workers_stopping || m_drained || m_work_ledger->pending ||
        (m_snapshots && !m_snapshots->isDrained())) return;
    m_drained = true;
    Q_EMIT workDrained();
}

void NodeModel::detachNotifications()
{
    std::vector<std::shared_ptr<RuntimeDialogRequest>> requests;
    { std::lock_guard lock{m_notifications->mutex};
      m_notifications->receiver = nullptr;
      for (const auto& weak : m_notifications->runtime_owned) if (auto request = weak.lock()) requests.push_back(std::move(request));
      m_notifications->dialogs.clear();
    }
    // Do not wait for Core callbacks while a question is awaiting the user.
    for (const auto& request : requests) {
        { std::lock_guard lock{request->mutex}; request->answered = true; request->answer = false; }
        request->condition.notify_all();
    }
}

void NodeModel::publishNotifications()
{
    if (m_workers_stopping || m_shutdown_requested) return;
    std::optional<Notifications::Block> block;
    std::optional<Notifications::Header> header;
    std::optional<int> peers;
    std::optional<bool> network_active;
    bool warnings;
    bool bans;
    std::optional<bool> subscribed;
    QString subscription_error;
    std::deque<std::shared_ptr<RuntimeDialogRequest>> dialogs;
    bool runtime_overflow;
    {
        std::lock_guard lock{m_notifications->mutex};
        block = std::exchange(m_notifications->block, {});
        header = std::exchange(m_notifications->header, {});
        peers = std::exchange(m_notifications->peers, {});
        network_active = std::exchange(m_notifications->network_active, {});
        warnings = std::exchange(m_notifications->warnings, false);
        bans = std::exchange(m_notifications->bans, false);
    }
    {
        // Decide sync completion only after both heights belong to this snapshot.
        const QScopedValueRollback applying{m_applying_sync_snapshot, true};
        if (header) {
            m_seen_header_tip = true;
            setHeaderSyncState(header->tip.block_height, header->tip.block_time, header->presync);
        }
        if (block) {
            m_seen_block_tip = true;
            m_block_tip_time = block->tip.block_time;
            setBlockTipHeight(block->tip.block_height);
            setVerificationProgress(block->progress);
            setBlockSyncActive(block->state != SynchronizationState::POST_INIT);
            Q_EMIT blockTipTimeChanged(m_block_tip_time);
        }
    }
    {
        std::lock_guard lock{m_notifications->mutex};
        subscribed = std::exchange(m_notifications->subscribed, {});
        subscription_error = m_notifications->subscription_error;
        m_header_spacing = m_notifications->header_spacing;
        dialogs.swap(m_notifications->dialogs);
        runtime_overflow = std::exchange(m_notifications->runtime_overflow, false);
    }
    if (subscribed) {
        m_subscriptions_ready = *subscribed;
        Q_EMIT notificationsReadyChanged();
        Q_EMIT networkActionReadyChanged();
        if (!*subscribed) { setStartupError(subscription_error); setErrorState(true); requestShutdown(); return; }
        if (m_initialization_requested && !m_initialization_dispatched) { m_initialization_dispatched = true; Q_EMIT requestedInitialize(); }
    }
    if (runtime_overflow) { m_status_error = tr("Some node messages exceeded the dialog queue limit."); Q_EMIT observationErrorChanged(); }
    for (const auto& request : dialogs) {
        bool answered;
        { std::lock_guard lock{request->mutex}; answered = request->answered; }
        if (answered) continue;
        if (!m_runtime_dialogs_enabled && !request->question) {
            if (request->style & CClientUIInterface::ICON_WARNING) {
                recordStartupWarningMessage(request->full_message ? *request->full_message : request->message);
                { std::lock_guard lock{request->mutex}; request->answered = true; request->answer = false; }
                request->condition.notify_all(); continue;
            } else if (!(request->style & CClientUIInterface::MODAL)) {
                if (request->style & CClientUIInterface::ICON_ERROR) recordStartupErrorMessage(request->full_message ? *request->full_message : request->message);
                continue;
            }
            if (!(request->style & CClientUIInterface::MODAL)) continue;
        }
        if (!m_runtime_dialogs_enabled && (request->question || (request->style & CClientUIInterface::ICON_ERROR))) m_startup_failure_dialog_shown = true;
        if (m_runtime_dialog_active && m_runtime_dialog_queue.size() >= 31) {
            { std::lock_guard lock{request->mutex}; request->answered = true; request->answer = false; }
            request->condition.notify_all();
            m_status_error = tr("A node dialog could not be shown because the dialog queue is full.");
            Q_EMIT observationErrorChanged(); continue;
        }
        if (m_runtime_dialog_active) m_runtime_dialog_queue.push_back(request);
        else showRuntimeDialogRequest(request);
    }
    updateSyncCompletion();
    if (block || header || peers || warnings || network_active) refreshStatus();
    if (network_active) {
        m_network_known = true;
        if (m_pause != !*network_active) {
            m_pause = !*network_active;
            Q_EMIT pauseChanged(m_pause);
        }
    }
    if (bans) Q_EMIT bannedListChanged();
    if (block || header || peers || warnings || network_active) Q_EMIT nodeInformationChanged();
}

QString NodeModel::observationError() const
{
    QStringList errors{m_status_error, m_mempool_error, m_shutdown_error};
    errors.removeAll(QString{});
    return errors.join(QLatin1Char('\n'));
}

void NodeModel::refreshStatus()
{
    if (!m_backend_queries_ready || m_admission_closed || m_shutdown_requested || m_workers_stopping) return;
    if (!m_status_query) {
        const QPointer<NodeModel> observer{this};
        m_status_query = std::make_shared<execution::SnapshotQuery<StatusSnapshot>>(m_query_scheduler,
            [node = &m_node, state = m_notifications] {
                StatusSnapshot result;
                bool ready;
                { std::lock_guard lock{state->mutex}; ready = state->ready; result.generation = state->status_generation; result.network_generation = state->network_generation; }
                result.inbound = node->getNodeCount(ConnectionDirection::In);
                result.outbound = node->getNodeCount(ConnectionDirection::Out);
                const auto warning_text = QString::fromStdString(node->getWarnings().translated);
                result.warnings = warning_text.left(32768);
                if (result.warnings.size() < warning_text.size()) result.warnings += QStringLiteral(" ") + QObject::tr("[additional warning text omitted]");
                result.warning_list = SplitWarnings(result.warnings);
                result.network_active = ready ? std::optional<bool>{node->getNetworkActive()} : std::nullopt;
                result.mempool_available = !gArgs.GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY);
                result.data_dir = QString::fromStdString(fs::PathToString(gArgs.GetDataDirNet()));
                result.blocks_dir = QString::fromStdString(fs::PathToString(gArgs.GetBlocksDirPath()));
                result.network = QString::fromStdString(Params().GetChainTypeString());
                result.startup_time = QDateTime::currentDateTime().addSecs(-TicksSeconds(GetUptime())).toString();
                result.user_agent = QString::fromStdString(strSubVersion);
                result.received = std::max(int64_t{0}, node->getTotalBytesRecv()); result.sent = std::max(int64_t{0}, node->getTotalBytesSent());
                if (ready) {
                    node->getHeaderTip(result.header_height, result.header_time);
                    result.block_height = node->getNumBlocks(); result.block_time = node->getLastBlockTime();
                    for (const auto& [addr, info] : node->getNetLocalAddresses()) {
                        if (!result.local_addresses.isEmpty()) result.local_addresses += QStringLiteral(", ");
                        result.local_addresses += QString::fromStdString(addr.ToStringAddr());
                        if (!addr.IsI2P()) result.local_addresses += QStringLiteral(":") + QString::number(info.nPort);
                        if (result.local_addresses.size() > 16384) { result.local_addresses = result.local_addresses.left(16384) + QStringLiteral(" ") + QObject::tr("[additional addresses omitted]"); break; }
                    }
                }
                return result;
            }, [observer, post = m_services.execution.post] { post([observer] { if (observer) observer->publishStatus(); }); });
    }
    m_status_query->request();
}

void NodeModel::publishStatus()
{
    const auto result = m_status_query->take();
    if (m_workers_stopping || m_shutdown_requested || !result) return;
    const auto error = result->error ? tr("Node information could not be refreshed.") : QString{};
    if (m_status_error != error) { m_status_error = error; Q_EMIT observationErrorChanged(); }
    if (!result->snapshot) return; // A failed read preserves the previous facts.
    const auto& snapshot = *result->snapshot;
    bool network_current;
    std::optional<int> total;
    { std::lock_guard lock{m_notifications->mutex}; network_current = snapshot.network_generation == m_notifications->network_generation; total = m_notifications->last_peer_total; }
    m_status_ready = true;
    m_information_local_addresses = snapshot.local_addresses;
    m_information_data_dir = snapshot.data_dir; m_information_blocks_dir = snapshot.blocks_dir;
    m_information_network = snapshot.network; m_information_startup_time = snapshot.startup_time;
    m_information_user_agent = snapshot.user_agent;
    m_information_header_height = snapshot.header_height; m_information_header_time = snapshot.header_time;
    m_information_block_height = snapshot.block_height; m_information_block_time = snapshot.block_time;
    m_total_bytes_received = snapshot.received; m_total_bytes_sent = snapshot.sent;
    setNumInboundPeers(snapshot.inbound); setNumOutboundPeers(snapshot.outbound);
    setNumPeers(total.value_or(snapshot.inbound + snapshot.outbound));
    if (m_warnings != snapshot.warnings || m_warning_list != snapshot.warning_list) {
        m_warnings = snapshot.warnings; m_warning_list = snapshot.warning_list; Q_EMIT warningsChanged();
    }
    if (snapshot.network_active && network_current) {
        m_network_known = true;
        if (m_pause != !*snapshot.network_active) { m_pause = !*snapshot.network_active; Q_EMIT pauseChanged(m_pause); }
    }
    if (m_mempool_information_available != snapshot.mempool_available) {
        m_mempool_information_available = snapshot.mempool_available; Q_EMIT mempoolInfoChanged();
    }
    Q_EMIT nodeInformationChanged();
}

void NodeModel::setBlockTipHeight(int new_height)
{
    if (new_height != m_block_tip_height) {
        m_block_tip_height = new_height;
        Q_EMIT blockTipHeightChanged();
        updateSyncCompletion();
    }
}

void NodeModel::setNumOutboundPeers(int new_num)
{
    if (new_num != m_num_outbound_peers) {
        m_num_outbound_peers = new_num;
        Q_EMIT numOutboundPeersChanged();
    }
}

void NodeModel::setNumPeers(int new_num)
{
    if (new_num != m_num_peers) {
        m_num_peers = new_num;
        Q_EMIT numPeersChanged();
    }
}

void NodeModel::setNumInboundPeers(int new_num)
{
    if (new_num != m_num_inbound_peers) {
        m_num_inbound_peers = new_num;
        Q_EMIT numInboundPeersChanged();
    }
}

void NodeModel::refreshMempoolInfo()
{
    if (!m_backend_queries_ready || m_admission_closed || m_shutdown_requested || m_workers_stopping) return;
    requestMempoolInfoRefresh();
}

void NodeModel::setMempoolInfoPollingActive(bool active)
{
    if (m_workers_stopping || m_mempool_info_polling_active == active) return;
    m_mempool_info_polling_active = active;
    if (!m_destroying) Q_EMIT mempoolInfoPollingActiveChanged(active);
    if (active && m_backend_queries_ready) { schedulePoll(Poll::Mempool); refreshMempoolInfo(); }
    else if (m_services.cancel) m_services.cancel(Poll::Mempool);
}

void NodeModel::initializeMempoolInfoPolling()
{
    m_mempool_info_timer = new QTimer(this);
    m_shutdown_timer = new QTimer(this);
    m_mempool_info_timer->setSingleShot(true); m_shutdown_timer->setSingleShot(true);
    connect(m_mempool_info_timer, &QTimer::timeout, this, [this] {
        auto callback = std::exchange(m_mempool_deadline, {}); if (callback) callback();
    });
    connect(m_shutdown_timer, &QTimer::timeout, this, [this] {
        auto callback = std::exchange(m_shutdown_deadline, {}); if (callback) callback();
    });
    if (!m_services.schedule) m_services.schedule = [this](Poll poll, std::chrono::milliseconds delay, auto callback) {
        auto* timer = poll == Poll::Mempool ? m_mempool_info_timer : m_shutdown_timer;
        auto& deadline = poll == Poll::Mempool ? m_mempool_deadline : m_shutdown_deadline;
        deadline = std::move(callback); timer->start(int(delay.count()));
    };
    if (!m_services.cancel) m_services.cancel = [this](Poll poll) {
        auto* timer = poll == Poll::Mempool ? m_mempool_info_timer : m_shutdown_timer;
        auto& deadline = poll == Poll::Mempool ? m_mempool_deadline : m_shutdown_deadline;
        timer->stop(); deadline = {};
    };
}

void NodeModel::schedulePoll(Poll poll)
{
    if (m_workers_stopping || m_shutdown_requested || m_admission_closed) return;
    if (poll == Poll::Mempool && (!m_mempool_info_polling_active || !m_backend_queries_ready)) return;
    if (poll == Poll::Shutdown && (!m_shutdown_polling || m_fatal_exception)) return;
    const QPointer<NodeModel> observer{this};
    m_services.schedule(poll, poll == Poll::Mempool ? std::chrono::milliseconds{MEMPOOL_INFO_POLLING_INTERVAL_MS} : 200ms,
        [observer, poll] {
            if (!observer || observer->m_workers_stopping || observer->m_shutdown_requested || observer->m_admission_closed) return;
            if (poll == Poll::Mempool) {
                if (!observer->m_mempool_info_polling_active) return;
                observer->refreshMempoolInfo(); observer->refreshStatus();
            } else {
                if (!observer->m_shutdown_polling || observer->m_fatal_exception) return;
                observer->m_shutdown_query->request();
            }
            observer->schedulePoll(poll);
        });
}

void NodeModel::requestMempoolInfoRefresh()
{
    if (!m_mempool_query) {
        const QPointer<NodeModel> observer{this};
        m_mempool_query = std::make_shared<execution::SnapshotQuery<MempoolInfo>>(m_query_scheduler,
            [node = &m_node] { return MempoolInfo{int(node->getMempoolSize()), node->getMempoolDynamicUsage() / 1'000'000.0, node->getMempoolMaxUsage() / 1'000'000.0}; },
            [observer, post = m_services.execution.post] { post([observer] { if (observer) observer->publishMempoolInfo(); }); });
    }
    m_mempool_query->request();
}

void NodeModel::publishMempoolInfo()
{
    const auto result = m_mempool_query->take();
    if (!result || m_workers_stopping || m_shutdown_requested) return;
    const auto error = result->error ? tr("Mempool information could not be refreshed.") : QString{};
    if (m_mempool_error != error) { m_mempool_error = error; Q_EMIT observationErrorChanged(); }
    if (result->snapshot) applyMempoolInfo(*result->snapshot);
}

void NodeModel::applyMempoolInfo(const MempoolInfo& info)
{
    if (m_workers_stopping || m_shutdown_requested) return;
    if (info.transaction_count != m_mempool_transaction_count ||
        info.usage_mb != m_mempool_usage_mb ||
        info.max_usage_mb != m_mempool_max_usage_mb) {
        m_mempool_transaction_count = info.transaction_count;
        m_mempool_usage_mb = info.usage_mb;
        m_mempool_max_usage_mb = info.max_usage_mb;
        Q_EMIT mempoolInfoChanged();
    }
}

void NodeModel::setRemainingSyncTime(double new_progress)
{
    if (new_progress >= 1.0) {
        m_sync_progress_tracker.reset();
        if (m_remaining_sync_time != 0) {
            m_remaining_sync_time = 0;
            Q_EMIT remainingSyncTimeChanged();
        }
        return;
    }

    const std::optional<int64_t> estimate{
        m_sync_progress_tracker.addSample(m_services.monotonic_milliseconds(), new_progress)};
    if (!estimate || *estimate == m_remaining_sync_time) return;

    m_remaining_sync_time = *estimate;
    Q_EMIT remainingSyncTimeChanged();
}
void NodeModel::setVerificationProgress(double new_progress)
{
    if (new_progress != m_verification_progress) {
        setRemainingSyncTime(new_progress);

        m_verification_progress = new_progress;
        Q_EMIT verificationProgressChanged();
    }
}

void NodeModel::setNodeReady(bool ready)
{
    if (m_node_ready == ready) return;

    m_node_ready = ready;
    { std::lock_guard lock{m_notifications->mutex}; m_notifications->ready = ready; }
    updateSyncCompletion();
}

void NodeModel::setBlockSyncActive(bool active)
{
    if (m_block_sync_active == active) {
        return;
    }

    m_block_sync_active = active;
    Q_EMIT blockSyncActiveChanged();
    updateSyncCompletion();
}

void NodeModel::setHeaderSyncState(int height, int64_t block_time, bool presync)
{
    m_header_tip_height = height;
    m_header_tip_time = block_time;

    const int64_t estimate_headers_left{(m_services.wall_seconds() - block_time) / m_header_spacing};
    const bool active{height > 0 && estimate_headers_left > HEADER_HEIGHT_DELTA_SYNC};
    const double progress{active ? 1.0 * height / (height + estimate_headers_left) : 0.0};

    if (m_header_sync_active != active ||
        m_header_presync != presync ||
        m_header_sync_progress != progress) {
        m_header_sync_active = active;
        m_header_presync = presync;
        m_header_sync_progress = progress;
        Q_EMIT headerSyncChanged();
    }
    updateSyncCompletion();
}

void NodeModel::updateSyncCompletion()
{
    if (m_applying_sync_snapshot) return;
    if (m_initial_sync_complete) {
        // Ignore ordinary header-first block announcements, but resume the
        // sync presentation after falling substantially behind (e.g. sleep).
        if (int64_t{m_header_tip_height} - m_block_tip_height > HEADER_HEIGHT_DELTA_SYNC) {
            m_initial_sync_complete = false;
            Q_EMIT initialSyncCompleteChanged();
        }
        return;
    }

    if (!m_node_ready ||
        m_block_sync_active ||
        m_header_sync_active ||
        m_header_presync ||
        m_block_tip_height < m_header_tip_height) {
        return;
    }

    // After startup or renewed catch-up, stay in the sync presentation until
    // the active chain reaches the best known header, even inside the gap
    // threshold above. This avoids flickering between the two presentations.
    m_initial_sync_complete = true;
    Q_EMIT initialSyncCompleteChanged();
}

void NodeModel::setPause(bool new_pause)
{
    if (!networkActionReady()) return;
    m_network_desired_pause = new_pause;
    if (m_network_action_pending || m_pause == new_pause) return;
    m_network_action_pending = true;
    m_network_action_error.clear();
    Q_EMIT networkActionErrorChanged();
    Q_EMIT networkActionPendingChanged();
    submitNetworkAction();
}

void NodeModel::submitNetworkAction()
{
    const bool requested = m_network_desired_pause;
    const QPointer<NodeModel> observer{this};
    auto operation = std::make_shared<execution::OwnedCommand<NetworkActionResult>>(m_query_scheduler,
        [observer, post = m_services.execution.post] { post([observer] { if (observer) observer->publishNetworkAction(); }); });
    m_network_command = operation;
    const auto admission = operation->start([node = &m_node, state = m_notifications, requested] {
        node->setNetworkActive(!requested);
        uint64_t generation;
        { std::lock_guard lock{state->mutex}; generation = state->network_generation; }
        return NetworkActionResult{!node->getNetworkActive(), generation, requested};
    });
    if (!admission) {
        m_network_command.reset(); m_network_action_pending = false;
        m_network_action_error = tr("The network action could not be started.");
        Q_EMIT networkActionPendingChanged(); Q_EMIT networkActionErrorChanged();
    }
}

void NodeModel::publishNetworkAction()
{
    if (!m_network_command) return;
    const auto result = m_network_command->result();
    if (!result) return;
    m_network_command.reset();
    if (m_shutdown_requested || m_workers_stopping || m_admission_closed) return;
    if (!result->value) {
        m_network_action_pending = false;
        m_network_action_error = tr("The network action has no confirmed outcome. Refresh node information before retrying.");
        Q_EMIT networkActionPendingChanged(); Q_EMIT networkActionErrorChanged(); refreshStatus(); return;
    }
    const auto& outcome = *result->value;
    bool current;
    { std::lock_guard lock{m_notifications->mutex}; current = outcome.generation == m_notifications->network_generation; }
    if (current) {
        m_network_known = true;
        if (m_pause != outcome.paused) { m_pause = outcome.paused; Q_EMIT pauseChanged(m_pause); }
    } else { publishNotifications(); refreshStatus(); }
    if (m_network_desired_pause != outcome.requested) { submitNetworkAction(); return; }
    m_network_action_pending = false;
    if (current && outcome.paused != outcome.requested) m_network_action_error = tr("Unable to change network activity.");
    Q_EMIT networkActionPendingChanged(); Q_EMIT networkActionErrorChanged(); Q_EMIT nodeInformationChanged();
}

void NodeModel::setErrorState(bool faulted)
{
    if (m_faulted != faulted) {
        m_faulted = faulted;
        Q_EMIT errorStateChanged(faulted);
    }
}

void NodeModel::setStartupError(const QString& error)
{
    if (m_startup_error != error) {
        m_startup_error = error;
        Q_EMIT startupErrorChanged();
    }
}

void NodeModel::addStartupWarnings(const QStringList& warnings)
{
    if (!m_startup_diagnostics->append(warnings, true)) {
        if (m_services.diagnostic_loss) *m_services.diagnostic_loss = true;
        m_status_error = tr("Startup warnings could not be retained."); Q_EMIT observationErrorChanged();
    }
}

void NodeModel::addStartupWarnings(std::shared_ptr<const startupdiagnostics::Collection> warnings)
{
    if (!m_startup_diagnostics->adopt(std::move(warnings))) {
        if (m_services.diagnostic_loss) *m_services.diagnostic_loss = true;
        m_status_error = tr("Startup warnings could not be retained."); Q_EMIT observationErrorChanged();
    }
}

void NodeModel::showStartupWarnings()
{
    if (m_startup_warnings_shown || !m_startup_diagnostics->hasWarnings()) return;
    m_startup_warnings_shown = true;
    showRuntimeDialogOnGuiThread(m_startup_diagnostics->summary(), CClientUIInterface::ICON_WARNING,
        /*question=*/false, /*startup_summary=*/true);
}

void NodeModel::recordStartupErrorMessage(const QString& message)
{
    if (!m_startup_diagnostics->append(message, false)) {
        if (m_services.diagnostic_loss) *m_services.diagnostic_loss = true;
        m_status_error = tr("A startup error could not be retained."); Q_EMIT observationErrorChanged();
    }
}

void NodeModel::recordStartupWarningMessage(const QString& message)
{
    if (!m_startup_diagnostics->append(message, true)) {
        if (m_services.diagnostic_loss) *m_services.diagnostic_loss = true;
        m_status_error = tr("A startup warning could not be retained."); Q_EMIT observationErrorChanged();
    }
}

void NodeModel::startNodeInitializionThread()
{
    if (m_initialization_requested) {
        return;
    }
    m_initialization_requested = true;
    if (m_subscriptions_ready) { m_initialization_dispatched = true; Q_EMIT requestedInitialize(); }
}

void NodeModel::requestShutdown()
{
    if (m_fatal_exception) {
        std::_Exit(EXIT_FAILURE);
    }
    if (m_shutdown_requested) {
        return;
    }
    m_shutdown_requested = true;
    detachNotifications();
    Q_EMIT networkActionReadyChanged();
    clearRuntimeDialogs(true);
    stopShutdownPolling();
    Q_EMIT requestedShutdown();
}

void NodeModel::initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info, bool initial_block_download, bool shutdown_requested)
{
    if (m_workers_stopping || m_fatal_exception || m_initialization_result) return;
    publishNotifications();
    if (m_startup_diagnostics_sealed || (success && (m_shutdown_requested || shutdown_requested)) ||
        (!success && m_startup_failure_dialog_shown)) {
        finishInitialization(success, tip_info, initial_block_download, shutdown_requested); return;
    }
    m_startup_diagnostics_sealed = true;
    m_initialization_result = InitializationResult{success, tip_info, initial_block_download, shutdown_requested};
    if (success) m_runtime_dialogs_enabled = true;
    const QPointer<NodeModel> observer{this};
    const bool admitted = m_startup_diagnostics->project(success, tr("Node initialization failed."), tr("Startup warnings:"));
    if (observer && !admitted) finishStartupDiagnostics();
}

void NodeModel::finishStartupDiagnostics()
{
    if (m_workers_stopping && !m_fatal_exception) return;
    const QPointer<NodeModel> observer{this};
    if (m_initialization_result) {
        const auto result = *std::exchange(m_initialization_result, {});
        if (!m_startup_diagnostics->error().isEmpty()) {
            if (m_services.diagnostic_loss) *m_services.diagnostic_loss = true;
            const auto error = m_startup_diagnostics->error();
            setErrorState(true); if (!observer) return;
            setStartupError(error); if (observer) Q_EMIT nodeInitialized(); return;
        }
        finishInitialization(result.success, result.tip, result.initial_block_download, result.shutdown_requested);
    } else if (m_fatal_exception) {
        setStartupError(m_startup_diagnostics->error().isEmpty() ? m_startup_diagnostics->summary() : m_startup_diagnostics->error());
    }
}

void NodeModel::finishInitialization(bool success, interfaces::BlockAndHeaderTipInfo tip_info, bool initial_block_download, bool shutdown_requested)
{
    if (m_workers_stopping || m_fatal_exception) return;
    // Consume startup notifications before changing their presentation phase.
    publishNotifications();
    if (success && (m_shutdown_requested || shutdown_requested)) {
        requestShutdown();
        return;
    }

    if (!success) {
        if (m_startup_failure_dialog_shown) {
            requestShutdown();
            Q_EMIT nodeInitialized();
            return;
        }
        setErrorState(true);
        refreshStatus();
        setStartupError(m_startup_diagnostics->summary());
        if (shutdown_requested) {
            requestShutdown();
        }
    } else {
        m_backend_queries_ready = true;
        Q_EMIT networkActionReadyChanged();
        if (m_mempool_info_polling_active) {
            schedulePoll(Poll::Mempool);
        }
        m_runtime_dialogs_enabled = true;
        showStartupWarnings();
        {
            const QScopedValueRollback applying{m_applying_sync_snapshot, true};
            publishNotifications();
            if (!m_seen_block_tip) {
                m_block_tip_time = tip_info.block_time;
                setBlockTipHeight(tip_info.block_height);
                setVerificationProgress(tip_info.verification_progress);
                setBlockSyncActive(initial_block_download);
            }
            // LoadChainTip emits an INIT_DOWNLOAD notification even on a caught-up
            // restart. Core's IBD flag only latches false: preserve a newer POST_INIT
            // notification, but let this final startup result clear the init flag.
            if (!initial_block_download) setBlockSyncActive(false);
            if (!m_seen_header_tip) setHeaderSyncState(tip_info.header_height, tip_info.header_time, /*presync=*/false);
            setNodeReady(true);
        }
        updateSyncCompletion();
        refreshStatus();
        refreshMempoolInfo();
        Q_EMIT chainStateReady();
    }
    Q_EMIT nodeInitialized();
}

void NodeModel::handleRunawayException(const QString& message)
{
    handleLifecycleException(message, {});
}

void NodeModel::handleLifecycleException(const QString& message, std::shared_ptr<execution::Scheduler> original_diagnostic_scope)
{
    stopShutdownPolling();
    setMempoolInfoPollingActive(false);
    m_backend_queries_ready = false;
    m_shutdown_requested = true;
    if (!m_fatal_exception) {
        m_fatal_exception = true;
        Q_EMIT fatalExceptionChanged();
    }
    setErrorState(true);
    m_initialization_result.reset();
    const bool retained = m_startup_diagnostics->resetInput(std::move(original_diagnostic_scope)) && m_startup_diagnostics->append(message, false);
    if (!retained && m_services.diagnostic_loss) *m_services.diagnostic_loss = true;
    const auto prefix = tr("A fatal error occurred. %1 can no longer continue safely and will quit.").arg(CLIENT_NAME);
    const auto fallback = retained ? QString{} : tr("The complete fatal diagnostic exceeds available capacity. Check the node log for details.");
    if (!m_startup_diagnostics->project(false, fallback, {}, prefix, /*normalize=*/false)) {
        if (m_services.diagnostic_loss) *m_services.diagnostic_loss = true;
        setStartupError(prefix + QStringLiteral("\n\n") + m_startup_diagnostics->error());
    }
}

void NodeModel::startShutdownPolling()
{
    if (m_fatal_exception || m_shutdown_requested || m_workers_stopping || m_shutdown_polling) return;
    if (!m_shutdown_query) {
        const QPointer<NodeModel> observer{this};
        m_shutdown_query = std::make_shared<execution::SnapshotQuery<ShutdownSnapshot>>(m_query_scheduler,
            [node = &m_node] { return ShutdownSnapshot{node->shutdownRequested()}; },
            [observer, post = m_services.execution.post] { post([observer] { if (observer) observer->publishShutdownStatus(); }); }, execution::Lane::Control);
    }
    m_shutdown_polling = true; schedulePoll(Poll::Shutdown);
}

void NodeModel::stopShutdownPolling()
{
    m_shutdown_polling = false;
    if (m_services.cancel) m_services.cancel(Poll::Shutdown);
}

void NodeModel::publishShutdownStatus()
{
    const auto result = m_shutdown_query->take();
    if (!result || m_workers_stopping || m_shutdown_requested) return;
    const auto error = result->error ? tr("The node shutdown state could not be checked.") : QString{};
    if (m_shutdown_error != error) { m_shutdown_error = error; Q_EMIT observationErrorChanged(); }
    if (result->snapshot && result->snapshot->requested && !m_fatal_exception) requestShutdown();
}

void NodeModel::initializeSubscriptions()
{
    const auto key = m_query_scheduler->allocateKey();
    const auto subscriptions = m_subscriptions;
    const auto cleanup = execution::Submit(*m_query_scheduler, [subscriptions] {
        for (const auto& handler : subscriptions->handlers) if (handler) handler->disconnect();
        subscriptions->handlers.clear();
    }, [] {}, {}, {execution::Lane::Cleanup, 1024, key}, true);
    if (!cleanup) {
        std::lock_guard lock{m_notifications->mutex};
        m_notifications->subscribed = false;
        m_notifications->subscription_error = tr("Node notification cleanup capacity is unavailable.");
        m_notifications->schedule(); return;
    }
    auto lifetime = m_query_scheduler->borrow(key);
    auto registration = m_query_scheduler->borrow(key);
    if (!lifetime || !registration) {
        m_query_scheduler->activateCleanup(cleanup.id);
        std::lock_guard lock{m_notifications->mutex}; m_notifications->subscribed = false;
        m_notifications->subscription_error = tr("Node notification lifetime could not be established.");
        m_notifications->schedule(); return;
    }
    m_subscription_lease.emplace(std::move(*lifetime.lease));
    auto registration_lease = std::make_shared<execution::ResourceLease>(std::move(*registration.lease));
    const auto state = m_notifications;
    const auto done = [state](bool subscribed, const QString& error = {}) {
        std::lock_guard lock{state->mutex}; state->subscribed = subscribed; state->subscription_error = error; state->schedule();
    };
    const auto admission = execution::Submit(*m_query_scheduler,
        [node = &m_node, state, subscriptions, registration_lease] {
            { std::lock_guard lock{state->mutex}; state->header_spacing = std::max(int64_t{1}, int64_t{Params().GetConsensus().nPowTargetSpacing}); }
            auto& handlers = subscriptions->handlers;
            handlers.push_back(node->handleNotifyBlockTip([state](SynchronizationState sync, interfaces::BlockTip tip, double progress) {
                std::lock_guard lock{state->mutex}; state->block = Notifications::Block{sync, std::move(tip), progress}; state->schedule();
            }));
            handlers.push_back(node->handleNotifyHeaderTip([state](SynchronizationState, interfaces::BlockTip tip, bool presync) {
                std::lock_guard lock{state->mutex}; state->header = Notifications::Header{std::move(tip), presync}; state->schedule();
            }));
            handlers.push_back(node->handleNotifyNumConnectionsChanged([state](int total) {
                std::lock_guard lock{state->mutex}; state->peers = total; state->last_peer_total = total; ++state->status_generation; state->schedule();
            }));
            handlers.push_back(node->handleNotifyNetworkActiveChanged([state](bool active) {
                std::lock_guard lock{state->mutex}; state->network_active = active; ++state->network_generation; ++state->status_generation; state->schedule();
            }));
            handlers.push_back(node->handleNotifyAlertChanged([state] {
                std::lock_guard lock{state->mutex}; state->warnings = true; ++state->status_generation; state->schedule();
            }));
            const auto convert = [state](const std::string& text) {
                const bool gui = QThread::currentThread() == QCoreApplication::instance()->thread();
                if (!gui) return QString::fromStdString(text);
                QString result = QString::fromUtf8(text.data(), std::min<size_t>(text.size(), 16384));
                if (text.size() > 16384) {
                    if (state->diagnostic_loss) *state->diagnostic_loss = true;
                    result += QStringLiteral(" ") + QObject::tr("[complete diagnostic rejected: Core work ran on GUI]");
                }
                return result;
            };
            handlers.push_back(node->handleMessageBox([state, convert](const bilingual_str& message, unsigned int style) {
                state->request(convert(message.translated), style, false);
            }));
            handlers.push_back(node->handleQuestion([state, convert](const bilingual_str& message, const std::string&, unsigned int style) {
                if (QThread::currentThread() == QCoreApplication::instance()->thread()) return false;
                return state->request(convert(message.translated), style, true);
            }));
            handlers.push_back(node->handleBannedListChanged([state] {
                std::lock_guard lock{state->mutex}; state->bans = true; state->schedule();
            }));
        }, [done] { done(true); }, [done](std::exception_ptr) { done(false, QObject::tr("Node notification handlers could not be installed.")); },
        {execution::Lane::Core, 1024, key});
    // Retirement fences future work on this key. Admit the original native
    // registration first; its lease and the model lease already prevent cleanup
    // from running until installation and the original owner have finished.
    m_query_scheduler->activateCleanup(cleanup.id);
    if (!admission) done(false, tr("Node notification installation could not be scheduled."));
}

quint64 NodeModel::requestDisconnectPeer(qint64 node_id)
{
    if (node_id < 0) return 0;
    return startPeerAction([node = &m_node, node_id] {
        const bool disconnected = node->disconnectById(node_id);
        return PeerActionResult{disconnected ? PeerActionOutcome::Succeeded : PeerActionOutcome::Rejected,
            disconnected ? QString{} : tr("The peer could not be disconnected. It may already be disconnected.")};
    });
}

quint64 NodeModel::requestBanPeer(const QString& raw_address, int64_t duration)
{
    if (duration <= 0 || raw_address.isEmpty() || raw_address.size() > 512) return 0;
    return startPeerAction([node = &m_node, raw_address, duration] {
        const auto address = LookupHost(raw_address.toStdString(), false);
        if (!address) return PeerActionResult{PeerActionOutcome::Rejected, tr("The peer address is invalid.")};
        if (!node->ban(*address, duration)) return PeerActionResult{PeerActionOutcome::Rejected, tr("The peer could not be banned.")};
        try {
            // A ban that was applied remains applied even if disconnect fails.
            node->disconnectByAddress(*address);
        } catch (...) {
            return PeerActionResult{PeerActionOutcome::PartiallyApplied,
                tr("The ban was applied, but disconnecting the peer failed.")};
        }
        return PeerActionResult{PeerActionOutcome::Succeeded, {}};
    });
}

quint64 NodeModel::startPeerAction(std::function<PeerActionResult()> effect)
{
    if (!networkActionReady() || m_peer_command) return 0;
    const auto ticket = ++m_peer_ticket;
    const QPointer<NodeModel> observer{this};
    auto command = std::make_shared<execution::OwnedCommand<PeerActionResult>>(m_query_scheduler,
        [observer, ticket, post = m_services.execution.post] {
            post([observer, ticket] { if (observer) observer->publishPeerAction(ticket); });
        });
    m_peer_command = command;
    if (!command->start(std::move(effect))) { m_peer_command.reset(); return 0; }
    Q_EMIT peerActionChanged();
    return ticket;
}

void NodeModel::publishPeerAction(quint64 ticket)
{
    if (ticket != m_peer_ticket || !m_peer_command) return;
    const auto result = m_peer_command->result();
    if (!result) return;
    m_peer_command.reset();
    if (m_admission_closed || m_workers_stopping || m_shutdown_requested) return;
    const auto outcome = result->value.value_or(PeerActionResult{PeerActionOutcome::Uncertain,
        tr("The peer action failed without a confirmed outcome. Refresh the peer and ban lists before retrying.")});
    const QPointer<NodeModel> observer{this};
    Q_EMIT peerActionChanged();
    if (!observer || m_admission_closed) return;
    Q_EMIT peerActionFinished(ticket, outcome.outcome, outcome.error);
}

QVariantList NodeModel::nodeInformationRows()
{
    if (!m_backend_queries_ready || m_shutdown_requested || m_workers_stopping) return {};
    const int header_height = std::max(m_header_tip_height, m_information_header_height);
    const int64_t header_time = m_header_tip_height >= m_information_header_height
        ? m_header_tip_time : m_information_header_time;
    const QString local_addresses = m_information_local_addresses.isEmpty() ? tr("None") : m_information_local_addresses;
    const int block_height = std::max(m_block_tip_height, m_information_block_height);
    const int64_t last_block_time = m_information_block_time;
    const QString warning_text{m_warning_list.empty() ? tr("None") : m_warning_list.join(QStringLiteral("\n"))};

    QVariantList rows;
    rows.push_back(InformationRow(tr("Client version"), fullClientVersion()));
    rows.push_back(InformationRow(tr("User agent"), m_information_user_agent));
    rows.push_back(InformationRow(tr("Datadir"), m_information_data_dir));
    rows.push_back(InformationRow(tr("Blocks dir"), m_information_blocks_dir));
    rows.push_back(InformationRow(tr("Startup time"), m_information_startup_time));
    rows.push_back(InformationRow(tr("Network"), m_information_network));
    rows.push_back(InformationRow(tr("Block height"), QString::number(block_height)));
    rows.push_back(InformationRow(tr("Header height"), QString::number(header_height)));
    rows.push_back(InformationRow(tr("Header time"), header_time > 0 ? QDateTime::fromSecsSinceEpoch(header_time).toString() : tr("Unknown")));
    rows.push_back(InformationRow(tr("Last block time"), last_block_time > 0 ? QDateTime::fromSecsSinceEpoch(last_block_time).toString() : tr("Unknown")));
    rows.push_back(InformationRow(tr("Verification progress"), QStringLiteral("%1%").arg(QString::number(m_verification_progress * 100.0, 'f', 2))));
    rows.push_back(InformationRow(tr("Peers"), tr("%1 total (%2 inbound, %3 outbound)").arg(m_num_peers).arg(m_num_inbound_peers).arg(m_num_outbound_peers)));
    rows.push_back(InformationRow(tr("Network active"), m_network_known ? (!m_pause ? tr("Yes") : tr("No")) : tr("Unknown")));
    rows.push_back(InformationRow(tr("Local addresses"), local_addresses));
    rows.push_back(InformationRow(tr("Warnings"), warning_text));
    return rows;
}

bool NodeModel::showRuntimeDialogOnGuiThread(const QString& message, unsigned int style, bool question, bool startup_summary)
{
    if (m_shutdown_requested || m_workers_stopping) return false;
    if (!m_runtime_dialogs_enabled && !question) {
        if (style & CClientUIInterface::ICON_WARNING) {
            recordStartupWarningMessage(message);
            return false;
        }
        if (!(style & CClientUIInterface::MODAL)) {
            if (style & CClientUIInterface::ICON_ERROR) {
                recordStartupErrorMessage(message);
            }
            return false;
        }
    }

    if (m_runtime_dialog_active && m_runtime_dialog_queue.size() >= 31) {
        m_status_error = tr("A node dialog could not be shown because the dialog queue is full.");
        Q_EMIT observationErrorChanged(); return false;
    }
    auto request{std::make_shared<RuntimeDialogRequest>()};
    request->message = message.left(16384);
    request->full_message = startupdiagnostics::RetainText(m_query_scheduler, m_dialog_text_budget, message);
    if (!request->full_message) {
        if (m_services.diagnostic_loss) *m_services.diagnostic_loss = true;
        request->message += QStringLiteral(" ") + tr("[complete message unavailable: diagnostic capacity exhausted]");
    }
    request->startup_summary = startup_summary;
    request->style = style;
    request->question = question;
    if (!m_runtime_dialogs_enabled && (question || (style & CClientUIInterface::ICON_ERROR))) {
        m_startup_failure_dialog_shown = true;
    }

    if (m_runtime_dialog_active) m_runtime_dialog_queue.push_back(request);
    else showRuntimeDialogRequest(request);
    return false; // Presentation never waits for an answer on GUI.

}

void NodeModel::showRuntimeDialogRequest(const std::shared_ptr<RuntimeDialogRequest>& request)
{
    if (m_runtime_diagnostics && m_runtime_diagnostics != m_startup_diagnostics) {
        m_runtime_diagnostics->closeAdmission(); m_runtime_diagnostics->deleteLater();
    }
    if (request->startup_summary) m_runtime_diagnostics = m_startup_diagnostics;
    else {
        m_runtime_diagnostics = new StartupDiagnosticsModel({m_query_scheduler, m_services.execution.post}, this);
        m_runtime_diagnostics->append(request->full_message ? *request->full_message : request->message, false);
        if (!m_runtime_diagnostics->project(false, {}, {}, {}, /*normalize=*/false)) {
            if (m_services.diagnostic_loss) *m_services.diagnostic_loss = true;
            request->message += QStringLiteral(" ") + tr("[complete diagnostics unavailable: capacity exhausted]");
        }
        const QPointer<NodeModel> observer{this};
        const QPointer<StartupDiagnosticsModel> details{m_runtime_diagnostics};
        connect(m_runtime_diagnostics, &StartupDiagnosticsModel::finished, this,
            [observer, details, original = std::weak_ptr<RuntimeDialogRequest>{request}] {
            if (!observer || !details || observer->m_runtime_dialog_active != original.lock() || details->error().isEmpty()) return;
            if (observer->m_services.diagnostic_loss) *observer->m_services.diagnostic_loss = true;
            observer->m_runtime_dialog_message += QStringLiteral(" ") + QObject::tr("[complete diagnostics unavailable]");
            Q_EMIT observer->runtimeDialogChanged();
        });
    }
    m_runtime_dialog_active = request;
    m_runtime_dialog_title = RuntimeDialogTitle(request->style);
    m_runtime_dialog_message = request->message;
    m_runtime_dialog_icon = RuntimeDialogIcon(request->style);
    m_runtime_dialog_buttons = RuntimeDialogButtons(request->style);
    m_runtime_dialog_question = request->question || HasMultipleRuntimeDialogButtons(m_runtime_dialog_buttons);
    m_runtime_dialog_visible = true;
    Q_EMIT runtimeDialogChanged();
}

void NodeModel::clearRuntimeDialogs(bool notify)
{
    // The queue is capped32. Only the necessary answer handoff runs on GUI;
    // retained full message and prepared row storage retire on Cleanup.
    const auto reject = [](const std::shared_ptr<RuntimeDialogRequest>& request) {
        if (!request) return;
        { std::lock_guard lock{request->mutex}; request->answered = true; request->answer = false; request->button = CClientUIInterface::BTN_CANCEL; }
        request->condition.notify_all();
    };
    reject(m_runtime_dialog_active);
    for (const auto& request : m_runtime_dialog_queue) reject(request);
    m_runtime_dialog_active.reset(); m_runtime_dialog_queue.clear();
    if (m_runtime_diagnostics && m_runtime_diagnostics != m_startup_diagnostics) {
        m_runtime_diagnostics->closeAdmission(); m_runtime_diagnostics->deleteLater();
    }
    m_runtime_diagnostics = nullptr;
    const bool visible = std::exchange(m_runtime_dialog_visible, false);
    if (notify && visible) Q_EMIT runtimeDialogChanged();
}

void NodeModel::answerRuntimeDialog(unsigned int button)
{
    if (!m_runtime_dialog_active) {
        return;
    }

    auto answered_dialog{std::move(m_runtime_dialog_active)};
    {
        std::lock_guard lock{answered_dialog->mutex};
        answered_dialog->button = button;
        answered_dialog->answer = button == CClientUIInterface::BTN_OK;
        answered_dialog->answered = true;
    }
    answered_dialog->condition.notify_all();

    if (!m_runtime_dialog_queue.empty()) {
        auto next_dialog{m_runtime_dialog_queue.front()};
        m_runtime_dialog_queue.pop_front();
        showRuntimeDialogRequest(next_dialog);
        return;
    }

    if (m_runtime_diagnostics && m_runtime_diagnostics != m_startup_diagnostics) {
        m_runtime_diagnostics->closeAdmission(); m_runtime_diagnostics->deleteLater();
    }
    m_runtime_diagnostics = nullptr;
    m_runtime_dialog_visible = false;
    Q_EMIT runtimeDialogChanged();
}

#ifdef ENABLE_TEST_AUTOMATION
void NodeModel::showRuntimeDialogForTest(const QString& message, unsigned int style, bool question)
{
    auto request{std::make_shared<RuntimeDialogRequest>()};
    request->message = message;
    request->style = style;
    request->question = question;

    if (m_runtime_dialog_active) {
        m_runtime_dialog_queue.push_back(request);
    } else {
        showRuntimeDialogRequest(request);
    }
}
#endif
