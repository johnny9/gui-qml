// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/guipresentation.h>
#include <qml/walletqmlcontroller.h>
#include <qml/backendexecutor.h>
#include <qml/walletlifecycle.h>
#include <qml/retainedsnapshot.h>

#include <qml/models/walletqmlmodel.h>
#include <qml/util.h>

#include <common/args.h>
#include <common/settings.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <script/descriptor.h>
#include <support/allocators/secure.h>
#include <univalue.h>
#include <util/result.h>
#include <util/time.h>
#include <wallet/walletutil.h>
#include <wallet/imports.h>
#include <wallet/wallet.h>
#include <util/translation.h>
#include <util/threadnames.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <deque>
#include <limits>
#include <initializer_list>

#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMetaObject>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSettings>
#include <QScopeGuard>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace {
size_t CommandPayload(std::initializer_list<size_t> values)
{
    size_t bytes{4096};
    for (const auto value : values) {
        if (value > std::numeric_limits<size_t>::max() - bytes) return std::numeric_limits<size_t>::max();
        bytes += value;
    }
    return bytes;
}

bool NotStarted(std::exception_ptr error)
{
    if (!error) return false;
    try { std::rethrow_exception(error); }
    catch (const execution::NotStarted&) { return true; }
    catch (...) { return false; }
}

QString JoinWarnings(const std::vector<bilingual_str>& warnings)
{
    QStringList lines;
    for (const auto& warning : warnings) {
        if (!warning.translated.empty()) {
            lines.append(QString::fromStdString(warning.translated));
        }
    }
    return lines.join('\n');
}

bool IsBackupLikeFile(const QFileInfo& file_info)
{
    QString file_name = file_info.fileName().toLower();
    return file_name.endsWith(".bak") || file_name.endsWith(".legacy.bak");
}

struct ImportErrorText { QString title, description, help; };
ImportErrorText PrepareImportError(const QString& raw)
{
    const QString error = raw.trimmed();
    if (error.isEmpty()) return {};
    const auto contains = [&](const char* needle) { return error.contains(QLatin1String(needle), Qt::CaseInsensitive); };
    if (contains("legacy wallet") || contains("migrat")) return {
        WalletQmlController::tr("This wallet needs to be migrated"),
        WalletQmlController::tr("The selected backup appears to come from a legacy wallet format. It needs to be migrated before it can be used here."),
        WalletQmlController::tr("If this is a legacy wallet backup, migrate it with the wallet migration tool before importing it here.")};
    if (contains("does not exist")) return {WalletQmlController::tr("We couldn't find that file"),
        WalletQmlController::tr("The selected file is no longer available at that location. Choose the backup file again and retry."), error};
    if (contains("already exists")) return {WalletQmlController::tr("A wallet with this name already exists"),
        WalletQmlController::tr("Importing this backup would create a wallet that already exists in your wallet directory. Remove or rename the existing wallet first, then try again."), error};
    if (contains("verification failed") || contains("not supported")) return {WalletQmlController::tr("This wallet type is not supported"),
        WalletQmlController::tr("The file looks like a wallet backup, but this application could not verify or open it in a supported format."), error};
    return {WalletQmlController::tr("This wallet couldn't be imported"),
        WalletQmlController::tr("The selected file could not be restored as a wallet. Check that it is a valid wallet backup, then try another file."), error};
}
QString NormalizeWalletPath(const QString& path)
{
    if (path.isEmpty()) {
        return {};
    }

    const QUrl url(path);
    QString normalized = url.isLocalFile() ? url.toLocalFile() : path;
    return QFileInfo(normalized).absoluteFilePath();
}

QString InferWalletLoadTarget(interfaces::Node& node, const QString& normalized_path)
{
    const QFileInfo selected_path(normalized_path);
    if (!selected_path.exists()) {
        return {};
    }

    const QString wallet_dir_path = QDir::cleanPath(
        QFileInfo(QString::fromStdString(node.walletLoader().getWalletDir())).absoluteFilePath());
    const QDir wallet_dir(wallet_dir_path);

    auto walletTargetFromDir = [&wallet_dir, &wallet_dir_path](const QString& dir_path) {
        QString clean_dir_path = QDir::cleanPath(dir_path);
        QString relative_path = wallet_dir.relativeFilePath(clean_dir_path);
        // Wallet loader accepts wallet names relative to -walletdir. Use those
        // when possible so nested wallets resolve to "subdir/name". For the
        // top-level wallet directory, keep the absolute path so it remains a
        // valid load target instead of colliding with the "restore" branch.
        if (!relative_path.startsWith("..") && relative_path != ".") {
            return relative_path;
        }
        if (clean_dir_path == wallet_dir_path) {
            return clean_dir_path;
        }
        return clean_dir_path;
    };

    if (selected_path.isDir()) {
        return walletTargetFromDir(selected_path.absoluteFilePath());
    }

    if (!selected_path.isFile() || IsBackupLikeFile(selected_path)) {
        return {};
    }

    QString file_name = selected_path.fileName();
    const QString parent_dir_path = QDir::cleanPath(selected_path.dir().absolutePath());

    if (file_name.compare("wallet.dat", Qt::CaseInsensitive) == 0) {
        // A wallet.dat selected inside wallet storage should be loaded via its
        // containing wallet directory. The file itself may also just be a
        // copied backup, so only treat it as loadable when it already lives
        // inside the configured wallet directory.
        const QString relative_parent = wallet_dir.relativeFilePath(parent_dir_path);
        if (!relative_parent.startsWith("..") || parent_dir_path == wallet_dir_path) {
            return walletTargetFromDir(parent_dir_path);
        }
        return {};
    }

    // Current Bitcoin Core still supports top-level data files in -walletdir
    // for backwards compatibility. Those should be loaded by file name.
    if (parent_dir_path == wallet_dir_path) {
        return file_name;
    }

    return {};
}

QString ResolveManagedWalletReference(interfaces::Node& node, const QString& path, QString* wallet_format = nullptr)
{
    if (wallet_format) {
        wallet_format->clear();
    }

    if (path.isEmpty()) {
        return {};
    }

    const QString candidate = QDir::cleanPath(path);
    const auto wallet_dir_entries = node.walletLoader().listWalletDir();

    // Wallet selector entries come from listWalletDir(), which reports wallet
    // names relative to -walletdir. Accept those names directly before trying
    // to interpret the value as a filesystem path.
    for (const auto& [wallet_path, format] : wallet_dir_entries) {
        const QString listed_wallet = QDir::cleanPath(QString::fromStdString(wallet_path));
        if (listed_wallet == candidate) {
            if (wallet_format) {
                *wallet_format = QString::fromStdString(format);
            }
            return listed_wallet;
        }
    }

    const QString normalized_path = NormalizeWalletPath(path);
    if (normalized_path.isEmpty() || !QFileInfo::exists(normalized_path)) {
        return {};
    }

    QString load_target = InferWalletLoadTarget(node, normalized_path);
    if (load_target.isEmpty()) {
        return {};
    }

    const QString clean_load_target = QDir::cleanPath(load_target);
    for (const auto& [wallet_path, format] : wallet_dir_entries) {
        const QString listed_wallet = QDir::cleanPath(QString::fromStdString(wallet_path));
        if (listed_wallet == clean_load_target) {
            if (wallet_format) {
                *wallet_format = QString::fromStdString(format);
            }
            break;
        }
    }

    return load_target;
}

QString InferRestoreWalletName(const QString& normalized_path)
{
    const QFileInfo selected_path(normalized_path);
    if (!selected_path.exists() || selected_path.isDir()) {
        return {};
    }

    QString wallet_name;
    QString file_name = selected_path.fileName();

    if (file_name.compare("wallet.dat", Qt::CaseInsensitive) == 0) {
        wallet_name = selected_path.dir().dirName();
    } else if (file_name.toLower().endsWith(".legacy.bak")) {
        wallet_name = file_name.left(file_name.size() - QString(".legacy.bak").size());
    } else {
        wallet_name = selected_path.completeBaseName();
    }

    if (wallet_name.isEmpty()) {
        wallet_name = QStringLiteral("restored-wallet");
    }
    return wallet_name;
}

QString StoredDisplayName(const QString& path)
{
    QSettings settings;
    const auto alias = settings.value(QStringLiteral("walletDisplayNames/%1").arg(path.trimmed())).toString().trimmed();
    if (settings.status() != QSettings::NoError) throw std::runtime_error("Wallet aliases could not be read");
    return alias.isEmpty() ? path.trimmed() : alias;
}
QString CheckWalletName(interfaces::Node& node, const QString& raw_name, const std::shared_ptr<WalletNameRegistry>& registry = {})
{
    const auto name = raw_name.trimmed();
    if (name.isEmpty()) return WalletQmlController::tr("Enter a wallet name.");
    const auto conflicts = [&](const QString& real) {
        return real.compare(name, Qt::CaseInsensitive) == 0 || StoredDisplayName(real).compare(name, Qt::CaseInsensitive) == 0;
    };
    for (const auto& [path, format] : node.walletLoader().listWalletDir()) if (conflicts(QString::fromStdString(path)))
        return WalletQmlController::tr("A wallet with this name already exists");
    // Core's getter is not a runtime resynchronization capability. The app
    // seeds once, then records names from retained notification handles.
    std::optional<QString> previous;
    while (registry) {
        QString real;
        {
            std::lock_guard lock{registry->mutex};
            const auto next = previous ? registry->names.upper_bound(*previous) : registry->names.begin();
            if (next == registry->names.end()) break;
            real = *next;
        }
        previous = real;
        if (conflicts(real)) return WalletQmlController::tr("A wallet with this name already exists");
    }
    return {};
}
QString SuggestedExternalSignerWalletName(const QString& signer_name)
{
    QString suggested = signer_name.trimmed();
    suggested.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    suggested.remove(QRegularExpression(QStringLiteral("^_+")));
    while (suggested.contains(QStringLiteral("__"))) {
        suggested.replace(QStringLiteral("__"), QStringLiteral("_"));
    }
    if (suggested.isEmpty()) {
        suggested = QStringLiteral("external_signer");
    }
    return suggested.left(20);
}

} // namespace

struct OpenWalletListModel::State {
    struct Node { uint64_t key{0}; WalletQmlModel* wallet{nullptr}; Node* left{nullptr}; Node* right{nullptr}; int height{1}, size{1}; };
    WalletNameRegistry registry;
    std::deque<Node> arena;
    std::deque<Node*> free;
    std::map<WalletQmlModel*, uint64_t> keys;
    Node* root{nullptr}; uint64_t next{0};
    bool transaction{false}, closed{false}, reconciling{false}, scheduled{false};
    WalletQmlModel* inserting{nullptr};
    std::set<WalletQmlModel*> removals;
    std::map<WalletQmlModel*, QPointer<WalletQmlModel>> observers;
    static int Size(const Node* node) { return node ? node->size : 0; }
    static int Height(const Node* node) { return node ? node->height : 0; }
    static void Fix(Node* node) { node->size = 1 + Size(node->left) + Size(node->right); node->height = 1 + std::max(Height(node->left), Height(node->right)); }
    static Node* Left(Node* node) { auto* root = node->right; node->right = root->left; root->left = node; Fix(node); Fix(root); return root; }
    static Node* Right(Node* node) { auto* root = node->left; node->left = root->right; root->right = node; Fix(node); Fix(root); return root; }
    static Node* Balance(Node* node) {
        Fix(node);
        if (Height(node->left) > Height(node->right) + 1) { if (Height(node->left->right) > Height(node->left->left)) node->left = Left(node->left); return Right(node); }
        if (Height(node->right) > Height(node->left) + 1) { if (Height(node->right->left) > Height(node->right->right)) node->right = Right(node->right); return Left(node); }
        return node;
    }
    static Node* Insert(Node* root, Node* node) {
        // An AVL tree with an int row count has height below64.
        std::array<Node**,64> path{};size_t depth{0};auto** link=&root;
        while(*link){
            Q_ASSERT(depth<path.size());path[depth++]=link;
            link=node->key<(*link)->key?&(*link)->left:&(*link)->right;
        }
        *link=node;
        while(depth){auto** ancestor=path[--depth];*ancestor=Balance(*ancestor);}
        return root;
    }
    static Node* Remove(Node* root, uint64_t key, Node*& removed) {
        std::array<Node**,64> path{};size_t depth{0};auto** link=&root;
        while(key!=(*link)->key){
            Q_ASSERT(depth<path.size());path[depth++]=link;
            link=key<(*link)->key?&(*link)->left:&(*link)->right;
        }
        if((*link)->left&&(*link)->right){
            auto* target=*link;Q_ASSERT(depth<path.size());path[depth++]=link;
            link=&target->right;
            while((*link)->left){Q_ASSERT(depth<path.size());path[depth++]=link;link=&(*link)->left;}
            std::swap(target->key,(*link)->key);std::swap(target->wallet,(*link)->wallet);
        }
        removed=*link;*link=removed->left?removed->left:removed->right;
        while(depth){auto** ancestor=path[--depth];*ancestor=Balance(*ancestor);}
        return root;
    }
    static const Node* At(const Node* root, int row) {
        while (root) { const int left = Size(root->left); if (row == left) return root; if (row < left) root = root->left; else { row -= left + 1; root = root->right; } }
        return nullptr;
    }
    int Rank(uint64_t key) const {
        int row{0}; const auto* node = root;
        while (node) { if (key == node->key) return row + Size(node->left); if (key < node->key) node = node->left; else { row += Size(node->left) + 1; node = node->right; } }
        return -1;
    }
};
OpenWalletListModel::OpenWalletListModel(std::shared_ptr<execution::Scheduler> scheduler, QObject* parent, presentation::Post post)
    : QAbstractListModel(parent), m_post(presentation::ReliablePost(post ? std::move(post) : presentation::QueuedPost()))
{
    m_state = execution::RetainSnapshot<State>(scheduler, [] { return std::make_unique<State>(); });
}
std::shared_ptr<WalletNameRegistry> OpenWalletListModel::nameRegistry() const
{
    return m_state ? std::shared_ptr<WalletNameRegistry>{m_state, &m_state->registry} : std::shared_ptr<WalletNameRegistry>{};
}
OpenWalletListModel::~OpenWalletListModel() { closeAdmission(); }
void OpenWalletListModel::closeAdmission()
{
    if (!m_state) return;
    m_state->closed = true;
    if (!m_state->transaction) m_state.reset();
}
int OpenWalletListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() || !m_state ? 0 : State::Size(m_state->root); }
QModelIndex OpenWalletListModel::index(int row, int column, const QModelIndex& parent) const
{
    if (parent.isValid() || column != 0 || row < 0 || row >= rowCount()) return {};
    return createIndex(row, column, State::At(m_state->root, row)->wallet);
}
QVariant OpenWalletListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= rowCount() || role != Qt::UserRole + 1) return {};
    const auto found = m_state->observers.find(static_cast<WalletQmlModel*>(index.internalPointer()));
    return found == m_state->observers.end() ? QVariant{} : QVariant::fromValue(found->second.data());
}
QHash<int, QByteArray> OpenWalletListModel::roleNames() const { return {{Qt::UserRole + 1, "wallet"}}; }
int OpenWalletListModel::rowForWallet(WalletQmlModel* wallet) const
{
    if (!m_state) return -1;
    const auto key = m_state->keys.find(wallet); return key == m_state->keys.end() ? -1 : m_state->Rank(key->second);
}
bool OpenWalletListModel::addWallet(WalletQmlModel* wallet)
{
    if (!m_state || m_state->closed || m_state->transaction || !wallet || !wallet->available()) return false;
    if (m_state->keys.contains(wallet)) return true;
    const int row = rowCount();
    if (row == std::numeric_limits<int>::max() || m_state->next == std::numeric_limits<uint64_t>::max()) return false;
    auto state = m_state;
    const QPointer<OpenWalletListModel> guard{this};
    const QPointer<WalletQmlModel> original{wallet};
    state->transaction = true;
    state->inserting = wallet;
    beginInsertRows({}, row, row);
    if (!guard) return false;
    State::Node* node;
    if (state->free.empty()) {
        state->arena.emplace_back();
        node = &state->arena.back();
    } else {
        node = state->free.front();
        state->free.pop_front();
        *node = {};
    }
    node->key = ++state->next;
    node->wallet = wallet;
    state->keys.emplace(wallet, node->key);
    state->observers.emplace(wallet, original);
    state->root = State::Insert(state->root, node);
    endInsertRows();
    if (!guard) return false;
    state->inserting = nullptr;
    state->transaction = false;
    if (state->closed) {
        m_state.reset();
        return false;
    }
    if (!original || !original->available()) state->removals.insert(wallet);
    reconcile();
    return guard && m_state == state && state->keys.contains(wallet);
}
void OpenWalletListModel::removeWallet(WalletQmlModel* wallet)
{
    const QPointer<OpenWalletListModel> guard{this};
    const bool removed = removeWalletRow(wallet);
    if (guard && removed) reconcile();
}
bool OpenWalletListModel::removeWalletRow(WalletQmlModel* wallet)
{
    if (!m_state || m_state->closed) return false;
    if (m_state->transaction) {
        // Only a live row or the single promised insertion can enter this
        // inventory; foreign pointers cannot grow a deferred-removal queue.
        if (m_state->keys.contains(wallet) || m_state->inserting == wallet) m_state->removals.insert(wallet);
        return false;
    }
    const int row = rowForWallet(wallet);
    if (row < 0) return false;
    auto state = m_state;
    const auto key = state->keys.at(wallet);
    const QPointer<OpenWalletListModel> guard{this};
    state->transaction = true;
    beginRemoveRows({}, row, row);
    if (!guard) return false;
    State::Node* removed{nullptr};
    state->root = State::Remove(state->root, key, removed);
    state->keys.erase(wallet);
    state->observers.erase(wallet);
    state->removals.erase(wallet);
    removed->wallet = nullptr;
    state->free.push_back(removed);
    endRemoveRows();
    if (!guard) return false;
    state->transaction = false;
    if (state->closed) {
        m_state.reset();
        return false;
    }
    // The caller owns reconciliation, so the bounded drain does not recurse.
    return true;
}
void OpenWalletListModel::reconcile()
{
    if (!m_state || m_state->closed || m_state->transaction || m_state->reconciling) return;
    const auto state = m_state;
    const QPointer<OpenWalletListModel> guard{this};
    state->reconciling = true;
    for (int budget = 32; budget > 0 && !state->removals.empty(); --budget) {
        auto* wallet = *state->removals.begin();
        state->removals.erase(state->removals.begin());
        removeWalletRow(wallet);
        if (!guard || m_state != state) return;
    }
    state->reconciling = false;
    if (!state->removals.empty() && !state->scheduled) {
        state->scheduled = true;
        m_post([guard, state] {
            state->scheduled = false;
            if (guard && guard->m_state == state) guard->reconcile();
        });
    }
}

