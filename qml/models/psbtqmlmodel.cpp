// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/psbtqmlmodel.h>
#include <qml/backendexecutor.h>
#include <qml/util.h>
#include <qml/ownedwork.h>
#include <qml/guipresentation.h>
#include <limits>
#include <QClipboard>
#include <QGuiApplication>
#include <QPointer>
#include <QThread>
#include <algorithm>

QString PsbtQmlModel::LocalFilePath(const QString& path) { return psbtwork::LocalFilePath(path); }
QString PsbtQmlModel::LoadPsbtFromFile(const QString& path, PartiallySignedTransaction& document) { return psbtwork::LoadFile(path, document); }
QByteArray PsbtQmlModel::SerializePsbtRaw(const PartiallySignedTransaction& document) { return psbtwork::SerializeRaw(document); }
QString PsbtQmlModel::SavePsbtToFile(const PartiallySignedTransaction& document, const QString& path) { return psbtwork::SaveFile(document, path); }
QString PsbtQmlModel::PsbtErrorText(common::PSBTError error) { return psbtwork::ErrorText(error); }
bool PsbtQmlModel::IsMultisigPsbtInput(const PartiallySignedTransaction& document, size_t index) { return psbtwork::Multisig(document, index).has_value(); }
std::optional<std::pair<int, int>> PsbtQmlModel::MultisigPsbtInputSigInfo(const PartiallySignedTransaction& document, size_t index) { return psbtwork::Multisig(document, index); }

PsbtQmlModel::PsbtQmlModel(interfaces::Wallet* wallet, interfaces::Node* node, QObject* parent)
    : QObject(parent), m_node(node), m_wallet_expected(wallet != nullptr) {}

PsbtQmlModel::~PsbtQmlModel()
{
    closeAdmission();
    releaseSnapshot();
    // The reserved Cleanup owner still retains each structural result when its
    // GUI alias is released; activation happens only after the GUI reference.
    m_active.reset();
    m_active_lease.reset();
    // BackendExecutor owns its asynchronous native drain after GUI teardown.
    // Never enter a nested GUI loop or wait for Core/device work here.
    if (m_owned_executor) m_owned_executor->shutdown();
}

