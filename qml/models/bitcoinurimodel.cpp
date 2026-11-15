// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bitcoinurimodel.h>

#include <bitcoin-build-config.h> // IWYU pragma: keep
#include <qml/models/bitcoinuri.h>
#ifdef ENABLE_WALLET
#include <qml/models/walletqmlmodel.h>
#endif
#include <QTimer>
#include <qml/guipresentation.h>
#include <QFile>
#include <QCryptographicHash>
#include <QPointer>
#include <QUrl>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <map>
#include <variant>

struct BitcoinUriParseWork {
    std::mutex mutex;
    QVariantMap result;
    std::atomic_bool ready{false}, cancelled{false};
};

namespace {
QVariantMap ResultMap(const BitcoinUriParseResult& r)
{
    QString note;
    if (r.has_message && !r.message.isEmpty()) {
        note = r.message;
        if (r.has_label && !r.label.isEmpty()) note += " - " + r.label;
    }
    return {{QStringLiteral("success"), r.success}, {QStringLiteral("error"), r.error.left(4096)},
        {QStringLiteral("address"), r.address}, {QStringLiteral("amountSats"), static_cast<qlonglong>(r.amount_sats)},
        {QStringLiteral("hasAmount"), r.has_amount}, {QStringLiteral("label"), r.label},
        {QStringLiteral("hasLabel"), r.has_label}, {QStringLiteral("uriMessage"), r.message},
        {QStringLiteral("hasMessage"), r.has_message}, {QStringLiteral("privateNote"), note}};
}
QVariantMap Error(const QString& error)
{
    BitcoinUriParseResult result; result.error = error; return ResultMap(result);
}
using InputResult = std::variant<QByteArray, QVariantMap>;
QVariantMap ParseText(const QString& input)
{
    constexpr qsizetype MAX_CHARS{1024 * 1024};
    qsizetype first{0};
    while (first < input.size() && input[first].isSpace()) ++first;
    const bool prefix = input.mid(first, 8).compare(QLatin1String{"bitcoin:"}, Qt::CaseInsensitive) == 0;
    auto result = input.size() > MAX_CHARS ? Error(BitcoinUriModel::tr("Payment URI is too large.")) : ResultMap(BitcoinUri::Parse(input));
    result.insert(QStringLiteral("isPaymentUri"), prefix);
    result.insert(QStringLiteral("probeSucceeded"), true);
    result.insert(QStringLiteral("sourceId"), QString::fromLatin1(QCryptographicHash::hash(
        QByteArray::fromRawData(reinterpret_cast<const char*>(input.constData()), input.size() * qsizetype{sizeof(QChar)}), QCryptographicHash::Sha256).toHex()));
    return result;
}
bool PasteProvenance(const QString& previous, const QString& current, const QString& clipboard)
{
    if (clipboard.isEmpty() || previous == current || current.size() < clipboard.size()) return false;
    const auto unchanged = current.size() - clipboard.size();
    if (unchanged > previous.size()) return false;
    qsizetype prefix{0}, suffix{0};
    while (prefix < std::min(previous.size(), current.size()) && previous[prefix] == current[prefix]) ++prefix;
    while (suffix < std::min(previous.size(), current.size()) && previous[previous.size() - suffix - 1] == current[current.size() - suffix - 1]) ++suffix;
    const auto lower = std::max<qsizetype>(0, unchanged - suffix), upper = std::min(prefix, unchanged);
    const auto found = current.indexOf(clipboard, lower);
    return lower <= upper && found >= 0 && found <= upper;
}
QVariantMap ContextResult(QVariantMap result, const QString& text, const QVariantMap& context)
{
    const auto address = context.value("address").toString(), note = context.value("note").toString();
    const auto amount = context.value("amount").toLongLong();
    const bool fixed_replaces_maximum = context.value("maximum").toBool() && result.value("hasAmount").toBool();
    const auto field = context.value("sourceField").toString();
    const auto private_note = result.value("privateNote").toString();
    result.insert(QStringLiteral("conflicts"), result.value("success").toBool() && (fixed_replaces_maximum ||
        (field != "address" && !address.isEmpty() && address != result.value("address").toString()) ||
        (field != "amount" && result.value("hasAmount").toBool() && amount != 0 && amount != result.value("amountSats").toLongLong()) ||
        (field != "label" && !note.isEmpty() && !private_note.isEmpty() && note != private_note)));
    result.insert(QStringLiteral("matchesExistingRequest"), result.value("success").toBool() && !fixed_replaces_maximum &&
        address == result.value("address").toString() && (!result.value("hasAmount").toBool() || amount == result.value("amountSats").toLongLong()) &&
        (!result.value("hasLabel").toBool() || context.value("requestLabel").toString() == result.value("label").toString()));
    if (context.value("edited").toBool()) {
        const auto current = context.value("currentText").toString();
        result.insert(QStringLiteral("pasteProvenance"), PasteProvenance(context.value("previousText").toString(), current, text));
        result.insert(QStringLiteral("containsPaymentUri"), current.contains(QLatin1String{"bitcoin:"}, Qt::CaseInsensitive));
    } else result.insert(QStringLiteral("pasteProvenance"), true);
    return result;
}
InputResult ReadFile(const QString& input)
{
    constexpr qint64 MAX_BYTES{1024 * 1024};
    const QUrl url(input);
    QFile file(url.isLocalFile() ? url.toLocalFile() : input);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return Error(BitcoinUriModel::tr("Cannot open file: %1").arg(file.errorString()));
    if (file.size() > MAX_BYTES) return Error(BitcoinUriModel::tr("File is too large to be a payment URI."));
    // Bound the read itself: the file can grow after size(), or be a stream.
    auto bytes = file.read(MAX_BYTES + 1);
    if (file.error() != QFileDevice::NoError) return Error(BitcoinUriModel::tr("Cannot read file: %1").arg(file.errorString()));
    if (bytes.size() > MAX_BYTES || !file.atEnd()) return Error(BitcoinUriModel::tr("File is too large to be a payment URI."));
    return bytes;
}
}