void WalletImportErrorLines::setValue(QString text, std::shared_ptr<const void> lifetime, bool quiet)
{
    const auto revision = ++m_revision;
    if (quiet || m_resetting) { m_text = std::move(text); m_lifetime = std::move(lifetime); return; }
    const QPointer<WalletImportErrorLines> observer{this};
    m_resetting = true;
    beginResetModel(); if (!observer) return;
    if (revision == m_revision) { m_text = std::move(text); m_lifetime = std::move(lifetime); }
    m_resetting = false;
    endResetModel();
}
struct WalletQmlController::ImportErrorFrame {
    struct Budget { std::atomic_size_t frames{0}, bytes{0}; };
    std::shared_ptr<Budget> budget;
    QString error;
    quint64 revision{0};
    size_t bytes{0};
    ImportErrorFrame(std::shared_ptr<Budget> value, const QString& text, quint64 version)
        : budget(std::move(value)), error(text), revision(version), bytes(size_t(text.size()) * sizeof(QChar)) {}
    ~ImportErrorFrame() { error.clear(); budget->bytes -= bytes; --budget->frames; }
};
struct WalletQmlController::ImportErrorInput {
    std::mutex mutex;
    std::shared_ptr<ImportErrorFrame::Budget> budget{std::make_shared<ImportErrorFrame::Budget>()};
    std::shared_ptr<const ImportErrorFrame> current, notified;
    std::map<const ImportErrorFrame*, std::shared_ptr<const ImportErrorFrame>> retired;
};
struct WalletQmlController::ImportErrorProjection {
    std::shared_ptr<const ImportErrorFrame> frame, previous;
    ImportErrorText text;
    QString details, display;
    bool changed{false}, failed{false};
    quint64 revision{0};
};

namespace {
// Admission charges the canonical key and map-node retention before either is
// allocated. The existing cleanup/resource budgets bound every live origin.
template <typename T, typename Factory>
std::shared_ptr<T> RetainAdoptionValue(const std::shared_ptr<execution::Scheduler>& scheduler, size_t bytes, Factory factory)
{
    struct Storage { std::unique_ptr<T> value; };
    const auto storage = std::make_shared<Storage>();
    const auto key = scheduler->allocateKey();
    const auto cleanup = execution::Submit(*scheduler, [storage] { storage->value.reset(); }, [] {}, {},
        {execution::Lane::Cleanup, bytes, key, execution::WorkClass::Action}, true);
    if (!cleanup) return {};
    auto held = scheduler->borrow(key);
    scheduler->activateCleanup(cleanup.id);
    if (!held) return {};
    const auto lease = std::make_shared<execution::ResourceLease>(std::move(*held.lease));
    try { storage->value = factory(); }
    catch (...) { return {}; }
    return std::shared_ptr<T>{lease, storage->value.get()};
}
}
struct WalletQmlController::OpeningAdoption {
    QPointer<WalletQmlModel> facade;
    walletopening::Opening identity;
    bool closed{false};
    std::shared_ptr<std::atomic<uint64_t>> unloaded{std::make_shared<std::atomic<uint64_t>>(0)};
};
struct WalletQmlController::AdoptionOrigin {
    struct Binding { QString name; std::shared_ptr<OpeningAdoption> opening; };
    std::map<QString, std::shared_ptr<Binding>> bindings;
    bool unavailable{false};
    uint64_t sequence{0}; // GUI origin lifetime only.
    uint64_t admission_event{0}; // Observed Native-event clock, independent domain.
    std::function<void()> retired;
    ~AdoptionOrigin() { if (retired) { try { retired(); } catch (...) {} } }
};

struct WalletQmlController::Retirement : std::enable_shared_from_this<Retirement> {
    struct ClosedFence { QString name; uint64_t origin_cutoff, event_cutoff; std::shared_ptr<OpeningAdoption> opening; };
    struct Values {
        std::shared_ptr<ImportErrorInput> import_errors{std::make_shared<ImportErrorInput>()};
        std::list<WalletQmlModel*> pending;
        std::map<WalletProvisioningCheck*, QPointer<WalletProvisioningCheck>> forms;
        std::map<WalletQmlModel*, QPointer<WalletQmlModel>> alive;
        std::map<QString, WalletQmlModel*> names;
        std::map<WalletQmlModel*, std::list<WalletQmlModel*>::iterator> positions;
        std::map<WalletQmlModel*, WalletInformation> information;
        std::set<WalletQmlModel*> retiring;
        std::multiset<QString> retiring_names;
        std::map<WalletQmlModel*, std::shared_ptr<OpeningAdoption>> adoptions;
        std::set<uint64_t> origins;
        uint64_t admitting_origin{std::numeric_limits<uint64_t>::max()};
        std::map<QString, std::shared_ptr<ClosedFence>> closed;
        std::map<std::pair<uint64_t, uint64_t>, std::shared_ptr<ClosedFence>> close_order;
    };
    std::shared_ptr<Values> values;
    std::shared_ptr<BackendExecutor> executor;
    std::shared_ptr<QObject> bridge;
    QPointer<WalletQmlController> observer;
    QPointer<WalletQmlModel> empty;
    std::shared_ptr<Retirement> keep_alive;
    std::function<void(std::function<void()>)> post;
    std::function<void(std::function<void()>)> shutdown;
    bool started{false}, pumping{false}, batches_done{false}, native_done{false}, completed{false};
    bool selection_changed{false}, loaded_changed{false}, had_wallets{false};
    size_t native_backends{0};
    bool retained{true};
    void pruneAdoptions() {
        if (!values) return;
        const uint64_t oldest = std::min(values->admitting_origin, values->origins.empty() ? std::numeric_limits<uint64_t>::max() : *values->origins.begin());
        int budget{32};
        while (budget-- > 0 && !values->close_order.empty()) {
            const auto next = values->close_order.begin();
            if (next->first.first >= oldest) break;
            const auto found = values->closed.find(next->second->name);
            if (found != values->closed.end() && found->second == next->second) values->closed.erase(found);
            values->close_order.erase(next);
        }
        if (!values->close_order.empty() && values->close_order.begin()->first.first < oldest) {
            const auto self = shared_from_this();
            post([self] { self->pruneAdoptions(); });
        }
    }
    void watch(WalletQmlModel* model) {
        if (!model || !values || values->alive.contains(model)) return;
        values->alive.emplace(model, model);
        ++native_backends;
        const auto weak = weak_from_this();
        model->backendExecutor()->whenDrained(bridge.get(), [weak] { if (const auto state = weak.lock()) { --state->native_backends; state->finish(); } });
        QObject::connect(model, &QObject::destroyed, bridge.get(), [weak, model] {
            if (const auto state = weak.lock(); state && state->values) {
                state->values->alive.erase(model); state->values->retiring.erase(model); state->values->adoptions.erase(model); state->finish();
            }
        });
    }
    bool adopt(WalletQmlModel* model, bool remove) {
        if (!model || !values || values->retiring.contains(model)) return false;
        if (!values->alive.contains(model)) watch(model);
        const bool target = observer && observer->m_close_target == model;
        if (target) observer->m_close_target = nullptr;
        values->retiring.insert(model);
        const auto name = model->name();
        if (model != empty) values->retiring_names.insert(name);
        const auto self = shared_from_this();
        const QPointer<WalletQmlModel> original{model};
        const auto completed = std::make_shared<std::atomic_bool>(false);
        const auto retired = [self, original, model, name, completed] {
            if (completed->exchange(true) || !self->values) return;
            self->values->retiring.erase(model);
            const auto retiring_name = self->values->retiring_names.find(name);
            if (retiring_name != self->values->retiring_names.end()) self->values->retiring_names.erase(retiring_name);
            if (original && model != self->empty) original->deleteLater();
            self->finish();
        };
        QObject::connect(model, &WalletQmlModel::shutdownFinished, bridge.get(), retired);
        if (model->shutdownComplete()) post(retired);
        if (const auto opening = model->opening()) opening->retire(remove);
        return target;
    }
    void enqueue() {
        if (std::exchange(pumping, true)) return;
        const auto self = shared_from_this();
        post([self] { self->pumping = false; self->pump(); });
    }
    void pump() {
        if (!started || batches_done || !values) return;
        int budget{32};
        while (budget-- > 0) {
            if (!values->forms.empty()) {
                const auto form = values->forms.begin()->second; values->forms.erase(values->forms.begin());
                if (form) form->closeAdmission();
                continue;
            }
            if (values->pending.empty()) break;
            auto* raw = values->pending.front(); values->pending.pop_front();
            const auto found = values->alive.find(raw);
            if (found == values->alive.end() || !found->second) continue;
            const QPointer<WalletQmlModel> model = found->second;
            const auto name = model->name();
            const bool target = adopt(model, false);
            model->beginShutdown(); // Actual original-opening retirement is retained independently.
            if (observer) {
                if (target) Q_EMIT observer->closeTargetChanged();
                if (observer) Q_EMIT observer->walletLoadStateChanged(name, WalletListModel::LoadState::Closed, QString{});
            }
        }
        if (!values->forms.empty() || !values->pending.empty()) { enqueue(); return; }
        batches_done = true;
        if (empty) { adopt(empty, false); empty->beginShutdown(); }
        const auto self = shared_from_this();
        const auto notified = std::make_shared<std::atomic_bool>(false);
        shutdown([self, notified] {
            self->post([self, notified] {
                if (notified->exchange(true)) return;
                self->native_done = true;
                if (self->observer) { self->observer->m_controller_drained = true; self->observer->m_notifications_drained = true; }
                self->finish();
            });
        });
        finish();
    }
    void finish() {
        if (!started || !batches_done || native_backends || (values && !values->retiring.empty())) return;
        // The native cleanup reservation owns all large inventory containers.
        // Dropping its lease lets the executor finish only after teardown runs.
        values.reset();
        if (!native_done || std::exchange(completed, true)) return;
        if (empty) { if (observer) empty->setParent(observer); else empty->deleteLater(); }
        if (observer) { observer->m_shutdown_complete = true; Q_EMIT observer->walletsDrained(); }
        keep_alive.reset();
    }
};

WalletProvisioningCheck::WalletProvisioningCheck(interfaces::Node& node, execution::SnapshotServices services, QObject* parent, std::shared_ptr<std::atomic_bool> names_ready, std::shared_ptr<WalletNameRegistry> registry, std::shared_ptr<std::atomic_bool> shutdown_gate)
    : QObject(parent), m_node(node), m_names_ready(std::move(names_ready)), m_registry(std::move(registry)), m_services(std::move(services)), m_closed(std::move(shutdown_gate))
{
    Q_ASSERT(m_services.scheduler);
    m_bridge = presentation::MakeGuiObject();
    if (!m_services.post) m_services.post = [bridge = m_bridge](auto turn) { QMetaObject::invokeMethod(bridge.get(), std::move(turn), Qt::QueuedConnection); };
    refresh();
}
WalletProvisioningCheck::~WalletProvisioningCheck() { closeAdmission(); }
void WalletProvisioningCheck::closeAdmission() { m_closed = true; m_pending = false; if (m_query) m_query->close(); m_query.reset(); m_registry.reset(); }
void WalletProvisioningCheck::setName(const QString& value) { if (m_closed || m_name == value) return; m_name = value; const QPointer<WalletProvisioningCheck> guard{this}; refresh(); if (guard) Q_EMIT inputChanged(); }
void WalletProvisioningCheck::setXpub(const QString& value) { if (m_closed || m_xpub == value) return; m_xpub = value; const QPointer<WalletProvisioningCheck> guard{this}; refresh(); if (guard) Q_EMIT inputChanged(); }
void WalletProvisioningCheck::setPath(const QString& value) { if (m_closed || m_path == value) return; m_path = value; const QPointer<WalletProvisioningCheck> guard{this}; refresh(); if (guard) Q_EMIT inputChanged(); }
void WalletProvisioningCheck::refresh()
{
    if (m_closed) return;
    ++m_revision; m_pending = true; m_error.clear();
    { std::lock_guard lock{m_input->mutex}; m_input->name = m_name; m_input->xpub = m_xpub; m_input->path = m_path; m_input->revision = m_revision; }
    if (!m_query) {
        const QPointer<WalletProvisioningCheck> guard{this};
        m_query = std::make_shared<execution::SnapshotQuery<Result>>(m_services.scheduler, [input = m_input, node = &m_node, ready = m_names_ready, registry = m_registry, gate = m_closed.shared()] {
            if (gate && gate->load()) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
            QString name, xpub, path; Result result;
            { std::lock_guard lock{input->mutex}; name = input->name; xpub = input->xpub; path = input->path; result.revision = input->revision; }
            if (name.size() > 32768 || path.size() > 32768 || xpub.size() > 4096) throw std::length_error("Wallet form exceeds transport budget");
            result.name_error = ready && !ready->load() ? WalletQmlController::tr("Wallets are still loading. Try again in a moment.") : CheckWalletName(*node, name, registry);
            result.xpub_valid = DecodeExtPubKey(xpub.trimmed().toStdString()).pubkey.IsValid();
            result.normalized_path = NormalizeWalletPath(path);
            result.path_exists = !result.normalized_path.isEmpty() && QFileInfo::exists(result.normalized_path);
            return result;
        }, [guard, post = m_services.post] { post([guard] { if (guard) guard->publish(); }); });
    }
    m_query->request(); Q_EMIT changed();
}
void WalletProvisioningCheck::publish()
{
    if (m_closed || !m_query) return;
    const auto result = m_query->take(); if (!result) return;
    if (result->snapshot && result->snapshot->revision != m_revision) return;
    m_pending = false; m_completed = m_revision;
    m_error = result->error ? tr("Wallet form could not be checked.") : QString{};
    m_name_error = result->snapshot ? result->snapshot->name_error : QString{};
    m_xpub_valid = result->snapshot && result->snapshot->xpub_valid;
    m_path_exists = result->snapshot && result->snapshot->path_exists;
    m_normalized_path = result->snapshot ? result->snapshot->normalized_path : QString{};
    Q_EMIT changed();
}
WalletProvisioningCheck* WalletQmlController::createProvisioningCheck()
{
    if (m_shutting_down || !m_retirement->retained) return nullptr;
    auto* form = new WalletProvisioningCheck(m_node, {backgroundScheduler(), m_services.post}, nullptr, m_names_ready, m_names_registry, m_shutdown_gate);
    QQmlEngine::setObjectOwnership(form, QQmlEngine::JavaScriptOwnership);
    m_form_checks.emplace(form, form);
    connect(form, &QObject::destroyed, this, [this, form] { m_form_checks.erase(form); if (m_retirement && m_retirement->values) m_retirement->values->forms.erase(form); });
    connect(this, &WalletQmlController::initializedChanged, form, &WalletProvisioningCheck::refresh);
    return form;
}