void PsbtQmlModel::configureSession(std::shared_ptr<walletopening::Session> opening, psbtfee::Services services)
{
    Q_ASSERT(!m_active && !m_snapshot);
    m_opening = std::move(opening);
    if (m_opening && !services.scheduler) services.scheduler = m_opening->scheduler();
    setFeeServices(std::move(services));
}
void PsbtQmlModel::setFeeServices(psbtfee::Services services)
{
    // In-flight work retains its original services and Native lifetime.
    m_services = std::move(services);
    m_services.post=presentation::ReliablePost(std::move(m_services.post));
    if (!m_services.scheduler && m_opening) m_services.scheduler = m_opening->scheduler();
}
void PsbtQmlModel::ensureServices()
{
    if (!m_services.scheduler) {
        m_owned_executor = std::make_unique<BackendExecutor>(execution::Limits::Bounded());
        m_services.scheduler = m_owned_executor->scheduler();
    }
    if (!m_services.post) {
        m_bridge = presentation::MakeGuiObject();
        m_services.post = [bridge = m_bridge](std::function<void()> turn) {
            QMetaObject::invokeMethod(bridge.get(), std::move(turn), Qt::QueuedConnection);
        };
    }
}
void PsbtQmlModel::setNode(interfaces::Node* node)
{
    if (m_node == node) return;
    m_node = node;
    ++m_generation; // Late results from the old public Node context cannot publish.
    if (!busy() && m_snapshot && !m_closed) refreshState();
}
void PsbtQmlModel::closeAdmission()
{
    m_closed = true;
    m_phase = Phase::Closed;
    // Closing GUI delivery cannot hold the service drain hostage. Native work
    // retains its own operation and bounded scalar receipt through its outcome.
    releaseSnapshot();
    m_active.reset();
    m_active_lease.reset();
}
void PsbtQmlModel::detachWallet()
{
    closeAdmission();
    ++m_generation;
    Q_EMIT changed();
}
void PsbtQmlModel::releaseSnapshot()
{
    m_snapshot.reset();
    m_summary_window.clear();
    m_snapshot_lease.reset();
}
void PsbtQmlModel::clear()
{
    ++m_generation;
    releaseSnapshot();
    m_matched_txid.clear();
    m_error.clear(); m_status.clear();
    if (!busy()) m_phase = m_closed ? Phase::Closed : Phase::Idle;
    Q_EMIT changed();
}
void PsbtQmlModel::setError(const QString& error)
{
    ++m_generation;
    releaseSnapshot();
    m_matched_txid.clear();
    m_status.clear();
    m_error = error;
    m_phase = m_closed ? Phase::Closed : Phase::Failed;
    Q_EMIT changed();
}
void PsbtQmlModel::setMatchedTxid(const QString& txid) { m_matched_txid = txid; Q_EMIT changed(); }
QString PsbtQmlModel::error() const {
    if (m_closed && m_receipt) { std::lock_guard lock{m_receipt->mutex}; if (m_receipt->ready) return m_receipt->error; }
    return m_error;
}
bool PsbtQmlModel::submitted() const { if (!m_submission_receipt) return m_submitted; std::lock_guard lock{m_submission_receipt->mutex}; return m_submission_receipt->ready && m_submission_receipt->submitted; }
bool PsbtQmlModel::saved() const { if (!m_save_receipt) return m_saved; std::lock_guard lock{m_save_receipt->mutex}; return m_save_receipt->ready && m_save_receipt->saved; }
QString PsbtQmlModel::savedPath() const { if (!m_save_receipt) return m_saved_path; std::lock_guard lock{m_save_receipt->mutex}; return m_save_receipt->ready && m_save_receipt->saved ? m_save_receipt->path : m_saved_path; }
QString PsbtQmlModel::submittedTxid() const { if (!m_submission_receipt) return m_submitted_txid; std::lock_guard lock{m_submission_receipt->mutex}; return m_submission_receipt->ready ? m_submission_receipt->txid : m_submitted_txid; }
void PsbtQmlModel::updateSummaryWindow()
{
    m_summary_window = m_snapshot ? m_snapshot->summary.mid(m_summary_offset, m_summary_size) : QStringList{};
}
void PsbtQmlModel::requestSummaryWindow(int offset, int count)
{
    if (offset < 0 || count < 1) return;
    m_summary_offset = offset;
    m_summary_size = std::min(count, 128);
    updateSummaryWindow();
    Q_EMIT changed();
}
quint64 PsbtQmlModel::requestLoadFromFile(const QString& path)
{
    if (busy()) {
        if (m_active_kind != psbtwork::Kind::Load && m_active_kind != psbtwork::Kind::Inspect) return 0;
        // A replaceable read may finish on its own original inputs. Its result
        // is retired on Native/Domain and cannot publish into the new document.
        m_active.reset(); m_active_lease.reset();
    }
    ++m_generation;
    releaseSnapshot();
    psbtwork::Request request; request.kind = psbtwork::Kind::Load; request.path = path;
    return start(std::move(request));
}
quint64 PsbtQmlModel::requestSaveToFile(const QString& path)
{
    psbtwork::Request request; request.kind = psbtwork::Kind::Save; request.path = path;
    return start(std::move(request));
}
quint64 PsbtQmlModel::requestSign() { psbtwork::Request request; request.kind = psbtwork::Kind::Sign; return start(std::move(request)); }
quint64 PsbtQmlModel::requestSignWithPassphrase(const QString& passphrase)
{
    if(busy()||m_closed||!canSign())return 0;
    ensureServices();
    const auto captured=secretinput::Capture(m_services.scheduler,passphrase);
    if(!captured){m_phase=Phase::Failed;m_error=tr("The signing password could not be admitted for background execution.");Q_EMIT changed();return 0;}
    psbtwork::Request request;request.kind=psbtwork::Kind::Sign;request.captured_passphrase=captured.input;
    return start(std::move(request));
}
quint64 PsbtQmlModel::requestBroadcast() { psbtwork::Request request; request.kind = psbtwork::Kind::Broadcast; return start(std::move(request)); }
quint64 PsbtQmlModel::requestCopyToClipboard() { psbtwork::Request request; request.kind = psbtwork::Kind::Clipboard; return start(std::move(request)); }
void PsbtQmlModel::refreshState(const QString& status)
{
    if (!m_snapshot || busy() || m_closed) return;
    psbtwork::Request request; request.status = status;
    start(std::move(request));
}