BitcoinUriParseOperation::BitcoinUriParseOperation(quint64 id, std::shared_ptr<const std::atomic_bool> admission, QObject* parent)
    : QObject(parent), m_admission(std::move(admission)), m_id(id) {}
BitcoinUriParseOperation::~BitcoinUriParseOperation() { closeAdmission(); }
QVariantMap BitcoinUriParseOperation::result() const
{
    if (admissionClosed() || m_pending || !m_work || !m_work->ready) return {};
    std::lock_guard lock{m_work->mutex}; return m_work->result;
}
void BitcoinUriParseOperation::closeAdmission()
{
    m_closed = true; m_pending = false;
    if (m_work) m_work->cancelled = true;
    m_work.reset();
    if (auto unregister = std::exchange(m_unregister, {})) unregister();
}
void BitcoinUriParseOperation::cancel() { closeAdmission(); deleteLater(); }
void BitcoinUriParseOperation::retryPublication()
{
    if (admissionClosed() || !m_pending || !m_work || !m_work->ready) return;
    m_pending = false;
    Q_EMIT finished();
}
// GUI-only registry; independently retained retirement turns outlive the model.
// Each turn drops at most32 original operation aliases. Native cleanup remains
// owned by the pre-reserved lease, including already completed parse results.
struct BitcoinUriOperations : std::enable_shared_from_this<BitcoinUriOperations> {
    std::map<quint64, QPointer<BitcoinUriParseOperation>> live;
    presentation::Post post;
#ifdef ENABLE_WALLET
    std::mutex import_mutex;
    std::map<uint64_t, std::shared_ptr<paymentimport::Owner>> imports;
    std::shared_ptr<QObject> timer{presentation::MakeGuiObject()};
    void retireImports() {
        std::vector<std::shared_ptr<paymentimport::Owner>> retained;
        { std::lock_guard lock{import_mutex}; for (const auto& [id, operation] : imports) retained.push_back(operation); }
        for (const auto& operation : retained) operation->close();
    }
#endif
    void retire() {
        for (size_t n{0}; n < 32 && !live.empty(); ++n) {
            const auto operation = live.begin()->second;
            live.erase(live.begin());
            if (operation) operation->closeAdmission();
        }
        if (!live.empty()) post([self = shared_from_this()] { self->retire(); });
    }
};
BitcoinUriModel::BitcoinUriModel(QObject* parent)
    : QObject(parent), m_operations(std::make_shared<BitcoinUriOperations>())
{
    m_operations->post = presentation::ReliablePost({});
}
BitcoinUriModel::~BitcoinUriModel()
{
    closeAdmission();
}
void BitcoinUriModel::setServices(execution::SnapshotServices services) {
    if (m_closed) return;
    m_services = std::move(services);
    m_operations->post = presentation::ReliablePost(m_services.post);
}
void BitcoinUriModel::closeAdmission()
{
    if (std::exchange(m_closed, true)) return;
    *m_admission = true;
#ifdef ENABLE_WALLET
    m_operations->retireImports();
#endif
    m_operations->retire();
}
#ifdef ENABLE_WALLET
PaymentImportOperation* BitcoinUriModel::requestImportText(const QString& text, WalletQmlModel* wallet, SendRecipient* recipient, QObject* owner)
{
    const QPointer<BitcoinUriModel> self{this}; const QPointer<QObject> original_owner{owner};
    const auto target = wallet ? wallet->importTarget(recipient) : nullptr;
    return self && original_owner ? self->requestImport({paymentimport::Input::Kind::Text, text}, target, original_owner) : nullptr;
}
PaymentImportOperation* BitcoinUriModel::requestImportFile(const QString& path, WalletQmlModel* wallet, SendRecipient* recipient, QObject* owner)
{
    const QPointer<BitcoinUriModel> self{this}; const QPointer<QObject> original_owner{owner};
    const auto target = wallet ? wallet->importTarget(recipient) : nullptr;
    return self && original_owner ? self->requestImport({paymentimport::Input::Kind::File, path}, target, original_owner) : nullptr;
}
PaymentImportOperation* BitcoinUriModel::requestImport(paymentimport::Input input, std::shared_ptr<const paymentimport::TargetPort> target, QObject* owner)
{
    if (m_closed || !owner || !m_services.scheduler || input.value.size() > 1024 * 1024 ||
        m_sequence == std::numeric_limits<quint64>::max()) return nullptr;
    bool capacity;
    { std::lock_guard lock{m_operations->import_mutex}; capacity = m_operations->imports.size() >= MAX_OPERATIONS; }
    if (capacity) { Q_EMIT requestRejected(Rejection::Capacity); return nullptr; }
    auto* operation = new PaymentImportOperation(++m_sequence, owner);
    const QPointer<PaymentImportOperation> observer{operation};
    const auto registry = m_operations;
    const auto post = presentation::ReliablePost(m_services.post);
    paymentimport::Services services; services.scheduler = m_services.scheduler;
    services.wake = [post, timer = registry->timer](auto delay, auto callback) {
        post([timer, delay, callback = std::move(callback)] { QTimer::singleShot(delay, timer.get(), callback); });
    };
    const auto admission = m_admission;
    const auto identity = operation->requestId();
    auto actor = paymentimport::Owner::Create(identity, std::move(services),
        [post, observer](auto snapshot) { post([observer, snapshot = std::move(snapshot)]() mutable { if (observer) observer->project(std::move(snapshot)); }); },
        [weak = std::weak_ptr<BitcoinUriOperations>{registry}](uint64_t id) {
            if (const auto retained = weak.lock()) { std::lock_guard lock{retained->import_mutex}; retained->imports.erase(id); }
        });
    if (!actor || !observer || admission->load()) { if (actor) actor->close(); if (observer) delete observer.data(); return nullptr; }
    operation->m_owner = actor;
    operation->m_snapshot.phase = paymentimport::Phase::Reading; operation->m_snapshot.pending = true;
    operation->m_export_services.scheduler = m_services.scheduler; operation->m_export_services.post = post;
    { std::lock_guard lock{registry->import_mutex}; registry->imports.emplace(operation->requestId(), actor); }
    if (!actor->inspect(std::move(input)) || !actor->chooseTarget(std::move(target)) || !observer || admission->load()) {
        actor->close(); if (observer) delete observer.data(); return nullptr;
    }
    return observer;
}
#endif
BitcoinUriParseOperation* BitcoinUriModel::requestParse(const QString& text, QObject* owner, BitcoinUriParseOperation* replace)
{ return start(text, false, owner, replace); }
BitcoinUriParseOperation* BitcoinUriModel::requestProbe(const QString& text, const QVariantMap& context, QObject* owner, BitcoinUriParseOperation* replace)
{ return start(text, false, owner, replace, context); }
BitcoinUriParseOperation* BitcoinUriModel::requestParseFile(const QString& path, QObject* owner, BitcoinUriParseOperation* replace)
{ return start(path, true, owner, replace); }
BitcoinUriParseOperation* BitcoinUriModel::start(const QString& input, bool file, QObject* owner, BitcoinUriParseOperation* replace, const QVariantMap& context)
{
    if (m_closed || !owner) return nullptr;
    if (replace && replace->m_admission == m_admission && replace->parent() == owner) { replace->closeAdmission(); replace->deleteLater(); }
    if (m_operations->live.size() >= MAX_OPERATIONS) {
        Q_EMIT requestRejected(Rejection::Capacity);
        return nullptr;
    }
    if (!m_services.post) m_services.post = presentation::QueuedPost();
    auto* operation = new BitcoinUriParseOperation(++m_sequence, m_admission, owner);
    m_operations->live.emplace(operation->requestId(), operation);
    operation->m_unregister = [registry = std::weak_ptr<BitcoinUriOperations>{m_operations}, id = operation->requestId()] {
        if (const auto state = registry.lock()) state->live.erase(id);
    };
    const QPointer<BitcoinUriParseOperation> observer{operation};
    const auto post = m_services.post;
    const auto notify = [post, observer] {
        try { post([observer] { if (observer) observer->retryPublication(); }); }
        catch (...) { /* The original operation retains the result for retryPublication(). */ }
    };
    if (!m_services.scheduler) {
        operation->m_work = std::make_shared<BitcoinUriParseWork>();
        operation->m_work->result = Error(tr("Payment URI services are unavailable. Try again."));
        operation->m_work->ready = true; notify(); return operation;
    }
    // Reserve final native value teardown before retaining any parsed payload.
    const auto storage = std::make_shared<std::unique_ptr<BitcoinUriParseWork>>();
    const auto key = m_services.scheduler->allocateKey();
    const auto cleanup = execution::Submit(*m_services.scheduler, [storage] { storage->reset(); }, [] {}, {},
        {execution::Lane::Cleanup, 256, key, execution::WorkClass::Action}, true);
    auto held = cleanup ? m_services.scheduler->borrow(key) : execution::BorrowAdmission{{}, cleanup.rejection};
    if (cleanup) m_services.scheduler->activateCleanup(cleanup.id);
    if (!held) {
        // Bounded, allocation-free-in-size local failure; no native input retained.
        operation->m_work = std::make_shared<BitcoinUriParseWork>();
        operation->m_work->result = Error(tr("Payment URI work capacity is unavailable. Try again."));
        operation->m_work->ready = true; notify(); return operation;
    }
    const auto lease = std::make_shared<execution::ResourceLease>(std::move(*held.lease));
    *storage = std::make_unique<BitcoinUriParseWork>();
    auto work = std::shared_ptr<BitcoinUriParseWork>{lease, storage->get()};
    operation->m_work = work;
    const auto fail = [work, notify, gate = m_admission](std::exception_ptr) {
        if (*gate || work->cancelled) return;
        { std::lock_guard lock{work->mutex}; work->result = Error(BitcoinUriModel::tr("The payment request could not be read.")); }
        work->ready = true; notify();
    };
    size_t bytes = static_cast<size_t>(input.size()) * sizeof(QChar) + 512;
    for (const auto* key : {"address", "note", "requestLabel", "previousText", "currentText"}) bytes += static_cast<size_t>(context.value(key).toString().size()) * sizeof(QChar);
    const auto admission = execution::Submit(*m_services.scheduler, [work, input, file, context, gate = m_admission]() -> std::optional<InputResult> {
        if (*gate || work->cancelled) return {};
        return file ? ReadFile(input) : InputResult{ContextResult(ParseText(input), input, context)};
    }, [work, notify, fail, gate = m_admission](std::optional<InputResult> result) {
        if (!result || *gate || work->cancelled) return;
        try {
            // Core owns bounded file I/O; Domain owns decoding and URI policy.
            auto parsed = std::holds_alternative<QByteArray>(*result)
                ? ParseText(QString::fromUtf8(std::get<QByteArray>(*result)))
                : std::move(std::get<QVariantMap>(*result));
            { std::lock_guard lock{work->mutex}; work->result = std::move(parsed); }
            work->ready = true; notify();
        } catch (...) { fail(std::current_exception()); }
    }, fail,
        {file ? execution::Lane::Core : execution::Lane::Domain, bytes, {}, execution::WorkClass::Observation});
    if (!admission) fail({});
    return operation;
}