void WalletQmlController::describeCommandWallet(CommandResult& result, const QString& fallback)
{
    if (!result.wallet) return;
    result.name = fallback;
    try {
        result.name = QString::fromStdString(result.wallet->getWalletName());
        result.key_scheme = WalletQmlModel::keySchemeDisplayText(WalletQmlModel::keySchemeForWallet(*result.wallet));
    } catch (...) {
        result.incomplete = true;
        result.error = tr("The wallet operation completed, but wallet metadata could not be read. Check the wallet directory before retrying.");
    }
}

WalletQmlController::WalletOperationState WalletQmlController::walletOperationState() const
{
    if (m_shutting_down && m_command_receipt) { std::lock_guard lock{m_command_receipt->mutex}; if (m_command_receipt->ready) return m_command_receipt->state; }
    return m_command_state;
}
QString WalletQmlController::walletOperationName() const
{
    if (!m_command_receipt) return {};
    std::lock_guard lock{m_command_receipt->mutex}; return m_command_receipt->name;
}
bool WalletQmlController::submitWalletCommand(std::function<CommandResult()> effect,
    std::function<void(const CommandResult&)> done, std::function<void(std::exception_ptr)> failed, size_t payload_bytes)
{
    const QPointer<WalletQmlController> observer{this};
    const auto id = ++m_command_id;
    const auto scheduler = backgroundScheduler();
    const auto post = m_services.post;
    auto origin = newAdoptionOrigin();
    if (!observer || observer->m_shutting_down || id != observer->m_command_id) return false;
    m_command_origin = std::move(origin);
    m_adoption_state = WalletAdoptionState::AdoptionPending;
    m_adoption_error.clear();
    updateNotificationContext();
    if (!observer || observer->m_shutting_down || id != observer->m_command_id) return false;
    auto receipt = std::make_shared<CommandReceipt>(); receipt->name = m_command_requested_target; m_command_receipt = receipt;
    auto weak = std::make_shared<std::weak_ptr<Command>>();
    auto command = std::make_shared<Command>(scheduler, [weak, receipt, observer, id, post] {
        if (const auto command = weak->lock()) if (const auto result = command->result()) {
            auto description = result->value ? PrepareImportError(result->value->error).description : QString{};
            if (description.isEmpty()) description = WalletQmlController::tr("The selected file could not be restored as a wallet. Check that it is a valid wallet backup, then try another file.");
            const auto display = result->value ? result->value->error.left(1024) : QString{};
            const bool passphrase = result->value && result->value->error.contains(QLatin1String("passphrase"), Qt::CaseInsensitive);
            std::lock_guard lock{receipt->mutex}; receipt->ready = true; receipt->error_description = std::move(description);
            receipt->error_display = display; receipt->error_needs_passphrase = passphrase;
            if (result->error || !result->value) {
                bool not_started{false};
                if (result->error) try { std::rethrow_exception(result->error); } catch (const execution::NotStarted&) { not_started = true; } catch (...) {}
                receipt->state = not_started ? WalletOperationState::WalletOperationFailed : WalletOperationState::WalletOperationUncertain;
                receipt->error_description = WalletQmlController::tr("The wallet open outcome is uncertain. Check the wallet directory before retrying.");
            }
            else {
                const auto& value = *result->value;
                if (!value.name.isEmpty()) receipt->name = value.name; else if (!value.reference.isEmpty()) receipt->name = value.reference;
                receipt->state = value.incomplete ? WalletOperationState::WalletOperationIncomplete : value.needs_migration ? WalletOperationState::WalletOperationNeedsMigration
                    : value.needs_passphrase ? WalletOperationState::WalletOperationNeedsPassphrase
                    : value.wallet || value.effect_succeeded ? WalletOperationState::WalletOperationSucceeded : WalletOperationState::WalletOperationFailed;
            }
        }
        post([observer, id] { if (observer) observer->publishWalletCommand(id); });
    });
    *weak = command;
    auto owner = execution::RetainSnapshot<std::shared_ptr<Command>>(scheduler, [&] { return std::make_unique<std::shared_ptr<Command>>(command); });
    if (!observer || observer->m_shutting_down || id != observer->m_command_id) return false;
    m_command.reset(); m_command_owner.reset();
    if (!owner || !m_command_origin) {
        m_adoption_state = WalletAdoptionState::AdoptionUnavailable;
        m_adoption_error = tr("The wallet operation could not be started. Try again after pending operations finish.");
        m_command_state = WalletOperationState::WalletOperationFailed;
        failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
        if (observer) Q_EMIT walletOperationChanged();
        return false;
    }
    m_command = command; m_command_owner = std::move(owner);
    m_command_done = std::move(done); m_command_failed = std::move(failed);
    m_command_state = WalletOperationState::WalletOperationPending;
    const auto admission = command->start(std::move(effect),
        {execution::Lane::Core, payload_bytes, scheduler->allocateKey(), execution::WorkClass::Action});
    if (!observer || observer->m_shutting_down || id != observer->m_command_id) return bool(admission);
    if (!admission) {
        m_command_state = WalletOperationState::WalletOperationFailed;
        m_command_failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    }
    if (observer) Q_EMIT walletOperationChanged();
    return bool(admission);
}
void WalletQmlController::publishWalletCommand(quint64 id)
{
    if (m_shutting_down || id != m_command_id || !m_command || m_command_state != WalletOperationState::WalletOperationPending) return;
    if (m_adoption_depth) {
        m_deferred_command_publication = id;
        return;
    }
    const auto lifetime = m_command_owner;
    const auto result = m_command->result(); if (!result) return;
    const QPointer<WalletQmlController> observer{this};
    { std::lock_guard lock{m_command_receipt->mutex}; m_command_state = m_command_receipt->state; }
    const auto done = m_command_done;
    const auto failed = m_command_failed;
    const auto previous_publication = std::exchange(m_publishing_command, id);
    const auto previous_origin = std::exchange(m_publishing_origin, m_command_origin);
    if (result->error || !result->value) failed(result->error);
    else done(*result->value);
    if (!observer) return;
    m_publishing_command = previous_publication;
    m_publishing_origin = previous_origin;
    if (id == m_command_id) {
        m_command_origin.reset();
        Q_EMIT walletOperationChanged();
    }
}

backup::Services WalletQmlController::backupServices() const
{
    backup::Services services;
    services.coordinator = m_backup_installs;
    return services;
}

namespace {
WalletQmlController::Services ValidateControllerServices(WalletQmlController::Services services)
{
    if (bool(services.scheduler) != bool(services.shutdown) || bool(services.scheduler) != bool(services.post))
        throw std::invalid_argument("Controller scheduler, GUI post, and physical shutdown owner must be supplied together");
    return services;
}
}

WalletQmlController::WalletQmlController(interfaces::Node& node, Services services, QObject* parent)
    : WalletQmlController(node, execution::Limits::Bounded(), ValidateControllerServices(std::move(services)), parent) {}

WalletQmlController::WalletQmlController(interfaces::Node& node, QObject* parent)
    : WalletQmlController(node, execution::Limits::Bounded(), parent) {}

WalletQmlController::WalletQmlController(interfaces::Node& node, walletopening::Session::Preferences preferences, QObject* parent)
    : WalletQmlController(node, execution::Limits::Bounded(), parent)
{
    m_preferences = std::move(preferences);
}

WalletQmlController::WalletQmlController(interfaces::Node& node, walletopening::Session::Preferences preferences,
                                       walletopening::FeePolicy fee_policy, QObject* parent)
    : WalletQmlController(node, std::move(preferences), parent)
{
    m_fee_policy = std::move(fee_policy);
}

WalletQmlController::WalletQmlController(interfaces::Node& node, execution::Limits limits, QObject* parent)
    : WalletQmlController(node, limits, Services{}, parent) {}

WalletQmlController::WalletQmlController(interfaces::Node& node, execution::Limits limits, Services services, QObject* parent)
    : QObject(parent)
    , m_node(node)
    , m_empty_wallet(new WalletQmlModel(&node, this))
    , m_selected_wallet(m_empty_wallet)
    , m_wallet_command_executor(services.scheduler ? nullptr : std::make_shared<BackendExecutor>(limits))
    , m_services(std::move(services))
    , m_notification_bridge(presentation::MakeGuiObject())
    , m_open_local_path_fn([](const QString& path) {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    })
{
    if (!m_services.scheduler) m_services.scheduler = m_wallet_command_executor->scheduler();
    if (!m_services.post) m_services.post = [bridge = m_notification_bridge](auto callback) { QMetaObject::invokeMethod(bridge.get(), std::move(callback), Qt::QueuedConnection); };
    m_services.post = presentation::ReliablePost(std::move(m_services.post));
    if (!m_services.shutdown) m_services.shutdown = [executor = m_wallet_command_executor, bridge = m_notification_bridge](auto drained) {
        executor->whenDrained(bridge.get(), std::move(drained));
        executor->shutdown();
    };
    m_retirement = std::make_shared<Retirement>();
    m_retirement->shutdown = m_services.shutdown;
    m_retirement->executor = m_wallet_command_executor; m_retirement->bridge = m_notification_bridge; m_retirement->observer = this; m_retirement->empty = m_empty_wallet;
    m_retirement->post = m_services.post;
    m_retirement->values = execution::RetainSnapshot<Retirement::Values>(backgroundScheduler(), [] { return std::make_unique<Retirement::Values>(); });
    if (!m_retirement->values) {
        // No catalog or form inventory may be admitted without retained cleanup.
        // This fixed-size failed-initialization owner contains only the sentinel.
        m_retirement->retained = false; m_retirement->values = std::make_shared<Retirement::Values>();
    }
    m_retirement->watch(m_empty_wallet);
    m_import_error_input = std::shared_ptr<ImportErrorInput>{m_retirement->values, m_retirement->values->import_errors.get()};
    m_import_error_services = {backgroundScheduler(), m_services.post};
    m_open_wallet_model = new OpenWalletListModel(backgroundScheduler(), this, m_services.post);
    m_names_registry = m_open_wallet_model->nameRegistry();
    m_home_path = qEnvironmentVariable("HOME");
    m_notification_origin = newAdoptionOrigin();
    refreshDisplayAliases();
    m_empty_wallet->setNode(&m_node);
    if (m_wallet_command_executor) m_wallet_command_executor->setObjectName(QStringLiteral("wallet-commands"));
    m_notification_bridge->setObjectName(QStringLiteral("wallet-notifications"));
}

WalletQmlController::~WalletQmlController()
{
    blockSignals(true);
    beginShutdown();
    // A row signal may destroy this owner from inside Qt's paired insertion
    // transaction. Keep the list itself alive until that Qt call unwinds.
    if (m_open_wallet_model) {
        m_open_wallet_model->setParent(nullptr);
        m_open_wallet_model->deleteLater();
        m_open_wallet_model = nullptr;
    }
    m_retirement->observer = nullptr;
    // An independent GUI bridge continues at most32 original facade handoffs
    // per turn. No inventory scan, model deletion loop or GUI wait occurs here.
    m_retirement.reset();
}

void WalletQmlController::setSelectedWallet(QString path, QString wallet_format)
{
    if (m_retirement->values && m_retirement->values->retiring_names.contains(path) && !walletForName(path)) {
        QString error = tr("This wallet is still closing.");
        const QPointer<WalletQmlController> observer{this};
        setWalletLoadError(error); if (!observer || m_shutting_down) return;
        Q_EMIT walletLoadErrorNotice(path, m_command_id, tr("This wallet is still closing.")); if (!observer || m_shutting_down) return;
        Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::LoadError, error);
        return;
    }
    if (!m_initialized) {
        setWalletLoadError(walletDiscoveryError());
        return;
    }

    if (const auto found = m_wallet_names.find(path); found != m_wallet_names.end()) {
        ++m_selection_revision;
        updateNotificationContext();
        const auto revision = m_selection_revision;
        const QPointer<WalletQmlModel> original{found->second};
        const QPointer<WalletQmlController> guard{this};
        clearWalletLoadStatus();
        if (!guard || m_shutting_down || revision != m_selection_revision || !original || !original->available() || !m_wallet_positions.contains(original)) return;
        m_selected_wallet = original; Q_EMIT selectedWalletChanged(); return;
    }
    if (m_admitting_command || m_command_state == WalletOperationState::WalletOperationPending) return;
    m_admitting_command = true; m_admission_selection_revision = m_selection_revision;
    const QPointer<WalletQmlController> observer{this};
    const auto admission_guard = qScopeGuard([observer] { if (observer) observer->m_admitting_command = false; });
    ++m_selection_revision; m_admission_selection_revision = m_selection_revision;
    startWalletLoad(path, wallet_format);
}

WalletQmlModel* WalletQmlController::walletForName(const QString& name) const
{
    const auto found = m_wallet_names.find(name);
    return found != m_wallet_names.end() && found->second->available() ? found->second : nullptr;
}

bool WalletQmlController::selectWallet(WalletQmlModel* wallet)
{
    if (m_shutting_down || !wallet || !wallet->available() || !m_wallet_positions.contains(wallet)) return false;
    ++m_selection_revision;
    updateNotificationContext();
    if (m_selected_wallet != wallet) { m_selected_wallet = wallet; Q_EMIT selectedWalletChanged(); }
    return true;
}

void WalletQmlController::requestCloseWallet(WalletQmlModel* wallet)
{
    if (m_shutting_down || !wallet || !wallet->available() || !m_wallet_positions.contains(wallet)) return;
    const QPointer<WalletQmlController> guard{this};
    const QPointer<WalletQmlModel> original{wallet};
    if (m_close_target != wallet) {
        if (m_close_target) m_close_target->answerClose(m_close_target->closeQuestionToken(), false);
        if (!guard || !original || !original->available() || m_shutting_down) return;
        m_close_target = wallet;
        Q_EMIT closeTargetChanged();
        if (!guard || !original || m_close_target != original) return;
    }
    if (original && original->available() && m_close_target == original) original->requestClose();
}

bool WalletQmlController::isWalletOpen(const QString& path)
{
    return m_wallet_names.contains(path);
}

void WalletQmlController::publishWalletInfo(WalletQmlModel* wallet_model)
{
    const auto found = m_wallet_information.find(wallet_model);
    if (found == m_wallet_information.end()) return;
    const auto information = found->second;
    Q_EMIT walletInfoChanged(
        information.name,
        wallet_model->displayedBalanceSatoshi(),
        information.key_scheme,
        wallet_model->balanceKnown());
}

void WalletQmlController::initializeWalletInfo(WalletQmlModel* wallet_model)
{
    // Preserve the existing opening-metadata capture/publication point. Its
    // legacy queries belong to the opening migration, not balance callbacks.
    m_wallet_information.insert_or_assign(wallet_model, WalletInformation{
        wallet_model->name(), static_cast<int>(wallet_model->keySchemeKind())});
    publishWalletInfo(wallet_model);
}

void WalletQmlController::publishOpenWalletsInfo()
{
    if (m_shutting_down) return;
    m_info_cursor.reset(); publishWalletInfoTurn(++m_info_generation);
}
void WalletQmlController::publishWalletInfoTurn(quint64 generation)
{
    if (m_shutting_down || generation != m_info_generation) return;
    const QPointer<WalletQmlController> guard{this};
    for (int budget = 0; budget < 32; ++budget) {
        const auto next = m_info_cursor ? m_wallet_information.upper_bound(*m_info_cursor) : m_wallet_information.begin();
        if (next == m_wallet_information.end()) return;
        auto* wallet = next->first; m_info_cursor = wallet;
        publishWalletInfo(wallet); if (!guard || m_shutting_down || generation != m_info_generation) return;
    }
    QMetaObject::invokeMethod(this, [guard, generation] { if (guard) guard->publishWalletInfoTurn(generation); }, Qt::QueuedConnection);
}