quint64 PsbtQmlModel::start(psbtwork::Request request)
{
    if (busy() || m_closed || (m_opening && !m_opening->available())) return 0;
    if (request.kind == psbtwork::Kind::Sign && !canSign()) return 0;
    if (request.kind == psbtwork::Kind::Broadcast && !canBroadcast()) return 0;
    if (request.kind != psbtwork::Kind::Load && !loaded()) return 0;
    const auto ticket = ++m_operation_id;
    if (m_wallet_expected && !m_opening) {
        m_phase = Phase::Failed; m_error = tr("The wallet session is unavailable."); Q_EMIT changed(); return ticket;
    }
    ensureServices();
    if (m_snapshot) {
        request.document = m_snapshot->document;
        request.proof = m_snapshot->proof;
        request.approved_fee = m_snapshot->proof ? m_snapshot->proof->amount : std::nullopt;
    }
    const auto generation = m_generation;
    const auto text_bytes=execution::Utf16PayloadBytes({size_t(request.path.size()),size_t(request.status.size())});
    const auto secret_bytes=secretinput::Bytes(request.captured_passphrase);
    if(text_bytes>std::numeric_limits<size_t>::max()-4096||secret_bytes>std::numeric_limits<size_t>::max()-4096-text_bytes){
        m_phase=Phase::Failed;m_error=tr("The PSBT action input cannot fit the transport budget.");Q_EMIT changed();return ticket;
    }
    const auto capture_bytes=4096+text_bytes+secret_bytes;
    const QPointer<PsbtQmlModel> observer{this};
    auto receipt = std::make_shared<Receipt>();
    auto operation_ref = std::make_shared<std::weak_ptr<Operation>>();
    auto operation = std::make_shared<Operation>(m_services.scheduler, [post = m_services.post, observer, ticket, generation, receipt, operation_ref] {
        if (const auto operation = operation_ref->lock()) if (const auto result = operation->result()) {
            if (result->value) receipt->record(*result->value);
            else {
                psbtwork::Result failed;
                failed.code = psbtwork::Error::Uncertain;
                failed.error = tr("The PSBT action ended without a confirmed outcome. Reconcile before retrying.");
                receipt->record(failed);
            }
        }
        post([observer, ticket, generation] { if (observer) observer->publish(ticket, generation); });
    });
    *operation_ref = operation;
    m_receipt = receipt;
    if (request.kind == psbtwork::Kind::Broadcast) m_submission_receipt = receipt;
    if (request.kind == psbtwork::Kind::Save) m_save_receipt = receipt;
    const bool structural = request.kind == psbtwork::Kind::Load || request.kind == psbtwork::Kind::Inspect || request.kind == psbtwork::Kind::Sign;
    if (structural) {
        // Reserve native value release before any structural result can reach
        // GUI. Rejection retains no PSBT and is reported as a real failure.
        auto owner = std::make_shared<std::shared_ptr<Operation>>(operation);
        const auto key = m_services.scheduler->allocateKey();
        const auto cleanup = execution::Submit(*m_services.scheduler, [owner] { owner->reset(); }, [] {}, {},
            {execution::Lane::Cleanup, 256, key, execution::WorkClass::Action}, true);
        if (!cleanup) { m_phase = Phase::Failed; m_error = tr("PSBT result capacity is unavailable. Close an unused document and retry."); Q_EMIT changed(); return ticket; }
        auto held = m_services.scheduler->borrow(key);
        m_services.scheduler->activateCleanup(cleanup.id);
        if (!held) { m_phase = Phase::Failed; m_error = tr("PSBT result capacity is unavailable."); Q_EMIT changed(); return ticket; }
        m_active_lease.emplace(std::move(*held.lease));
    }
    m_active = operation; m_active_generation = generation; m_active_kind = request.kind;
    psbtwork::Context context{m_services.lookup ? m_services.lookup : psbtfee::NodeLookup(m_node), psbtwork::NodeBroadcast(m_node)};
    auto input = std::make_shared<psbtwork::Request>(std::move(request));
    execution::Admission admission;
    if (m_opening) {
        const bool observation = input->kind == psbtwork::Kind::Load || input->kind == psbtwork::Kind::Inspect;
        const auto action = observation ? nullptr : m_opening->startAction();
        if (observation) admission = operation->startWith([opening = m_opening, input, context,capture_bytes](auto done, auto failed) {
            return opening->read([input, context](interfaces::Wallet& wallet) {
                return psbtwork::Execute(&wallet, std::move(*input), context);
            }, std::move(done), std::move(failed),execution::WorkClass::Observation,capture_bytes);
        });
        else if (action) admission = operation->startWith([opening = m_opening, action, input, context,capture_bytes](auto done, auto failed) {
            return opening->perform(action, [input, context](interfaces::Wallet& wallet) {
                return psbtwork::Execute(&wallet, std::move(*input), context);
            }, std::move(done), std::move(failed),capture_bytes);
        });
        else admission = {{}, execution::Rejection::Closed};
    } else admission = operation->start([input, context] { return psbtwork::Execute(nullptr, std::move(*input), context); },{execution::Lane::Core,capture_bytes});
    if (!admission) {
        m_active.reset(); m_active_lease.reset();
        m_phase = Phase::Failed; m_error = tr("The PSBT action could not be queued."); Q_EMIT changed(); return ticket;
    }
    m_error.clear(); m_phase = Phase::Pending;
    Q_EMIT changed();
    return ticket;
}