void WalletQmlController::subscribeWalletInfo(WalletQmlModel* wallet_model)
{
    if (!wallet_model) return;
    // Sender is the wallet model, so Qt auto-disconnects when the model is
    // destroyed in closeWallet/unloadWallets.
    QObject::connect(wallet_model, &WalletQmlModel::balanceChanged,
                     this, [this, wallet_model]() {
                         publishWalletInfo(wallet_model);
                     });
    connect(wallet_model, &WalletQmlModel::openingMetadataChanged, this, [this, wallet_model] { initializeWalletInfo(wallet_model); });
}

QString WalletQmlController::homePath() const
{
    return m_home_path;
}

bool WalletQmlController::openWalletLocation(WalletQmlModel* wallet)
{
    if (m_shutting_down || m_location_pending) return false;
    if (!wallet || wallet == m_empty_wallet) {
        setWalletLocationOpenError(tr("No wallet file is available to view."));
        return false;
    }
    if (!wallet->available()) return false;
    const auto action = wallet->opening()->startAction(); if (!action) return false;
    const auto name = wallet->name().trimmed();
    const auto id = ++m_location_id;
    m_location_wallet = wallet; m_location_pending = true; m_wallet_location_open_error.clear();
    const QPointer<WalletQmlController> guard{this};
    auto operation = std::make_shared<execution::OwnedCommand<LocationResult>>(backgroundScheduler(), [guard, id, post = m_services.post] {
        post([guard, id] { if (guard) guard->publishLocation(id); });
    });
    m_location_operation = operation;
    const auto admitted = operation->start([name, node = &m_node, action] {
        (void)action; // Original opening remains retained through this Native handoff.
        LocationResult result;
        if (name.isEmpty()) { result.error = tr("No wallet file is available to view."); return result; }
        QFileInfo path{name};
        if (!path.isAbsolute()) path = QFileInfo{QDir{QString::fromStdString(node->walletLoader().getWalletDir())}.filePath(name)};
        if (!path.exists()) { result.error = tr("Wallet file not found: %1").arg(path.absoluteFilePath()); return result; }
        result.path = path.isDir() ? path.absoluteFilePath() : path.absolutePath();
        return result;
    });
    if (!admitted) { m_location_pending = false; m_wallet_location_open_error = tr("Wallet location could not be checked."); }
    Q_EMIT walletLocationOpenErrorChanged(); return bool(admitted);
}
void WalletQmlController::publishLocation(quint64 id)
{
    if (m_shutting_down || id != m_location_id || !m_location_operation) return;
    const auto result = m_location_operation->result(); if (!result) return;
    m_location_pending = false;
    m_wallet_location_open_error = result->value ? result->value->error : tr("Wallet location could not be checked.");
    const QPointer<WalletQmlController> guard{this};
    if (m_wallet_location_open_error.isEmpty() && m_location_wallet && m_location_wallet->available()) {
        if (!m_open_local_path_fn(result->value->path)) { if (!guard) return; m_wallet_location_open_error = tr("Could not open wallet file location."); }
    } else if (m_wallet_location_open_error.isEmpty()) m_wallet_location_open_error = tr("This wallet is closing.");
    if (guard) Q_EMIT walletLocationOpenErrorChanged();
}

void WalletQmlController::clearWalletLocationOpenError()
{
    setWalletLocationOpenError({});
}

void WalletQmlController::closeWallet(const QString& path)
{
    if (!m_initialized) {
        setWalletLoadError(walletDiscoveryError());
        return;
    }

    const QPointer<WalletQmlController> guard{this};
    const auto closing = m_wallet_names.find(path);
    if (closing != m_wallet_names.end()) closeOpeningAdoption(closing->second);
    if (!guard || m_shutting_down) return;
    clearWalletLoadStatus(); if (!guard || m_shutting_down) return;
    clearWalletMigrationStatus(); if (!guard || m_shutting_down) return;

    const auto found = m_wallet_names.find(path);
    if (found == m_wallet_names.end()) return;
    auto* wallet_to_close = found->second;
    removeWalletModel(wallet_to_close, true);
}

QString WalletQmlController::walletDisplayName(const QString& path) const
{
    auto name = path.trimmed();
    if (!m_names) return name;
    const auto found = m_names->names.find(name);
    return found != m_names->names.end() ? found->second : name;
}
bool WalletQmlController::setWalletDisplayName(const QString& path, const QString& display_name)
{
    if (m_shutting_down || m_names_write_pending || path.trimmed().isEmpty() || path.size() > 32768 || display_name.size() > 32768) return false;
    const auto name = path.trimmed(), alias = display_name.trimmed();
    const auto id = ++m_names_write_id;
    m_names_write_pending = true; m_names_write_error.clear();
    auto receipt = std::make_shared<SettingsReceipt>(); m_names_write_receipt = receipt;
    auto weak = std::make_shared<std::weak_ptr<execution::OwnedCommand<SettingsResult>>>();
    const QPointer<WalletQmlController> guard{this};
    auto operation = std::make_shared<execution::OwnedCommand<SettingsResult>>(backgroundScheduler(), [weak, receipt, guard, id, post = m_services.post] {
        if (const auto operation = weak->lock()) if (const auto result = operation->result()) {
            std::lock_guard lock{receipt->mutex}; receipt->ready = true;
            receipt->saved = result->value && result->value->saved;
            receipt->error = result->value ? result->value->error : WalletQmlController::tr("Wallet display name write outcome is uncertain.");
        }
        post([guard, id] { if (guard) guard->publishDisplayNameWrite(id); });
    });
    *weak = operation; m_names_write = operation;
    const auto admitted = operation->start([name, alias] {
        QSettings settings;
        const auto key = QStringLiteral("walletDisplayNames/%1").arg(name);
        if (alias.isEmpty() || alias == name) settings.remove(key); else settings.setValue(key, alias);
        settings.sync();
        SettingsResult result; result.saved = settings.status() == QSettings::NoError;
        if (!result.saved) result.error = tr("Wallet display name could not be saved.");
        return result;
    });
    if (!admitted) { m_names_write_pending = false; m_names_write_error = tr("Wallet display name write could not be queued."); }
    Q_EMIT walletDisplayNameWriteChanged(); return bool(admitted);
}
bool WalletQmlController::walletDisplayNameWriteSucceeded() const
{
    if (!m_names_write_receipt) return false;
    std::lock_guard lock{m_names_write_receipt->mutex}; return m_names_write_receipt->ready && m_names_write_receipt->saved;
}
QString WalletQmlController::walletDisplayNameWriteError() const
{
    if (m_names_write_receipt) { std::lock_guard lock{m_names_write_receipt->mutex}; if (m_names_write_receipt->ready) return m_names_write_receipt->error; }
    return m_names_write_error;
}
void WalletQmlController::publishDisplayNameWrite(quint64 id)
{
    if (m_shutting_down || id != m_names_write_id || !m_names_write) return;
    const auto result = m_names_write->result(); if (!result) return;
    m_names_write_pending = false; m_names_write_error = walletDisplayNameWriteError();
    const QPointer<WalletQmlController> guard{this};
    if (walletDisplayNameWriteSucceeded()) refreshDisplayAliases();
    if (guard) Q_EMIT walletDisplayNameWriteChanged();
}
void WalletQmlController::refreshDisplayAliases()
{
    if (m_shutting_down) return;
    if (!m_names_query) {
        const QPointer<WalletQmlController> guard{this};
        m_names_query = std::make_shared<execution::SnapshotQuery<std::shared_ptr<const DisplayAliases>>>(backgroundScheduler(),
            [scheduler = backgroundScheduler()] {
                auto aliases = execution::RetainSnapshot<DisplayAliases>(scheduler, [] {
                    auto result = std::make_unique<DisplayAliases>(); QSettings settings;
                    settings.beginGroup(QStringLiteral("walletDisplayNames"));
                    for (const auto& key : settings.allKeys()) {
                        const auto alias = settings.value(key).toString().trimmed(); if (!alias.isEmpty()) result->names.emplace(key, alias);
                    }
                    if (settings.status() != QSettings::NoError) throw std::runtime_error("Wallet aliases could not be read");
                    result->home = QDir::homePath(); return result;
                });
                if (!aliases) throw std::runtime_error("Wallet display name capacity is unavailable");
                return std::shared_ptr<const DisplayAliases>{std::move(aliases)};
            }, [guard, post = m_services.post] { post([guard] { if (guard) guard->publishDisplayAliases(); }); });
    }
    m_names_query->request();
}
void WalletQmlController::publishDisplayAliases()
{
    if (m_shutting_down || !m_names_query) return;
    const auto result = m_names_query->take(); if (!result) return;
    if (!result->snapshot || !*result->snapshot) { m_names_read_error = tr("Wallet display names could not be refreshed."); Q_EMIT walletDisplayNamesChanged(); return; }
    m_names_read_error.clear();
    m_names = *result->snapshot; m_home_path = m_names->home;
    m_names_cursor.reset(); applyDisplayAliases(++m_names_generation);
}
void WalletQmlController::applyDisplayAliases(quint64 generation)
{
    if (m_shutting_down || generation != m_names_generation) return;
    const QPointer<WalletQmlController> guard{this};
    int budget{32};
    while (budget--) {
        const auto next = m_names_cursor ? m_wallet_information.upper_bound(*m_names_cursor) : m_wallet_information.begin();
        if (next == m_wallet_information.end()) { Q_EMIT walletDisplayNamesChanged(); return; }
        auto* wallet = next->first; m_names_cursor = wallet;
        applyWalletDisplayName(wallet); if (!guard || m_shutting_down) return;
    }
    QMetaObject::invokeMethod(this, [guard, generation] { if (guard) guard->applyDisplayAliases(generation); }, Qt::QueuedConnection);
}

WalletQmlModel* WalletQmlController::selectedWallet() const
{
    return m_selected_wallet;
}

QList<QObject*> WalletQmlController::openWallets() const
{
    QMutexLocker locker(&m_wallets_mutex);
    return QList<QObject*>(m_wallets.begin(), m_wallets.end());
}

void WalletQmlController::unloadWallets()
{
    beginShutdown();
}

void WalletQmlController::closeAdmission()
{
    m_shutdown_gate->store(true); // O(1): fences every original wallet and form owner.
    if (m_import_error_query) m_import_error_query->close();
    m_import_error_query.reset();
    m_import_error_lines.setValue({}, {}, true);
    m_import_error_help.clear(); m_wallet_load_error.clear(); m_import_error_frame.reset(); m_import_error_projection.reset();
    m_import_error_retry_fixed.reset(); m_import_error_retry_owner.reset(); m_import_error_input.reset();
    m_import_error_pending = false;
    m_create_error_lines.setValue({}, {}, true); m_wallet_create_error.clear(); m_create_error_owner.reset();
    m_migration_required_details.setValue({}, {}, true); m_migration_passphrase_details.setValue({}, {}, true);
    m_wallet_migration_error.clear(); m_migration_error_owner.reset();
    m_shutting_down = true; m_names_ready->store(false);
    m_wallet_load_in_progress = false; m_wallet_migration_in_progress = false;
    m_command_state = WalletOperationState::WalletOperationClosed;
    if (m_open_wallet_model) m_open_wallet_model->closeAdmission();
    m_command.reset(); m_command_owner.reset();
    m_command_origin.reset(); m_notification_origin.reset(); m_publishing_origin.reset();
    m_deferred_command_publication = 0;
    m_deferred_notification_publication = false;
    if (m_names_query) m_names_query->close();
    m_names_query.reset(); m_names.reset();
    m_names_write.reset(); m_names_write_pending = false;
    m_location_operation.reset(); m_location_pending = false;
    m_names_registry.reset();
    if (m_signer_query) m_signer_query->close();
    m_external_signer_pending = false;
    if (m_empty_wallet) m_empty_wallet->closeAdmission(); // One fixed sentinel, independent of inventory size.
}
void WalletQmlController::setRetirementPostForTesting(std::function<void(std::function<void()>)> post)
{
    if (!m_shutdown_started && post) m_retirement->post = std::move(post);
}
void WalletQmlController::beginShutdown()
{
    if (std::exchange(m_shutdown_started, true)) return;
    closeAdmission();
    auto state = m_retirement; state->keep_alive = state; state->started = true;
    if (m_load_subscription) m_load_subscription->retire();
    state->selection_changed = m_selected_wallet != m_empty_wallet;
    state->loaded_changed = std::exchange(m_is_wallet_loaded, false);
    state->had_wallets = !m_wallets.empty();
    const bool target_changed = m_close_target != nullptr; m_close_target = nullptr;
    m_initialized = false; m_selected_wallet = m_empty_wallet;
    m_empty_wallet->setParent(nullptr); // Its lifetime now belongs to the independent retirement owner.
    state->values->pending = std::move(m_wallets);
    state->values->forms = std::move(m_form_checks);
    state->values->names = std::move(m_wallet_names);
    state->values->positions = std::move(m_wallet_positions);
    state->values->information = std::move(m_wallet_information);
    backgroundScheduler()->closeOrdinaryAdmission();
    state->enqueue(); // Retirement ownership is complete before any presentation callback.
    const QPointer<WalletQmlController> observer{this};
    if (target_changed) { Q_EMIT closeTargetChanged(); if (!observer) return; }
    Q_EMIT initializedChanged(); if (!observer) return;
    if (state->selection_changed) { Q_EMIT selectedWalletChanged(); if (!observer) return; }
    if (state->had_wallets) { Q_EMIT openWalletsChanged(); if (!observer) return; }
    if (state->loaded_changed) Q_EMIT isWalletLoadedChanged();
}
bool WalletQmlController::adoptRetirement(WalletQmlModel* model, bool remove)
{
    m_wallet_information.erase(model);
    return m_retirement->adopt(model, remove);
}
void WalletQmlController::retireWallet(WalletQmlModel* model, bool remove)
{
    if (!model || !m_retirement->values || m_retirement->values->retiring.contains(model)) return;
    const QPointer<WalletQmlController> observer{this};
    const bool target = adoptRetirement(model, remove);
    model->beginShutdown(remove);
    if (observer && target) Q_EMIT closeTargetChanged();
}
void WalletQmlController::checkShutdownFinished() { if (m_retirement) m_retirement->finish(); }

bool WalletQmlController::registerWalletModel(WalletQmlModel* wallet_model, std::shared_ptr<OpeningAdoption> adoption)
{
    const QPointer<WalletQmlController> observer{this};
    const QPointer<WalletQmlModel> original{wallet_model};
    if (!adoption) adoption = RetainAdoptionValue<OpeningAdoption>(backgroundScheduler(), 512, [] { return std::make_unique<OpeningAdoption>(); });
    if (!adoption || !observer || observer->m_shutting_down || !original) return false;
    adoption->facade = wallet_model;
    adoption->identity = wallet_model->opening()->identity();
    m_retirement->values->adoptions.emplace(wallet_model, std::move(adoption));
    m_retirement->watch(wallet_model); // Covers both initial handoff and notification-created originals.
    connect(wallet_model, &WalletQmlModel::availableChanged, this, [this, wallet_model] {
        if (wallet_model->available() && !m_shutting_down) m_open_wallet_model->addWallet(wallet_model);
        else m_open_wallet_model->removeWallet(wallet_model);
    });
    connect(wallet_model, &WalletQmlModel::walletUnloaded, this,
        [observer, original, post = m_services.post] {
            post([observer, original] { if (observer && original) observer->removeWalletModel(original); });
        });
    connect(wallet_model, &WalletQmlModel::closeConfirmed, this, [this, wallet_model] { removeWalletModel(wallet_model, true); });
    connect(wallet_model, &WalletQmlModel::closeQuestionChanged, this, [this, wallet_model] {
        if (m_close_target == wallet_model && wallet_model->closeQuestionToken().isEmpty()) {
            m_close_target = nullptr;
            Q_EMIT closeTargetChanged();
        }
    });
    return true;
}

void WalletQmlController::removeWalletModel(WalletQmlModel* wallet_model, bool remove)
{
    if (!wallet_model || wallet_model == m_empty_wallet) {
        return;
    }

    const QPointer<WalletQmlController> observer{this};
    closeOpeningAdoption(wallet_model);
    if (!observer || m_shutting_down) return;
    QString wallet_name;
    WalletQmlModel* next_selected_wallet{nullptr};
    {
        QMutexLocker locker(&m_wallets_mutex);
        const auto position = m_wallet_positions.find(wallet_model);
        if (position == m_wallet_positions.end()) {
            return;
        }

        wallet_name = wallet_model->name();
        for (WalletQmlModel* wallet : m_wallets) {
            if (wallet != wallet_model) {
                next_selected_wallet = wallet;
                break;
            }
        }
        m_wallets.erase(position->second); m_wallet_positions.erase(position); m_wallet_names.erase(wallet_name);
        if (m_names_registry) { std::lock_guard name_lock{m_names_registry->mutex}; m_names_registry->names.erase(wallet_name); }
        m_wallet_information.erase(wallet_model);
    }

    const bool selection_changed = m_selected_wallet == wallet_model;
    if (selection_changed) { ++m_selection_revision; m_selected_wallet = next_selected_wallet ? next_selected_wallet : m_empty_wallet; }
    const bool loaded = !m_wallets.empty();
    const bool loaded_changed = std::exchange(m_is_wallet_loaded, loaded) != loaded;
    // Commit the cached projection and resource handoff before any observer
    // can remove or destroy this controller in a nested GUI delivery.
    const bool target_changed = adoptRetirement(wallet_model, remove);
    if (selection_changed) {
        updateNotificationContext();
        Q_EMIT selectedWalletChanged(); if (!observer) return;
    }
    m_open_wallet_model->removeWallet(wallet_model); if (!observer) return;
    wallet_model->beginShutdown(remove); if (!observer) return;
    if (target_changed) { Q_EMIT closeTargetChanged(); if (!observer) return; }
    Q_EMIT openWalletsChanged(); if (!observer) return;
    Q_EMIT walletLoadStateChanged(wallet_name, WalletListModel::LoadState::Closed, QString{});
    if (!observer) return;
    if (loaded_changed) Q_EMIT isWalletLoadedChanged();
}

WalletQmlModel* WalletQmlController::addOrSelectWalletModel(std::shared_ptr<interfaces::Wallet> wallet, const std::optional<QString>& captured_name, bool select_wallet, std::shared_ptr<AdoptionOrigin> origin, uint64_t ingress_sequence)
{
    if (!wallet) return nullptr;
    if (m_shutting_down) return nullptr;
    const QPointer<WalletQmlController> guard{this};
    ++m_adoption_depth;
    const auto publication_guard = qScopeGuard([guard] {
        if (!guard) return;
        --guard->m_adoption_depth;
        guard->flushDeferredPublications();
    });
    const auto state = m_retirement;
    const auto values = state->values;
    const auto scheduler = backgroundScheduler();
    const auto publication = m_publishing_command;
    const auto selection_revision = m_selection_revision;
    const auto current = [&] {
        return guard && !guard->m_shutting_down && state->values == values && (!publication || guard->m_command_id == publication);
    };
    if (!captured_name) { setWalletLoadError(tr("Wallet information is unavailable.")); return nullptr; }
    const QString& name = *captured_name;
    if (!origin) origin = m_publishing_origin ? m_publishing_origin : m_notification_origin;
    if (!origin) { adoptionUnavailable(false); return nullptr; }
    const auto source_sequence = ingress_sequence ? ingress_sequence : origin->admission_event;
    std::shared_ptr<OpeningAdoption> previous_opening;
    if (const auto bound = origin->bindings.find(name); bound != origin->bindings.end()) previous_opening = bound->second->opening;
    if (!previous_opening) {
        if (const auto closed = values->closed.find(name); closed != values->closed.end()) previous_opening = closed->second->opening;
    }
    if (!previous_opening) {
        if (const auto named = m_wallet_names.find(name); named != m_wallet_names.end()) previous_opening = values->adoptions.at(named->second);
    }
    const auto unloaded = previous_opening ? previous_opening->unloaded->load() : 0;
    if (unloaded && source_sequence <= unloaded) { adoptionUnavailable(true); return nullptr; }
    if (ingress_sequence && unloaded && ingress_sequence > unloaded) {
        // This observes a new load AFTER the original public unload callback;
        // the GUI interest context is not a Core backend identity. Never alter
        // its old binding, which still protects delayed command results.
        if (previous_opening->facade && !previous_opening->facade->available()) {
            const auto selected = m_selection_revision;
            removeWalletModel(previous_opening->facade, false);
            if (!current()) return nullptr;
            if (selected != m_selection_revision) select_wallet = false;
        }
        auto fresh = newAdoptionOrigin();
        if (!current()) return nullptr;
        if (!fresh) { adoptionUnavailable(false); return nullptr; }
        origin = std::move(fresh);
    }
    if (origin->unavailable || source_sequence <= m_adoption_unavailable_event_cutoff) { adoptionUnavailable(false); return nullptr; }
    if (const auto closed = values->closed.find(name); closed != values->closed.end() && source_sequence <= closed->second->event_cutoff) {
        adoptionUnavailable(true);
        return nullptr;
    }
    std::shared_ptr<OpeningAdoption> adoption;
    if (const auto found = origin->bindings.find(name); found != origin->bindings.end()) {
        adoption = found->second->opening;
        if (adoption->closed || !adoption->facade || !adoption->facade->available() || adoption->facade->opening()->identity() != adoption->identity) {
            adoptionUnavailable(true);
            return nullptr;
        }
    }
    WalletQmlModel* model{adoption ? adoption->facade.data() : nullptr};
    bool mismatched_opening{false};
    {
        QMutexLocker locker(&m_wallets_mutex);
        select_wallet |= m_wallets.empty();
        if (const auto found = m_wallet_names.find(name); found != m_wallet_names.end()) {
            mismatched_opening = model && model != found->second;
            model = found->second;
        }
    }
    if (mismatched_opening) { adoptionUnavailable(true); return nullptr; }
    if (!adoption && model) adoption = m_retirement->values->adoptions.at(model);
    if (!adoption) adoption = RetainAdoptionValue<OpeningAdoption>(scheduler, 512, [] { return std::make_unique<OpeningAdoption>(); });
    if (!current()) return nullptr;
    if (!adoption) { origin->unavailable = true; adoptionUnavailable(false); return nullptr; }
    if (!origin->bindings.contains(name)) {
        const auto units = static_cast<size_t>(name.size());
        if (units > (std::numeric_limits<size_t>::max() - 768) / 4) { origin->unavailable = true; adoptionUnavailable(false); return nullptr; }
        auto binding = RetainAdoptionValue<AdoptionOrigin::Binding>(scheduler, 768 + units * 4, [name, adoption] {
            auto value = std::make_unique<AdoptionOrigin::Binding>(); value->name = name; value->opening = adoption; return value;
        });
        if (!current()) return nullptr;
        if (!binding) { origin->unavailable = true; adoptionUnavailable(false); return nullptr; }
        origin->bindings.emplace(binding->name, binding);
    }
    const bool added = !model;
    if (added) {
        model = new WalletQmlModel(WalletQmlModel::SharedWallet{std::move(wallet), backgroundScheduler(), captured_name.value_or(QString{}), m_preferences, m_fee_policy, m_shutdown_gate, {m_adoption_clock, adoption->unloaded}}, &m_node, FeeReadServices{}, psbtfee::Services{}, backupServices());
        m_retirement->watch(model);
        if (!registerWalletModel(model, adoption)) {
            if (!current()) return nullptr;
            retireWallet(model, false);
            adoptionUnavailable(false);
            return nullptr;
        }
        subscribeWalletInfo(model);
        QMutexLocker locker(&m_wallets_mutex);
        m_wallets.push_back(model); m_wallet_positions.emplace(model, std::prev(m_wallets.end())); m_wallet_names.insert_or_assign(name, model);
        if (m_names_registry) { std::lock_guard name_lock{m_names_registry->mutex}; m_names_registry->names.insert(name); }
    }
    const QPointer<WalletQmlModel> original{model};
    const auto still_adopted = [&] {
        return current() && original && !adoption->closed && original->available()
            && original->opening()->identity() == adoption->identity && m_wallet_positions.contains(original)
            && m_wallet_names.contains(name) && m_wallet_names.at(name) == original;
    };
    const auto keep_adopted = [&] {
        if (still_adopted()) return true;
        if (current()) adoptionUnavailable(true);
        return false;
    };
    const bool listed = m_open_wallet_model->addWallet(model);
    if (!keep_adopted()) return nullptr;
    if (!listed) {
        adoptionUnavailable(false);
        return nullptr;
    }
    applyWalletDisplayName(model);
    if (!keep_adopted()) return nullptr;
    select_wallet = select_wallet && m_selection_revision == selection_revision;
    if (select_wallet) m_selected_wallet = model;
    if (added) {
        Q_EMIT openWalletsChanged();
        if (!keep_adopted()) return nullptr;
    }
    Q_EMIT walletLoadStateChanged(name, WalletListModel::LoadState::Open, QString{});
    if (!keep_adopted()) return nullptr;
    initializeWalletInfo(model);
    if (!keep_adopted()) return nullptr;
    if (select_wallet && m_selection_revision == selection_revision) {
        Q_EMIT selectedWalletChanged();
        if (!keep_adopted()) return nullptr;
    }
    setWalletLoaded(true);
    if (!keep_adopted()) return nullptr;
    setNoWalletsFound(false);
    if (!keep_adopted()) return nullptr;
    if (m_publishing_origin == origin) m_adoption_state = WalletAdoptionState::AdoptionAvailable;
    return model;
}

bool WalletQmlController::createWalletAsync(const QString& name,
                                            SecureString passphrase,
                                            uint64_t wallet_creation_flags,
                                            WalletLoadAction load_action,
                                            bool report_create_error,
                                            WalletSetupFn setup_wallet, std::function<QString()> preflight,
                                            secretinput::Captured captured_passphrase, size_t extra_payload_bytes)
{
    const QPointer<WalletQmlController> observer{this};
    QString wallet_name{name}; m_command_requested_target = wallet_name;
    const auto selection = m_admitting_command ? m_admission_selection_revision : m_selection_revision;
    if (setup_wallet) {
        m_deferred_wallet_name = wallet_name;
    }
    m_command_state = WalletOperationState::WalletOperationPending;
    setWalletLoadInProgress(true); if (!observer || m_shutting_down) return false;

    const size_t payload_bytes = CommandPayload({secretinput::Bytes(captured_passphrase), passphrase.capacity(),
        static_cast<size_t>(wallet_name.size()) * sizeof(QChar), extra_payload_bytes});
    return submitWalletCommand([node = &m_node, wallet_name, passphrase = std::move(passphrase), captured_passphrase = std::move(captured_passphrase),
                            wallet_creation_flags, setup_wallet = std::move(setup_wallet), preflight = std::move(preflight), registry = m_names_registry]() mutable {
        CommandResult out;
        const auto clear_secret = qScopeGuard([&] { QmlUtil::ClearSecureString(passphrase); });
        try {
            wallet_name = wallet_name.trimmed();
            if (captured_passphrase) {
                passphrase = secretinput::Materialize(captured_passphrase);
                captured_passphrase.reset();
            }
            if (preflight) out.error = preflight();
            if (out.error.isEmpty()) out.error = CheckWalletName(*node, wallet_name, registry);
        } catch (...) { out.error = tr("Wallet details could not be checked. No wallet was created."); }
        if (!out.error.isEmpty()) return out;
        std::vector<bilingual_str> warnings;
        auto wallet = node->walletLoader().createWallet(wallet_name.toStdString(), passphrase, wallet_creation_flags, warnings);
        QmlUtil::ClearSecureString(passphrase);
        out.warnings = JoinWarnings(warnings);
        if (!wallet) {
            auto error = util::ErrorString(wallet);
            out.error = QString::fromStdString(error.translated.empty() ? error.original : error.translated);
            if (out.error.isEmpty()) out.error = tr("Wallet creation failed.");
        } else {
            // Core creation has already happened. Public remove only unloads;
            // preserve the incomplete storage outcome if descriptor setup fails.
            out.effect_succeeded = true; out.name = wallet_name; out.reference = wallet_name;
            if (setup_wallet) {
                try { out.error = setup_wallet(**wallet); }
                catch (...) { out.error = tr("Wallet storage was created, but descriptor setup outcome is uncertain. Check the wallet directory before retrying."); }
            }
            if (!out.error.isEmpty()) {
                out.incomplete = true;
                try { (*wallet)->remove(); } catch (...) { out.warnings = tr("The incomplete wallet could not be unloaded."); }
            } else out.wallet = std::move(*wallet);
        }
        describeCommandWallet(out, wallet_name);
        return out;
    }, [this, wallet_name, load_action, report_create_error, selection](const CommandResult& out) {
        const QPointer<WalletQmlController> guard{this};
        if (m_deferred_wallet_name == wallet_name) m_deferred_wallet_name.clear();
        setWalletLoadWarnings(out.warnings); if (!guard) return;
        setWalletLoadInProgress(false); if (!guard) return;
        if (!out.wallet || out.incomplete) { if (report_create_error) setWalletCreateError(out.error); else setWalletLoadError(out.error); return; }
        auto* wallet = addOrSelectWalletModel(out.wallet, out.name, selection == m_selection_revision); if (!guard || !wallet) return;
        if (load_action == WalletLoadAction::Load) Q_EMIT walletLoadSucceeded(wallet);
        else Q_EMIT walletCreateSucceeded(wallet);
    }, [this, report_create_error](std::exception_ptr failure) {
        const QPointer<WalletQmlController> guard{this};
        m_deferred_wallet_name.clear(); setWalletLoadInProgress(false); if (!guard) return;
        const auto error = NotStarted(failure) ? tr("Wallet creation could not be queued. No wallet was created.")
            : tr("Wallet creation outcome is uncertain. Check the wallet directory before retrying.");
        if (report_create_error) setWalletCreateError(error); else setWalletLoadError(error);
    }, payload_bytes);
}

void WalletQmlController::createSingleSigWallet(const QString &name, const QString &passphrase)
{
    const QPointer<WalletQmlController> observer{this};
    if (m_admitting_command) return;
    m_admitting_command = true; m_admission_selection_revision = m_selection_revision;
    const auto admission_guard = qScopeGuard([observer] { if (observer) observer->m_admitting_command = false; });
    if (m_shutting_down || m_command_state == WalletOperationState::WalletOperationPending || m_wallet_load_in_progress) return;
    clearWalletCreateStatus(); if (!observer || m_shutting_down) return;
    clearWalletLoadStatus(); if (!observer || m_shutting_down) return;
    clearWalletMigrationStatus(); if (!observer || m_shutting_down) return;
    if (!m_initialized) {
        setWalletCreateError(walletDiscoveryError());
        return;
    }

    auto captured = secretinput::Capture(backgroundScheduler(), passphrase);
    if (!captured) { setWalletCreateError(tr("The password input could not be queued. No wallet was created.")); return; }
    createWalletAsync(name,
                      {},
                      wallet::WALLET_FLAG_DESCRIPTORS,
                      WalletLoadAction::Create,
                      /*report_create_error=*/true, {}, {}, std::move(captured.input));
}

bool WalletQmlController::createExternalSignerWallet(const QString& name)
{
    const QPointer<WalletQmlController> observer{this};
    if (m_admitting_command) return false;
    m_admitting_command = true; m_admission_selection_revision = m_selection_revision;
    const auto admission_guard = qScopeGuard([observer] { if (observer) observer->m_admitting_command = false; });
    if (m_shutting_down || m_command_state == WalletOperationState::WalletOperationPending) return false;
    clearWalletLoadStatus(); if (!observer || m_shutting_down) return false;
    clearWalletMigrationStatus(); if (!observer || m_shutting_down) return false;
    if (!m_initialized) {
        setWalletLoadError(walletDiscoveryError());
        return false;
    }

    if (m_external_signer_pending) {
        setWalletLoadError(tr("Wait for the external signer check to finish."));
        return false;
    }
    // Creation revalidates the device through Core. Cached discovery controls
    // presentation and must never execute the signer command on GUI.
    if (!m_external_signer_path_configured) {
        setWalletLoadError(tr("Set an external signer path in Wallet settings first."));
        return false;
    }
    if (!m_external_signer_error.isEmpty()) {
        setWalletLoadError(m_external_signer_error);
        return false;
    }
    if (m_external_signer_count == 0) {
        setWalletLoadError(tr("Connect an external signer and try again."));
        return false;
    }
    if (m_external_signer_count > 1) {
        setWalletLoadError(tr("More than one external signer was found. Connect only one device and try again."));
        return false;
    }

    constexpr uint64_t flags = wallet::WALLET_FLAG_DESCRIPTORS |
        wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS |
        wallet::WALLET_FLAG_EXTERNAL_SIGNER;
    return createWalletAsync(name,
                      SecureString{},
                      flags,
                      WalletLoadAction::Load,
                      /*report_create_error=*/false);
}