void PsbtQmlModel::retryPublication()
{
    if (m_active) publish(m_operation_id, m_active_generation);
}
void PsbtQmlModel::publish(quint64 ticket, quint64 generation)
{
    if (!m_active || ticket != m_operation_id) return;
    auto outcome = m_active->result();
    if (!outcome) return;
    QString clipboard;
    if (outcome->error || !outcome->value) {
        bool not_started{false};
        if (outcome->error) {
            try { std::rethrow_exception(outcome->error); }
            catch (const execution::NotStarted&) { not_started = true; }
            catch (...) {}
        }
        m_phase = m_closed ? Phase::Closed : not_started ? Phase::Failed : Phase::Uncertain;
        m_error = not_started ? tr("The PSBT action did not start.") : tr("The PSBT action ended without a confirmed outcome. Reconcile before retrying.");
        if (!not_started && m_active_kind == psbtwork::Kind::Broadcast) m_broadcast_blocked = true;
        if (!not_started && m_active_kind == psbtwork::Kind::Sign) m_signing_blocked = true;
    } else {
        const auto& result = *outcome->value;
        if (result.submitted) m_submitted = true;
        if (result.saved) m_saved = true;
        if (result.submitted || result.code == psbtwork::Error::Uncertain) {
            m_submitted_txid = result.txid;
            if (m_active_kind == psbtwork::Kind::Broadcast && generation == m_generation) m_broadcast_blocked = true;
        }
        if ((result.code == psbtwork::Error::Uncertain || result.code == psbtwork::Error::Relock) && m_active_kind == psbtwork::Kind::Sign) m_signing_blocked = true;
        if (result.saved) m_saved_path = result.path;
        if (!m_closed && generation == m_generation) {
            if (result.snapshot) {
                releaseSnapshot();
                m_snapshot = result.snapshot;
                if (m_active_lease) { m_snapshot_lease.emplace(std::move(*m_active_lease)); m_active_lease.reset(); }
                m_summary_offset = 0; updateSummaryWindow();
                m_status = result.snapshot->status; m_error = result.snapshot->error;
                if (m_active_kind == psbtwork::Kind::Load) { m_broadcast_blocked = false; m_signing_blocked = false; }
            }
            if (!result.error.isEmpty()) { m_error = result.error; m_status = result.error; }
            if (result.submitted) m_status = tr("Transaction accepted by the local node. Transaction ID: %1").arg(result.txid);
            else if (result.saved) m_status = tr("PSBT saved.");
            else if (m_active_kind == psbtwork::Kind::Clipboard && result.code == psbtwork::Error::None) {
                clipboard = result.clipboard;
                m_status = tr("PSBT copied to clipboard.");
            }
            m_phase = result.code == psbtwork::Error::None ? Phase::Ready
                : result.code == psbtwork::Error::NeedsUnlock || result.code == psbtwork::Error::IncorrectPassphrase ? Phase::NeedsUnlock
                : result.code == psbtwork::Error::Uncertain || result.code == psbtwork::Error::Relock ? Phase::Uncertain : Phase::Failed;
        } else m_phase = m_closed ? Phase::Closed : Phase::Stale;
    }
    // Release GUI aliases before activating native structural teardown.
    outcome.reset(); m_active.reset(); m_active_lease.reset();
    const QPointer<PsbtQmlModel> observer{this};
    if (!clipboard.isEmpty()) QGuiApplication::clipboard()->setText(clipboard);
    if (!observer) return;
    Q_EMIT changed();
    if (observer) Q_EMIT finished(ticket, generation);
}