void WalletQmlController::createWatchOnlyWallet(const QString &name, const QString &xpub)
{
    const QPointer<WalletQmlController> observer{this};
    if (m_admitting_command) return;
    m_admitting_command = true; m_admission_selection_revision = m_selection_revision;
    const auto admission_guard = qScopeGuard([observer] { if (observer) observer->m_admitting_command = false; });
    if (m_shutting_down || m_command_state == WalletOperationState::WalletOperationPending || m_wallet_load_in_progress) return;
    clearWalletLoadStatus(); if (!observer || m_shutting_down) return;
    clearWalletMigrationStatus(); if (!observer || m_shutting_down) return;
    if (!m_initialized) {
        setWalletLoadError(walletDiscoveryError());
        return;
    }

    const uint64_t creation_flags = wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS |
                                    wallet::WALLET_FLAG_DESCRIPTORS |
                                    wallet::WALLET_FLAG_BLANK_WALLET;
    createWalletAsync(name,
                      SecureString{},
                      creation_flags,
                      WalletLoadAction::Create,
                      /*report_create_error=*/false,
                      [xpub](interfaces::Wallet& wallet) {
        const auto key = xpub.trimmed().toStdString();
        const std::vector<std::pair<std::string, bool>> descriptors{{"wpkh(" + key + "/0/*)", false}, {"wpkh(" + key + "/1/*)", true}};
        std::vector<wallet::ImportDescriptorRequest> requests;
        requests.reserve(descriptors.size());
        for (const auto& [descriptor, internal] : descriptors) {
            const auto checksum = GetDescriptorChecksum(descriptor);
            if (checksum.empty()) return WalletQmlController::tr("Failed to import descriptors into watch-only wallet.");
            wallet::ImportDescriptorRequest request;
            request.descriptor = descriptor + "#" + checksum;
            request.active = true;
            request.internal = internal;
            request.range = std::pair<int64_t, int64_t>{0, 0};
            request.next_index = 0;
            // The public implementation chooses now and owns the complete
            // import, notification, refresh and rescan outcome on Core.
            requests.push_back(std::move(request));
        }
        const auto results = wallet.importDescriptors(requests);
        if (results.size() != requests.size() || std::any_of(results.begin(), results.end(),
                [](const wallet::ImportResult& result) { return result.has_error(); })) {
            // Reject a partial external/internal pair and let the accepted
            // creation command remove the incomplete wallet before publishing.
            return WalletQmlController::tr("Failed to import descriptors into watch-only wallet.");
        }
        return QString{};
    }, [xpub] {
        if (xpub.size() > 4096 || !DecodeExtPubKey(xpub.trimmed().toStdString()).pubkey.IsValid()) return WalletQmlController::tr("Invalid extended public key.");
        return QString{};
    }, {}, static_cast<size_t>(xpub.size()) * sizeof(QChar));
}

void WalletQmlController::importWallet(const QString& path)
{
    const QPointer<WalletQmlController> observer{this};
    if (m_admitting_command) return;
    m_admitting_command = true; m_admission_selection_revision = m_selection_revision;
    const auto admission_guard = qScopeGuard([observer] { if (observer) observer->m_admitting_command = false; });
    if (!m_initialized) {
        setWalletLoadError(walletDiscoveryError());
        return;
    }
    startWalletImport(path);
}

void WalletQmlController::clearWalletCreateStatus()
{
    setWalletCreateError(QString());
}

void WalletQmlController::clearWalletLoadStatus()
{
    const QPointer<WalletQmlController> guard{this};
    if (m_command_state != WalletOperationState::WalletOperationPending) { setWalletLoadInProgress(false); if (!guard) return; }
    setWalletLoadError({}); if (guard) setWalletLoadWarnings({});
}

void WalletQmlController::migrateWallet(const QString& path, const QString& passphrase)
{
    const QPointer<WalletQmlController> observer{this};
    if (m_admitting_command) return;
    m_admitting_command = true; m_admission_selection_revision = m_selection_revision;
    const auto admission_guard = qScopeGuard([observer] { if (observer) observer->m_admitting_command = false; });
    if (m_shutting_down || m_command_state == WalletOperationState::WalletOperationPending) return;
    if (!m_initialized) {
        setWalletMigrationError(walletDiscoveryError());
        return;
    }
    auto captured = secretinput::Capture(backgroundScheduler(), passphrase);
    if (!captured) {
        const auto revision = setWalletMigrationError(tr("The password input could not be queued. No wallet update was started."));
        if (!observer || m_shutting_down || revision != m_migration_error_revision) return;
        Q_EMIT walletMigrationErrorNotice(path, m_migration_error_operation, false, m_migration_error_display);
        if (observer && !m_shutting_down) Q_EMIT walletMigrationFailed();
        return;
    }
    startWalletMigration(path, {}, std::move(captured.input));
}

void WalletQmlController::clearWalletMigrationStatus()
{
    const QPointer<WalletQmlController> guard{this};
    if (m_command_state != WalletOperationState::WalletOperationPending) { setWalletMigrationInProgress(false); if (!guard) return; }
    setWalletMigrationError({});
}

void WalletQmlController::requestOpenWalletSettings(WalletQmlModel* wallet)
{
    if (!wallet || !wallet->available() || !m_wallet_positions.contains(wallet)) return;
    Q_EMIT openWalletSettingsRequested(wallet);
}

void WalletQmlController::setExternalSignerServices(execution::SnapshotServices services)
{
    Q_ASSERT(!m_signer_query);
    m_signer_services = std::move(services);
}

void WalletQmlController::refreshExternalSignerStatus()
{
    if (m_shutting_down) return;
    ++*m_signer_revision;
    if (!m_signer_query) {
        const QPointer<WalletQmlController> observer{this};
        if (!m_signer_services.scheduler) m_signer_services.scheduler = backgroundScheduler();
        if (!m_signer_services.post) m_signer_services.post = m_services.post;
        m_signer_query = std::make_shared<execution::SnapshotQuery<SignerStatus>>(m_signer_services.scheduler,
            [node = &m_node, revision = m_signer_revision] {
                SignerStatus result; result.revision = revision->load();
                try {
                    const auto path = QString::fromStdString(SettingToString(node->getPersistentSetting("signer")).value_or("")).trimmed();
                    result.configured = !path.isEmpty();
                    if (result.configured) node->forceSetting("signer", path.toStdString());
                    else node->forceSetting("signer", common::SettingsValue{});
                    const auto signers = node->listExternalSigners();
                    result.count = static_cast<int>(signers.size());
                    if (result.count == 1) {
                        const auto name = signers.front()->getName();
                        if (name.size() > 32'768) throw std::length_error{"Signer name exceeds presentation budget"};
                        result.name = QString::fromStdString(name);
                    }
                    else if (result.count > 1) result.error = tr("More than one external signer was found. Connect only one device.");
                } catch (...) { result.error = tr("The signer command did not return valid output. Check that the path is correct."); }
                result.suggested = SuggestedExternalSignerWalletName(result.name);
                return result;
            }, [post = m_signer_services.post, observer] {
                post([observer] { if (observer) observer->publishExternalSignerStatus(); });
            });
    }
    m_external_signer_pending = true;
    m_signer_query->request();
    Q_EMIT externalSignerStatusChanged();
}

void WalletQmlController::publishExternalSignerStatus()
{
    if (m_shutting_down || !m_signer_query) return;
    const auto result = m_signer_query->take();
    if (!result) return;
    if (result->snapshot && result->snapshot->revision != m_signer_revision->load()) return;
    m_external_signer_pending = false;
    const auto status = result->snapshot ? *result->snapshot : SignerStatus{};
    const QPointer<WalletQmlController> observer{this};
    setExternalSignerStatus(status.configured, status.count, status.name,
        result->snapshot ? status.error : tr("The external signer check could not be queued."),
        result->snapshot ? status.suggested : QStringLiteral("external_signer"));
    if (observer) Q_EMIT externalSignerStatusChanged();
}

void WalletQmlController::requestOpenReceive(WalletQmlModel* wallet)
{
    if (!wallet || !wallet->available() || !m_wallet_positions.contains(wallet)) return;
    Q_EMIT openReceiveRequested(wallet);
}

void WalletQmlController::requestClosePaymentRequestDetail(WalletQmlModel* wallet)
{
    if (!wallet || !wallet->available() || !m_wallet_positions.contains(wallet)) return;
    Q_EMIT closePaymentRequestDetailRequested(wallet);
}

QString WalletQmlController::walletImportErrorTitle() const { return m_import_error_title; }
QString WalletQmlController::walletImportErrorDescription() const { return m_import_error_description; }
QString WalletQmlController::walletImportErrorHelpText() const { return m_import_error_help; }
QString WalletQmlController::currentCommandErrorDescription() const
{
    if (!m_command_receipt) return {};
    std::lock_guard lock{m_command_receipt->mutex}; return m_command_receipt->error_description;
}

QString WalletQmlController::walletDisplayNameKey(const QString& path) const
{
    return QStringLiteral("walletDisplayNames/%1").arg(path);
}

void WalletQmlController::applyWalletDisplayName(WalletQmlModel* wallet_model) const
{
    if (!wallet_model) {
        return;
    }
    wallet_model->setDisplayName(walletDisplayName(wallet_model->name()));
}
void WalletQmlController::startWalletImport(const QString& path)
{
    const QPointer<WalletQmlController> admission_observer{this};
    m_command_requested_target = path;
    if (m_shutting_down || m_command_state == WalletOperationState::WalletOperationPending) return;
    m_command_state = WalletOperationState::WalletOperationPending;
    clearWalletMigrationStatus(); if (!admission_observer || m_shutting_down) return; clearWalletLoadStatus(); if (!admission_observer || m_shutting_down) return; clearLastImportedWalletInfo(); if (!admission_observer || m_shutting_down) return;
    setWalletLoadInProgress(true); if (!admission_observer || m_shutting_down) return;
    const auto selection = m_admitting_command ? m_admission_selection_revision : m_selection_revision;
    submitWalletCommand([node = &m_node, path] {
        CommandResult out;
        const auto normalized = NormalizeWalletPath(path);
        if (normalized.isEmpty()) { out.error = tr("Choose a wallet backup file."); return out; }
        if (!QFileInfo::exists(normalized)) { out.error = tr("The selected wallet path does not exist."); return out; }
        const auto name = InferRestoreWalletName(normalized);
        std::vector<bilingual_str> warnings;
        auto wallet = node->walletLoader().restoreWallet(fs::PathFromString(normalized.toStdString()), name.toStdString(), warnings, true);
        out.warnings = JoinWarnings(warnings);
        if (!wallet) out.error = QString::fromStdString(util::ErrorString(wallet).translated);
        else out.wallet = std::move(*wallet);
        describeCommandWallet(out, name);
        return out;
    }, [this, selection](const CommandResult& out) {
        const QPointer<WalletQmlController> guard{this};
        setWalletLoadInProgress(false); if (!guard) return;
        setWalletLoadWarnings(out.warnings); if (!guard) return;
        if (!out.wallet || out.incomplete) { setWalletLoadError(out.error.isEmpty() ? tr("Wallet import failed.") : out.error); return; }
        setLastImportedWalletInfo(out.name, out.key_scheme); if (!guard) return;
        auto* wallet = addOrSelectWalletModel(out.wallet, out.name, selection == m_selection_revision); if (!guard || !wallet) return;
        Q_EMIT walletImportSucceeded(wallet);
    }, [this](std::exception_ptr failure) {
        const QPointer<WalletQmlController> guard{this};
        setWalletLoadInProgress(false);
        if (guard) setWalletLoadError(NotStarted(failure) ? tr("Wallet import could not be queued. No wallet was restored.")
            : tr("Wallet import outcome is uncertain. Check the wallet directory before retrying."));
    }, CommandPayload({static_cast<size_t>(path.size()) * sizeof(QChar)}));
}

QString WalletQmlController::walletDiscoveryError() const
{
    return m_initialization_failed ? tr("Wallet discovery is unavailable. Restart the app to refresh the wallet list.") :
        tr("Wallets are still loading. Try again in a moment.");
}

void WalletQmlController::consumeWalletNotifications()
{
    if (m_shutting_down || m_initialization_failed || !m_initialized) return;
    if (m_adoption_depth || m_projecting_notifications) {
        m_deferred_notification_publication = true;
        return;
    }
    const QPointer<WalletQmlController> guard{this};
    m_projecting_notifications = true;
    const auto restore = qScopeGuard([guard] {
        if (!guard) return;
        guard->m_projecting_notifications = false;
        guard->flushDeferredPublications();
    });
    auto handoff = m_load_subscription->takeNotifications();
    if (!handoff) return;
    for (const auto& entry : handoff->entries) {
        // Keep the handoff's shadow until its reserved background disposal.
        // The opaque value is installed only by this controller's context port.
        // Native subscription code retains it without inspecting GUI state.
        auto origin = std::static_pointer_cast<AdoptionOrigin>(entry.adoption_origin);
        if (!origin) {
            setWalletLoadError(tr("Wallet updates are unavailable. Restart the app to refresh the wallet list."));
            if (guard) m_load_subscription->retire();
            return;
        }
        addOrSelectWalletModel(entry.wallet, QString::fromStdString(entry.name), entry.select_wallet && entry.selection_revision == m_selection_revision, std::move(origin), entry.ingress_sequence);
        if (!guard || m_shutting_down) return;
    }
}

void WalletQmlController::flushDeferredPublications()
{
    if (m_shutting_down || m_adoption_depth || m_projecting_notifications) return;
    const auto command = std::exchange(m_deferred_command_publication, 0);
    const bool notifications = std::exchange(m_deferred_notification_publication, false);
    if (!command && !notifications) return;
    const QPointer<WalletQmlController> observer{this};
    m_services.post([observer, command, notifications] {
        if (!observer || observer->m_shutting_down) return;
        if (command) observer->publishWalletCommand(command);
        if (observer && !observer->m_shutting_down && notifications) observer->consumeWalletNotifications();
    });
}

std::shared_ptr<WalletQmlController::AdoptionOrigin> WalletQmlController::newAdoptionOrigin()
{
    const QPointer<WalletQmlController> observer{this};
    if (m_origin_sequence == std::numeric_limits<uint64_t>::max()) return {};
    const auto sequence = ++m_origin_sequence;
    const auto admission_event = m_adoption_clock->next();
    if (!admission_event) return {};
    const auto scheduler = backgroundScheduler();
    const auto state = m_retirement;
    const auto values = state->values;
    const auto post = m_services.post;
    const auto previous_admission = values->admitting_origin;
    values->admitting_origin = std::min(previous_admission, sequence);
    const auto admission_guard = qScopeGuard([state, values, previous_admission] {
        values->admitting_origin = previous_admission;
        if (state->values == values) state->pruneAdoptions();
    });
    // Keep unregister transport charged until GUI consumption. It owns only
    // metadata, never an idle original action capability or wallet borrower.
    auto registration = RetainAdoptionValue<uint64_t>(scheduler, 256, [sequence] { return std::make_unique<uint64_t>(sequence); });
    if (!registration || !observer || observer->m_shutting_down || state->values != values) return {};
    const std::weak_ptr<Retirement> weak = state;
    auto origin = RetainAdoptionValue<AdoptionOrigin>(scheduler, 512, [sequence, admission_event, weak, registration, post] {
        auto value = std::make_unique<AdoptionOrigin>();
        value->sequence = sequence;
        value->admission_event = admission_event;
        value->retired = [weak, registration, post] {
            post([weak, registration] {
                if (const auto owner = weak.lock(); owner && owner->values) {
                    owner->values->origins.erase(*registration);
                    owner->pruneAdoptions();
                }
            });
        };
        return value;
    });
    if (!observer || observer->m_shutting_down || state->values != values) return {};
    if (origin) values->origins.insert(sequence);
    return origin;
}
size_t WalletQmlController::adoptionFenceCount() const
{
    return m_retirement->values ? m_retirement->values->closed.size() : 0;
}
void WalletQmlController::updateNotificationContext()
{
    if (!m_load_subscription || m_shutting_down) return;
    auto origin = m_wallet_load_in_progress && m_command_origin ? m_command_origin : m_notification_origin;
    if (!origin) {
        m_load_subscription->rejectNotificationContext();
        return;
    }
    m_load_subscription->setNotificationContext(m_deferred_wallet_name, m_wallet_load_in_progress, m_selection_revision, std::move(origin));
}
void WalletQmlController::closeOpeningAdoption(WalletQmlModel* wallet)
{
    const QPointer<WalletQmlController> observer{this};
    const auto state = m_retirement;
    const auto values = state->values;
    if (!values) return;
    const auto found = values->adoptions.find(wallet);
    if (found == values->adoptions.end() || found->second->closed) return;
    const auto opening = found->second;
    const auto unloaded = opening->unloaded->load();
    const auto observed = unloaded ? unloaded : m_adoption_clock->next();
    const auto cutoff = observed ? observed : std::numeric_limits<uint64_t>::max();
    const auto origin_cutoff = m_origin_sequence;
    const QString name = wallet->name();
    opening->closed = true; // Fence before allocation or any reentrant signal.
    wallet->closeAdmission();
    if (!observer || observer->m_shutting_down || state->values != values) return;
    const auto units = static_cast<size_t>(name.size());
    const auto scheduler = backgroundScheduler();
    std::shared_ptr<Retirement::ClosedFence> fence;
    if (units <= (std::numeric_limits<size_t>::max() - 768) / 4 && m_close_fence_sequence != std::numeric_limits<uint64_t>::max()) {
        fence = RetainAdoptionValue<Retirement::ClosedFence>(scheduler, 768 + units * 4, [name, origin_cutoff, cutoff, opening] {
            return std::make_unique<Retirement::ClosedFence>(Retirement::ClosedFence{name, origin_cutoff, cutoff, opening});
        });
    }
    if (!observer || observer->m_shutting_down || state->values != values) return;
    if (fence && m_close_fence_sequence != std::numeric_limits<uint64_t>::max()) {
        const auto latest = values->closed.find(name);
        if (latest == values->closed.end() || latest->second->event_cutoff <= cutoff) values->closed.insert_or_assign(name, fence);
        values->close_order.emplace(std::pair{origin_cutoff, ++m_close_fence_sequence}, fence);
    } else {
        m_adoption_unavailable_event_cutoff = std::max(m_adoption_unavailable_event_cutoff, cutoff);
    }
    auto next_origin = newAdoptionOrigin();
    if (!observer || observer->m_shutting_down || state->values != values) return;
    m_notification_origin = std::move(next_origin);
    updateNotificationContext();
    if (observer && !observer->m_shutting_down) state->pruneAdoptions();
}
void WalletQmlController::adoptionUnavailable(bool closed)
{
    if (m_shutting_down) return;
    if (!m_publishing_command || m_publishing_command != m_command_id) {
        if (!closed) setWalletLoadError(tr("The wallet could not be shown. Try again after pending operations finish."));
        return;
    }
    m_adoption_state = closed ? WalletAdoptionState::AdoptionClosed : WalletAdoptionState::AdoptionUnavailable;
    m_adoption_error = closed ? tr("The wallet operation completed after this wallet was closed. Open the wallet again to continue.")
                              : tr("The wallet view is unavailable. Try again after pending operations finish.");
    // This is a view notice. The independent Native receipt is unchanged.
    const QString message = m_adoption_error;
    const QString name = walletOperationName();
    const auto operation = m_command_id;
    const QPointer<WalletQmlController> observer{this};
    setWalletLoadError(message);
    if (observer && !observer->m_shutting_down && operation == observer->m_command_id) Q_EMIT walletLoadErrorNotice(name, operation, message);
}

void WalletQmlController::handleLoadWallet(std::shared_ptr<interfaces::Wallet> wallet, const std::optional<QString>& captured_name, bool select_wallet)
{
    // Subscription effects already applied the context captured at ingress.
    // A later GUI command must not reinterpret an earlier notification.
    addOrSelectWalletModel(std::move(wallet), captured_name, select_wallet);
}

void WalletQmlController::initialize()
{
    if (m_initialized || m_initializing || m_initialization_failed || m_shutting_down) return;
    if (!m_retirement->retained) { m_initialization_failed = true; setWalletLoadError(tr("Wallet loading could not be started.")); return; }
    if (!m_open_wallet_model || !m_open_wallet_model->valid()) { m_initialization_failed = true; setWalletLoadError(tr("Wallet presentation capacity is unavailable.")); return; }
    // wallet_loader is not set when -disablewallet is passed; bail out.
    if (gArgs.GetBoolArg("-disablewallet", false)) return;
    m_initializing = true;
    const QPointer<WalletQmlController> guard{this};
    // Pinned Core notifies before AddWallet. Its initial capture is not a
    // coherent resynchronization capability. Seed once and retain notification
    // handles; the subtree wallet/node interfaces remain unchanged.
    m_load_subscription = std::make_shared<walletlife::LoadSubscription>(
        backgroundScheduler(),
        [node = &m_node](walletlife::LoadSubscription::Callback callback) {
            return node->walletLoader().handleLoadWallet(std::move(callback));
        },
        [node = &m_node] { return node->walletLoader().getWallets(); },
        [guard, post = m_services.post](walletlife::Update update) {
            post([guard, update] { if (guard) guard->handleLifecyclePublication(update); });
        }, [clock = m_adoption_clock] { return clock->next(); });
    updateNotificationContext();
    if (!m_notification_origin || !m_load_subscription->start()) {
        m_initializing = false;
        m_initialization_failed = true;
        setWalletLoadError(tr("Wallet loading could not be started."));
    }
}

void WalletQmlController::handleLifecyclePublication(walletlife::Update update)
{
    if (m_shutting_down) return;
    switch (update.kind) {
    case walletlife::Publication::Ready:
    case walletlife::Publication::InitialHandoff:
        finishInitialization();
        break;
    case walletlife::Publication::Changed:
        m_load_subscription->acknowledgeChange();
        consumeWalletNotifications();
        break;
    case walletlife::Publication::Failed:
    case walletlife::Publication::Retired: {
        const QPointer<WalletQmlController> guard{this};
        const bool initialized_changed = m_initialized;
        const QString error = update.reason == walletlife::FailureReason::IngressCapacity ?
            tr("Wallet discovery is unavailable because it could not keep up with wallet notifications. Restart the app to refresh the wallet list.") :
            update.kind == walletlife::Publication::Failed ? tr("Wallet loading failed.") :
            update.cleanup_failed ? tr("Wallet notification cleanup failed.") : tr("Wallet notifications are unavailable.");
        m_initializing = false;
        m_initialized = false; m_names_ready->store(false);
        m_initialization_failed = true;
        setWalletLoadError(error); if (!guard || m_shutting_down) return;
        if (initialized_changed) {
            Q_EMIT initializedChanged();
            if (!guard || m_shutting_down) return;
        }
        break;
    }
    }
}

void WalletQmlController::finishInitialization()
{
    if (m_shutting_down || m_initialization_failed || !m_initializing) return;
    const QPointer<WalletQmlController> guard{this};
    auto handoff = m_load_subscription->takeInitial();
    if (!handoff) return;
    if (!handoff->entries.empty()) {
        for (const auto& entry : handoff->entries) {
            auto adoption = RetainAdoptionValue<OpeningAdoption>(backgroundScheduler(), 512, [] { return std::make_unique<OpeningAdoption>(); });
            if (!guard || m_shutting_down) return;
            if (!adoption) {
                m_initialization_failed = true;
                m_initializing = false;
                setWalletLoadError(tr("Wallets could not be shown. Try again after pending operations finish."));
                if (guard) m_load_subscription->retire();
                return;
            }
            auto* wallet_model = new WalletQmlModel(WalletQmlModel::SharedWallet{entry.wallet, backgroundScheduler(), QString::fromStdString(entry.name), m_preferences, m_fee_policy, m_shutdown_gate, {m_adoption_clock, adoption->unloaded}}, &m_node, FeeReadServices{}, psbtfee::Services{}, backupServices());
            if (!registerWalletModel(wallet_model, adoption)) {
                if (!guard || m_shutting_down) return;
                retireWallet(wallet_model, false);
                m_initialization_failed = true;
                m_initializing = false;
                setWalletLoadError(tr("Wallets could not be shown. Try again after pending operations finish."));
                if (!guard || m_shutting_down) return;
                m_load_subscription->retire();
                return;
            }
            applyWalletDisplayName(wallet_model);
            m_wallets.push_back(wallet_model); m_wallet_positions.emplace(wallet_model, std::prev(m_wallets.end())); m_wallet_names.insert_or_assign(wallet_model->name(), wallet_model);
            if (m_names_registry) { std::lock_guard name_lock{m_names_registry->mutex}; m_names_registry->names.insert(wallet_model->name()); }
            m_open_wallet_model->addWallet(wallet_model); if (!guard || m_shutting_down) return;
            subscribeWalletInfo(wallet_model);
            Q_EMIT walletLoadStateChanged(QString::fromStdString(entry.name), WalletListModel::LoadState::Open, QString{});
            if (!guard || m_shutting_down) return;
            initializeWalletInfo(wallet_model);
            if (!guard || m_shutting_down) return;
        }
        // Background disposal publishes the next bounded presentation batch.
        return;
    }
    m_initializing = false;
    m_initialized = true; m_names_ready->store(true);
    if (!m_wallets.empty()) {
        m_selected_wallet = m_wallets.front();
        Q_EMIT openWalletsChanged();
        if (!guard || m_shutting_down) return;
        setWalletLoaded(true);
        if (!guard || m_shutting_down) return;
        setNoWalletsFound(false);
        if (!guard || m_shutting_down) return;
        Q_EMIT selectedWalletChanged();
        if (!guard || m_shutting_down) return;
    }
    refreshExternalSignerStatus();
    if (!guard || m_shutting_down) return;
    Q_EMIT initializedChanged();
    if (!guard || m_shutting_down) return;
    consumeWalletNotifications();
}

void WalletQmlController::setWalletLoaded(bool loaded)
{
    if (m_is_wallet_loaded != loaded) {
        m_is_wallet_loaded = loaded;
        Q_EMIT isWalletLoadedChanged();
    }
}

void WalletQmlController::setNoWalletsFound(bool no_wallets_found)
{
    if (m_no_wallets_found != no_wallets_found) {
        m_no_wallets_found = no_wallets_found;
        Q_EMIT noWalletsFoundChanged();
    }
}

void WalletQmlController::startWalletLoad(const QString& path, const QString& wallet_format)
{
    const QPointer<WalletQmlController> admission_observer{this};
    m_command_requested_target = path;
    if (m_shutting_down || m_command_state == WalletOperationState::WalletOperationPending) return;
    m_command_state = WalletOperationState::WalletOperationPending;
    clearWalletMigrationStatus(); if (!admission_observer || m_shutting_down) return; clearWalletLoadStatus(); if (!admission_observer || m_shutting_down) return;
    setWalletLoadInProgress(true); if (!admission_observer || m_shutting_down) return;
    const auto selection = m_admitting_command ? m_admission_selection_revision : m_selection_revision;
    const QPointer<WalletQmlController> guard{this};
    Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::Loading, {}); if (!guard || m_shutting_down) return;
    submitWalletCommand([node = &m_node, path, wallet_format] {
        CommandResult out;
        if (path.trimmed().isEmpty()) { out.error = tr("Choose a wallet to open."); return out; }
        QString format = wallet_format;
        try { out.reference = format.isEmpty() ? ResolveManagedWalletReference(*node, path, &format) : QDir::cleanPath(path); }
        catch (...) { out.error = tr("Wallet directory could not be checked."); return out; }
        if (out.reference.isEmpty()) { out.error = tr("The selected wallet is not available in the wallet directory."); return out; }
        if (format == "bdb") { out.needs_migration = true; return out; }
        std::vector<bilingual_str> warnings;
        auto wallet = node->walletLoader().loadWallet(out.reference.toStdString(), warnings);
        out.warnings = JoinWarnings(warnings);
        if (!wallet) out.error = QString::fromStdString(util::ErrorString(wallet).translated);
        else out.wallet = std::move(*wallet);
        describeCommandWallet(out, out.reference);
        return out;
    }, [this, path, selection](const CommandResult& out) {
        const QPointer<WalletQmlController> guard{this};
        setWalletLoadInProgress(false); if (!guard) return;
        setWalletLoadWarnings(out.warnings); if (!guard) return;
        if (out.needs_migration) { Q_EMIT walletMigrationRequired(out.reference); return; }
        if (!out.wallet || out.incomplete) {
            const auto error = out.error.isEmpty() ? tr("Wallet could not be opened.") : out.error;
            setWalletLoadError(error); if (!guard) return;
            Q_EMIT walletLoadErrorNotice(path, m_command_id, currentCommandErrorDescription()); if (!guard || m_shutting_down) return;
            Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::LoadError, error); return;
        }
        auto* wallet = addOrSelectWalletModel(out.wallet, out.name, selection == m_selection_revision); if (guard && wallet) Q_EMIT walletLoadSucceeded(wallet);
    }, [this, path](std::exception_ptr failure) {
        const QPointer<WalletQmlController> guard{this};
        setWalletLoadInProgress(false); if (!guard) return;
        const auto error = NotStarted(failure) ? tr("Wallet open could not be queued. No wallet was opened.")
            : tr("Wallet open outcome is uncertain. Check the wallet directory before retrying.");
        setWalletLoadError(error); if (!guard || m_shutting_down) return;
        Q_EMIT walletLoadErrorNotice(path, m_command_id, currentCommandErrorDescription()); if (guard && !m_shutting_down) Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::LoadError, error);
    }, CommandPayload({static_cast<size_t>(path.size()) * sizeof(QChar), static_cast<size_t>(wallet_format.size()) * sizeof(QChar)}));
}

void WalletQmlController::startWalletMigration(const QString& path, SecureString passphrase, secretinput::Captured captured_passphrase)
{
    const QPointer<WalletQmlController> admission_observer{this};
    m_command_requested_target = path;
    if (m_shutting_down || m_command_state == WalletOperationState::WalletOperationPending) { QmlUtil::ClearSecureString(passphrase); return; }
    m_command_state = WalletOperationState::WalletOperationPending;
    clearWalletLoadStatus(); if (!admission_observer || m_shutting_down) return; clearWalletMigrationStatus(); if (!admission_observer || m_shutting_down) return; setWalletMigrationInProgress(true); if (!admission_observer || m_shutting_down) return;
    const auto selection = m_admitting_command ? m_admission_selection_revision : m_selection_revision;
    const size_t payload_bytes = CommandPayload({secretinput::Bytes(captured_passphrase), passphrase.capacity(),
        static_cast<size_t>(path.size()) * sizeof(QChar)});
    submitWalletCommand([node = &m_node, path, passphrase = std::move(passphrase), captured_passphrase = std::move(captured_passphrase)]() mutable {
        const auto clear_secret = qScopeGuard([&] { QmlUtil::ClearSecureString(passphrase); });
        CommandResult out;
        if (path.trimmed().isEmpty()) { out.error = tr("Choose a wallet to update."); return out; }
        try {
            if (captured_passphrase) {
                passphrase = secretinput::Materialize(captured_passphrase);
                captured_passphrase.reset();
            }
            out.reference = ResolveManagedWalletReference(*node, path);
            if (out.reference.isEmpty()) { out.error = tr("The selected wallet is not available in the wallet directory."); return out; }
            if (passphrase.empty() && node->walletLoader().isEncrypted(out.reference.toStdString())) { out.needs_passphrase = true; return out; }
        } catch (...) { out.error = tr("Wallet migration details could not be checked. No migration was started."); return out; }
        auto result = node->walletLoader().migrateWallet(out.reference.toStdString(), passphrase, true);
        if (!result) { out.error = QString::fromStdString(util::ErrorString(result).translated); if (out.error.isEmpty()) out.error = tr("Wallet update failed."); }
        else { out.effect_succeeded = true; out.wallet = std::move(result->wallet); }
        describeCommandWallet(out, out.reference);
        return out;
    }, [this, path, selection](const CommandResult& out) {
        const QPointer<WalletQmlController> guard{this};
        setWalletMigrationInProgress(false); if (!guard) return;
        if (out.needs_passphrase) { Q_EMIT walletMigrationPassphraseRequired(out.reference); return; }
        if (!out.error.isEmpty()) { const auto revision = setWalletMigrationError(out.error); if (!guard || m_shutting_down || revision != m_migration_error_revision) return;
            Q_EMIT walletMigrationErrorNotice(path, m_migration_error_operation, m_migration_error_needs_passphrase, m_migration_error_display);
            if (guard && !m_shutting_down) Q_EMIT walletMigrationFailed();
            return; }
        if (out.wallet) {
            auto* adopted = addOrSelectWalletModel(out.wallet, out.name, selection == m_selection_revision);
            if (!guard || !adopted) return;
        }
        clearWalletMigrationStatus(); if (guard) Q_EMIT walletMigrationSucceeded();
    }, [this, path](std::exception_ptr failure) {
        const QPointer<WalletQmlController> guard{this};
        setWalletMigrationInProgress(false); if (!guard) return;
        const auto revision = setWalletMigrationError(NotStarted(failure)
            ? tr("Wallet update could not be queued. No wallet update was started.")
            : tr("Wallet update outcome is uncertain. Check the wallet directory before retrying."));
        if (!guard || m_shutting_down || revision != m_migration_error_revision) return;
        Q_EMIT walletMigrationErrorNotice(path, m_migration_error_operation, false, m_migration_error_display);
        if (guard && !m_shutting_down) Q_EMIT walletMigrationFailed();
    }, payload_bytes);
}

void WalletQmlController::setWalletCreateError(const QString& error)
{
    if (m_shutting_down || !commandPublicationCurrent()) return;
    if ((error.isEmpty() && m_wallet_create_error.isEmpty()) ||
        (error.size() == m_wallet_create_error.size() && error.constData() == m_wallet_create_error.constData())) return;
    std::shared_ptr<const void> owner;
    QString display;
    if (m_command && m_command_owner) {
        const auto actual = m_command->result();
        if (actual && actual->value && actual->value->error.size() == error.size() && actual->value->error.constData() == error.constData()) {
            owner = m_command_owner;
            std::lock_guard lock{m_command_receipt->mutex}; display = m_command_receipt->error_display;
        }
    }
    if (!owner && error.size() > 1024) {
        m_wallet_create_error = tr("Wallet error details could not be retained. Check the latest wallet operation before retrying.");
        display = m_wallet_create_error;
    } else {
        m_wallet_create_error = error;
        if (!owner) display = error;
    }
    m_create_error_owner = std::move(owner); m_create_error_display = std::move(display);
    const QPointer<WalletQmlController> observer{this};
    const auto original = m_wallet_create_error.constData();
    m_create_error_lines.setValue(m_wallet_create_error, m_create_error_owner);
    if (!observer || m_shutting_down || original != m_wallet_create_error.constData()) return;
    Q_EMIT walletCreateErrorChanged();
}

void WalletQmlController::setWalletLoadInProgress(bool in_progress)
{
    const QPointer<WalletQmlController> observer{this};
    const auto command = m_command_id;
    const bool changed = m_wallet_load_in_progress != in_progress;
    m_wallet_load_in_progress = in_progress;
    if (!in_progress) {
        auto origin = newAdoptionOrigin();
        if (!observer || observer->m_shutting_down || command != observer->m_command_id) return;
        m_notification_origin = std::move(origin);
    }
    updateNotificationContext();
    if (observer && !observer->m_shutting_down && command == observer->m_command_id && changed) Q_EMIT walletLoadInProgressChanged();
}

void WalletQmlController::setImportErrorServicesForTesting(execution::SnapshotServices services)
{
    if (m_shutting_down || m_import_error_query) return;
    if (services.scheduler) m_import_error_services.scheduler = std::move(services.scheduler);
    if (services.post) m_import_error_services.post = std::move(services.post);
}
void WalletQmlController::setWalletLoadError(const QString& error) { captureWalletLoadError(error); }
bool WalletQmlController::captureWalletLoadError(const QString& error)
{
    if (m_shutting_down || !commandPublicationCurrent() || !m_import_error_input) return false;
    if (!m_import_error_unavailable && ((error.isEmpty() && m_wallet_load_error.isEmpty()) ||
        (error.size() == m_wallet_load_error.size() && error.constData() == m_wallet_load_error.constData()))) return true;
    constexpr size_t MAX_FRAMES{128}, MAX_BYTES{16 * 1024 * 1024};
    const auto input = m_import_error_input;
    const auto revision = ++m_import_error_revision;
    if (m_import_error_query) m_import_error_query->take(); // Retire an older immediate transport failure before accepting a new origin.
    bool accepted{false};
    {
        std::lock_guard lock{input->mutex};
        const size_t bytes = error.size() <= qsizetype(MAX_BYTES / sizeof(QChar)) ? size_t(error.size()) * sizeof(QChar) : MAX_BYTES + 1;
        if (input->budget->frames < MAX_FRAMES && bytes <= MAX_BYTES - input->budget->bytes.load()) {
            ++input->budget->frames; input->budget->bytes += bytes;
            std::shared_ptr<const ImportErrorFrame> frame;
            try { frame = std::make_shared<ImportErrorFrame>(input->budget, error, revision); }
            catch (...) { --input->budget->frames; input->budget->bytes -= bytes; throw; }
            if (input->current) input->retired.emplace(input->current.get(), input->current);
            input->current = frame;
            // Native cannot retire an old frame until every GUI alias has
            // moved. No callback/signal runs while this bounded handoff locks.
            m_import_error_frame = frame; m_wallet_load_error = frame->error;
            m_import_error_title = error.isEmpty() ? QString{} : tr("This wallet couldn't be imported");
            m_import_error_display = error.isEmpty() ? QString{} : tr("Preparing wallet error details…");
            m_import_error_description = error.isEmpty() ? QString{} : tr("Preparing wallet error details…");
            m_import_error_help.clear(); m_import_error_projection.reset();
            accepted = true;
        }
    }
    const QPointer<WalletQmlController> observer{this};
    if (!accepted) {
        const bool hidden = m_import_error_unavailable;
        m_import_error_unavailable = true; m_import_error_pending = false; m_import_error_visibility_changed = true;
        m_import_error_projection_error = tr("Error details could not be retained. Retry showing the details after background work finishes.");
        m_import_error_title = tr("Wallet error details are unavailable");
        m_import_error_description = tr("Check the latest wallet operation before trying another wallet action.");
        m_import_error_help.clear(); m_import_error_projection.reset();
        m_import_error_retry_fixed.reset(); m_import_error_retry_owner.reset();
        if (error.size() <= 4096) m_import_error_retry_fixed = error; // Bounded local diagnostic, never a large GUI capture.
        else if (m_command && m_command_owner) {
            const auto actual = m_command->result();
            if (actual && actual->value && actual->value->error.size() == error.size() && actual->value->error.constData() == error.constData()) m_import_error_retry_owner = m_command_owner;
        }
        m_import_error_lines.setValue({}, {}); if (!observer || m_shutting_down || revision != m_import_error_revision) return false;
        prepareImportErrorProjection(); // Dispose older input frames on Native even while latest capture failed.
        if (!hidden) { Q_EMIT walletLoadErrorChanged(); if (!observer || m_shutting_down) return false; }
        Q_EMIT walletImportErrorChanged(); return false;
    }
    m_import_error_visibility_changed |= m_import_error_unavailable;
    m_import_error_unavailable = false; m_import_error_pending = true; m_import_error_projection_error.clear();
    m_import_error_retry_owner.reset(); m_import_error_retry_fixed.reset();
    m_import_error_lines.setValue({}, {}); if (!observer || m_shutting_down || revision != m_import_error_revision) return true;
    prepareImportErrorProjection();
    Q_EMIT walletImportErrorChanged(); return true;
}
bool WalletQmlController::retryWalletImportErrorPresentation()
{
    if (m_shutting_down) return false;
    if (!m_import_error_unavailable) {
        if (m_import_error_projection_error.isEmpty()) return false;
        m_import_error_pending = true; m_import_error_projection_error.clear(); prepareImportErrorProjection(); Q_EMIT walletImportErrorChanged(); return true;
    }
    const auto owner = m_import_error_retry_owner;
    if (owner && *owner) {
        const auto actual = (*owner)->result();
        if (actual && actual->value) return captureWalletLoadError(actual->value->error);
    }
    if (m_import_error_retry_fixed) { const auto fixed = *m_import_error_retry_fixed; return captureWalletLoadError(fixed); }
    return false; // Unknown source lifetime cannot be recreated as a false new native error.
}
void WalletQmlController::prepareImportErrorProjection()
{
    if (m_shutting_down || !m_import_error_input) return;
    if (!m_import_error_query) {
        const QPointer<WalletQmlController> observer{this};
        m_import_error_query = std::make_shared<execution::SnapshotQuery<std::shared_ptr<const ImportErrorProjection>>>(m_import_error_services.scheduler,
            [input = m_import_error_input, scheduler = m_import_error_services.scheduler] {
                std::shared_ptr<const ImportErrorFrame> frame, previous;
                std::map<const ImportErrorFrame*, std::shared_ptr<const ImportErrorFrame>> retired;
                { std::lock_guard lock{input->mutex}; frame = input->current; previous = input->notified; retired.swap(input->retired); }
                retired.clear(); // Every large retired last reference is released on Native.
                if (!frame) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
                try {
                    auto result = execution::RetainSnapshot<ImportErrorProjection>(scheduler, [&] {
                        auto value = std::make_unique<ImportErrorProjection>(); value->frame = frame; value->previous = previous; value->revision = frame->revision;
                        value->text = PrepareImportError(frame->error);
                        value->display = frame->error.left(1024);
                        value->details = frame->error;
                        if (!value->text.help.isEmpty() && value->text.help != frame->error.trimmed()) value->details.prepend(value->text.help + QStringLiteral("\n\n"));
                        value->changed = previous ? previous->error != frame->error : !frame->error.isEmpty();
                        return value;
                    });
                    if (result) return std::shared_ptr<const ImportErrorProjection>{std::move(result)};
                } catch (const std::exception&) { }
                // A failed projection carries only bounded scalar context. Its
                // actual large source remains in the Native input channel.
                auto failed = std::make_shared<ImportErrorProjection>(); failed->revision = frame->revision; failed->failed = true;
                return std::shared_ptr<const ImportErrorProjection>{std::move(failed)};
            }, [observer, post = m_import_error_services.post] { post([observer] { if (observer) observer->publishImportErrorProjection(); }); }, execution::Lane::Domain);
    }
    m_import_error_query->request();
}
void WalletQmlController::publishImportErrorProjection()
{
    if (m_shutting_down || !m_import_error_query) return;
    const auto result = m_import_error_query->take(); if (!result) return;
    if (!result->snapshot || !*result->snapshot) {
        if (m_import_error_unavailable) return;
        m_import_error_pending = false; m_import_error_projection_error = tr("Wallet error details could not be prepared. Retry showing the details.");
        Q_EMIT walletImportErrorChanged(); return;
    }
    const auto projection = *result->snapshot;
    if (m_import_error_unavailable || projection->revision != m_import_error_revision) return;
    if (projection->failed) {
        m_import_error_pending = false; m_import_error_projection_error = tr("Wallet error details could not be prepared. Retry showing the details.");
        Q_EMIT walletImportErrorChanged(); return;
    }
    {
        std::lock_guard lock{m_import_error_input->mutex};
        // A newer GUI acknowledgement can overtake an already-running read.
        // Recompute the Native equality decision instead of comparing text here.
        if (m_import_error_input->notified != projection->previous) { m_import_error_query->request(); return; }
        m_import_error_input->notified = projection->frame;
    }
    m_import_error_display = projection->display;
    m_import_error_title = projection->text.title; m_import_error_description = projection->text.description; m_import_error_help = projection->text.help;
    m_import_error_projection = projection; m_import_error_pending = false; m_import_error_projection_error.clear();
    const bool raw_changed = projection->changed || std::exchange(m_import_error_visibility_changed, false);
    const QPointer<WalletQmlController> observer{this};
    m_import_error_lines.setValue(projection->details, projection); if (!observer || m_shutting_down || projection->revision != m_import_error_revision) return;
    if (raw_changed) { Q_EMIT walletLoadErrorChanged(); if (!observer || m_shutting_down) return; }
    Q_EMIT walletImportErrorChanged();
}

void WalletQmlController::setWalletLoadWarnings(const QString& warnings)
{
    if (m_wallet_load_warnings != warnings) {
        m_wallet_load_warnings = warnings;
        Q_EMIT walletLoadWarningsChanged();
    }
}

void WalletQmlController::setWalletMigrationInProgress(bool in_progress)
{
    if (m_wallet_migration_in_progress != in_progress) {
        m_wallet_migration_in_progress = in_progress;
        Q_EMIT walletMigrationInProgressChanged();
    }
}

QAbstractItemModel* WalletQmlController::captureMigrationErrorDetails(quint64 operation, int slot)
{
    if (m_shutting_down || operation != m_migration_error_operation || (slot != 0 && slot != 1)) return nullptr;
    const auto revision = m_migration_error_revision;
    auto* lines = slot == 0 ? &m_migration_required_details : &m_migration_passphrase_details;
    const QPointer<WalletQmlController> observer{this};
    lines->setValue(m_wallet_migration_error, m_migration_error_owner);
    return observer && !m_shutting_down && revision == m_migration_error_revision && operation == m_migration_error_operation ? lines : nullptr;
}
quint64 WalletQmlController::setWalletMigrationError(const QString& error)
{
    if (m_shutting_down || !commandPublicationCurrent()) return m_migration_error_revision;
    if ((error.isEmpty() && m_wallet_migration_error.isEmpty()) ||
        (m_migration_error_operation == m_command_id && error.size() == m_wallet_migration_error.size() && error.constData() == m_wallet_migration_error.constData())) return m_migration_error_revision;
    std::shared_ptr<const void> owner;
    QString display;
    bool passphrase{false};
    if (m_command && m_command_owner) {
        const auto actual = m_command->result();
        if (actual && actual->value && actual->value->error.size() == error.size() && actual->value->error.constData() == error.constData()) {
            owner = m_command_owner;
            std::lock_guard lock{m_command_receipt->mutex};
            display = m_command_receipt->error_display; passphrase = m_command_receipt->error_needs_passphrase;
        }
    }
    // All non-command callers supply fixed bounded diagnostics. An unknown
    // large producer cannot lend unowned text to a GUI cache.
    if (!owner && error.size() > 1024) {
        m_wallet_migration_error = tr("Wallet error details could not be retained. Check the latest wallet operation before retrying.");
        display = m_wallet_migration_error;
    } else {
        m_wallet_migration_error = error;
        if (!owner) display = error;
    }
    m_migration_error_owner = std::move(owner); m_migration_error_display = std::move(display);
    m_migration_error_needs_passphrase = passphrase; m_migration_error_operation = m_command_id;
    const auto revision = ++m_migration_error_revision;
    Q_EMIT walletMigrationErrorChanged();
    return revision;
}

QString WalletQmlController::describeImportedWalletKeyScheme(interfaces::Wallet& imported_wallet) const
{
    return WalletQmlModel::keySchemeDisplayText(WalletQmlModel::keySchemeForWallet(imported_wallet));
}

void WalletQmlController::setLastImportedWalletInfo(const QString& wallet_name, const QString& key_scheme)
{
    if (m_last_imported_wallet_name == wallet_name &&
        m_last_imported_wallet_key_scheme == key_scheme) {
        return;
    }

    m_last_imported_wallet_name = wallet_name;
    m_last_imported_wallet_key_scheme = key_scheme;
    Q_EMIT lastImportedWalletInfoChanged();
}

void WalletQmlController::clearLastImportedWalletInfo()
{
    setLastImportedWalletInfo(QString(), QString());
}

void WalletQmlController::setExternalSignerStatus(bool path_configured, int signer_count, const QString& signer_name, const QString& error, const QString& suggested_name)
{
    if (m_external_signer_path_configured == path_configured &&
        m_external_signer_count == signer_count &&
        m_external_signer_name == signer_name &&
        m_external_signer_error == error &&
        m_suggested_external_signer_wallet_name == suggested_name) {
        return;
    }

    m_external_signer_path_configured = path_configured;
    m_external_signer_count = signer_count;
    m_external_signer_name = signer_name;
    m_external_signer_error = error;
    m_suggested_external_signer_wallet_name = suggested_name;
    Q_EMIT externalSignerStatusChanged();
}

void WalletQmlController::setWalletLocationOpenError(const QString& error)
{
    if (m_wallet_location_open_error == error) {
        return;
    }

    m_wallet_location_open_error = error;
    Q_EMIT walletLocationOpenErrorChanged();
}

void WalletQmlController::setOpenLocalPathFnForTesting(OpenLocalPathFn fn)
{
    m_open_local_path_fn = std::move(fn);
}
