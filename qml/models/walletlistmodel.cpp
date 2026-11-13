// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/guipresentation.h>
#include <qml/models/walletlistmodel.h>
#include <qml/backendexecutor.h>
#include <qml/bitcoinunits.h>
#include <qml/retainedsnapshot.h>
#include <interfaces/node.h>
#include <QSettings>
#include <QThread>
#include <QPointer>
#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>

struct WalletListModel::Item {
    QString name, format, display_name, balance_text, section, error;
    bool from_wallet_dir{false}, balance_known{false};
    qint64 balance{0};
    int key_scheme{-1};
    LoadState load_state{LoadState::Closed};
};
struct WalletListModel::Input {
    enum class Kind { Directory, Load, Unit, Info, Names } kind;
    Input(Kind value) : kind(value) {}
    QString name, error;
    LoadState load{LoadState::Closed};
    qint64 balance{0};
    int scheme{-1}, unit{0};
    bool known{false};
    quint64 revision{0};
};
struct WalletListModel::InputStore {
    std::mutex mutex;
    std::deque<Input> pending;
    std::shared_ptr<const Snapshot> published;
};
struct WalletListModel::Snapshot {
    std::vector<Item> rows;
    std::vector<std::pair<int, QList<int>>> notices;
    quint64 revision{0}, directory_epoch{0};
    bool loaded{false}, structure_changed{false};
    QString error;
};
struct WalletListModel::State {
    struct Information { qint64 balance{0}; int scheme{-1}; bool known{false}; };
    std::vector<std::pair<std::string, std::string>> directory;
    std::map<QString, Information> information;
    QSet<QString> open;
    QString loading, failed, error;
    bool loaded{false};
    int unit{0};
    quint64 revision{0}, directory_epoch{0};
    void Apply(const Input& input, interfaces::Node& node) {
        revision = input.revision;
        switch (input.kind) {
        case Input::Kind::Directory: directory = node.walletLoader().listWalletDir(); loaded = true; ++directory_epoch; break;
        case Input::Kind::Names: break;
        case Input::Kind::Unit: unit = input.unit; break;
        case Input::Kind::Info: information[input.name] = {input.balance, input.scheme, input.known}; break;
        case Input::Kind::Load:
            if (input.load == LoadState::Loading) { loading = input.name; failed.clear(); error.clear(); }
            else if (input.load == LoadState::LoadError) { loading.clear(); failed = input.name; error = input.error; }
            else {
                if (input.load == LoadState::Open) open.insert(input.name); else open.remove(input.name);
                if (loading == input.name) loading.clear();
                if (failed == input.name) { failed.clear(); error.clear(); }
            }
            break;
        }
    }
    std::unique_ptr<Snapshot> Project(const std::shared_ptr<const Snapshot>& previous) const {
        auto result = std::make_unique<Snapshot>();
        result->revision = revision; result->directory_epoch = directory_epoch; result->loaded = loaded;
        if (loaded) {
            QSet<QString> listed;
            for (const auto& [path, format] : directory) {
                Item item; item.name = QString::fromStdString(path); item.format = QString::fromStdString(format); item.from_wallet_dir = true;
                listed.insert(item.name); result->rows.push_back(std::move(item));
            }
            for (const auto& name : open) if (!listed.contains(name)) { Item item; item.name = name; result->rows.push_back(std::move(item)); }
        }
        if (result->rows.size() > size_t(std::numeric_limits<int>::max())) throw std::length_error("Wallet catalog exceeds Qt row capacity");
        QSettings settings;
        for (auto& item : result->rows) {
            const auto name = item.name.trimmed();
            item.display_name = settings.value(QStringLiteral("walletDisplayNames/%1").arg(name)).toString().trimmed();
            if (item.display_name.isEmpty()) item.display_name = name;
            if (const auto found = information.find(item.name); found != information.end()) {
                item.balance = found->second.balance; item.balance_known = found->second.known; item.key_scheme = found->second.scheme;
            }
            if (item.balance_known) item.balance_text = QmlBitcoinUnits::formatForDisplay(QmlBitcoinUnits::fromDisplayUnit(unit), item.balance);
            item.load_state = failed == item.name ? LoadState::LoadError : loading == item.name ? LoadState::Loading : open.contains(item.name) ? LoadState::Open : LoadState::Closed;
            item.section = open.contains(item.name) ? QStringLiteral("open") : QStringLiteral("closed");
            if (failed == item.name) item.error = error;
        }
        if (settings.status() != QSettings::NoError) throw std::runtime_error("Wallet display names could not be read");
        std::stable_sort(result->rows.begin(), result->rows.end(), [&](const auto& a, const auto& b) {
            const bool a_open = open.contains(a.name), b_open = open.contains(b.name);
            if (a_open != b_open) return a_open;
            for (auto sensitivity : {Qt::CaseInsensitive, Qt::CaseSensitive}) {
                const int order = QString::compare(a.name, b.name, sensitivity); if (order) return order < 0;
            }
            const int order = QString::compare(a.format, b.format, Qt::CaseInsensitive);
            return order ? order < 0 : QString::compare(a.format, b.format, Qt::CaseSensitive) < 0;
        });
        result->structure_changed = !previous || previous->rows.size() != result->rows.size();
        for (size_t row = 0; !result->structure_changed && row < result->rows.size(); ++row) {
            const auto& a = previous->rows[row]; const auto& b = result->rows[row];
            result->structure_changed = a.name != b.name || a.format != b.format || a.from_wallet_dir != b.from_wallet_dir;
        }
        if (previous && !result->structure_changed) for (size_t row = 0; row < result->rows.size(); ++row) {
            const auto& a = previous->rows[row]; const auto& b = result->rows[row]; QList<int> roles;
            if (a.display_name != b.display_name) roles.append({Qt::DisplayRole, DisplayNameRole});
            if (a.load_state != b.load_state) roles.append(LoadStateRole);
            if (a.section != b.section) roles.append(WalletSectionRole);
            if (a.error != b.error) roles.append(ErrorMessageRole);
            if (a.balance != b.balance || a.balance_known != b.balance_known || a.key_scheme != b.key_scheme) roles.append({BalanceRole, BalanceSatoshiRole, BalanceKnownRole, KeySchemeKindRole});
            else if (a.balance_text != b.balance_text) roles.append(BalanceRole);
            if (!roles.empty()) result->notices.emplace_back(static_cast<int>(row), std::move(roles));
        }
        return result;
    }
};
WalletListModel::WalletListModel(interfaces::Node& node, QObject* parent) : WalletListModel(node, execution::SnapshotServices{}, parent) {}
WalletListModel::WalletListModel(interfaces::Node& node, execution::SnapshotServices services, QObject* parent)
    : QAbstractListModel(parent), m_services(std::move(services)), m_node(node)
{
    if (!m_services.scheduler) { m_owned_executor = std::make_shared<BackendExecutor>(execution::Limits::Bounded()); m_services.scheduler = m_owned_executor->scheduler(); }
    if (!m_services.post) {
        m_bridge = presentation::MakeGuiObject();
        m_services.post = [bridge = m_bridge](auto turn) { QMetaObject::invokeMethod(bridge.get(), std::move(turn), Qt::QueuedConnection); };
    }
    m_input = execution::RetainSnapshot<InputStore>(m_services.scheduler, [] { return std::make_unique<InputStore>(); });
    m_state = execution::RetainSnapshot<State>(m_services.scheduler, [] { return std::make_unique<State>(); });
    if (!m_state || !m_input) m_error = tr("Wallet catalog capacity is unavailable.");
}
WalletListModel::~WalletListModel() { closeAdmission(); if (m_owned_executor) m_owned_executor->shutdown(); }
void WalletListModel::closeAdmission()
{
    m_stopping = true; m_pending = false;
    if (m_query) m_query->close();
    m_query.reset();
    if (m_input) { std::lock_guard lock{m_input->mutex}; m_input->published.reset(); }
    m_visible.reset(); m_state.reset(); m_input.reset();
}
void WalletListModel::listWalletDir() { enqueue({Input::Kind::Directory}); }
void WalletListModel::setWalletLoadState(const QString& name, LoadState state, const QString& error)
{
    if (name.isEmpty()) return;
    Input input{Input::Kind::Load}; input.name = name; input.load = state; input.error = error; enqueue(std::move(input));
}
void WalletListModel::setDisplayUnit(int unit) { Input input{Input::Kind::Unit}; input.unit = unit; enqueue(std::move(input)); }
void WalletListModel::setWalletInfo(const QString& name, qint64 balance, int keySchemeKind, bool balanceKnown)
{
    if (name.isEmpty()) return;
    Input input{Input::Kind::Info}; input.name = name; input.balance = balance; input.scheme = keySchemeKind; input.known = balanceKnown; enqueue(std::move(input));
}
void WalletListModel::refreshDisplayNames() { enqueue({Input::Kind::Names}); }
void WalletListModel::enqueue(Input input)
{
    if (m_stopping || !m_state || !m_input) return;
    input.revision = ++m_revision;
    { std::lock_guard lock{m_input->mutex}; m_input->pending.push_back(std::move(input)); }
    m_pending = true;
    if (!m_query) {
        const QPointer<WalletListModel> guard{this};
        m_query = std::make_shared<execution::SnapshotQuery<std::shared_ptr<const Snapshot>>>(m_services.scheduler,
            [state = m_state, input = m_input, scheduler = m_services.scheduler, node = &m_node] {
                std::deque<Input> pending; std::shared_ptr<const Snapshot> published;
                { std::lock_guard lock{input->mutex}; pending.swap(input->pending); published = input->published; }
                QString error;
                for (const auto& event : pending) {
                    try { state->Apply(event, *node); }
                    catch (...) { state->revision = event.revision; error = WalletListModel::tr("Wallet directory could not be read."); }
                }
                auto snapshot = execution::RetainSnapshot<Snapshot>(scheduler, [&] {
                    if (!error.isEmpty()) { auto failed = std::make_unique<Snapshot>(); failed->error = error; failed->revision = state->revision; return failed; }
                    return state->Project(published);
                });
                if (!snapshot) throw std::runtime_error("Wallet catalog projection capacity is unavailable");
                return std::shared_ptr<const Snapshot>{std::move(snapshot)};
            }, [guard, post = m_services.post] { post([guard] { if (guard) guard->publishSnapshot(); }); });
    }
    m_query->request();
    Q_EMIT projectionChanged();
}
void WalletListModel::publishSnapshot()
{
    if (m_stopping || !m_query) return;
    const auto result = m_query->take(); if (!result) return;
    const auto next = result->snapshot ? *result->snapshot : std::shared_ptr<const Snapshot>{};
    if (next && next->revision != m_revision) return;
    const QPointer<WalletListModel> guard{this};
    m_error = next ? next->error : tr("Wallet catalog could not be refreshed.");
    if (!next || !m_error.isEmpty()) { m_pending = false; Q_EMIT projectionChanged(); return; }
    const bool count_changed = rowCount() != int(next->rows.size());
    if (next->structure_changed) { beginResetModel(); if (!guard) return; }
    m_visible = next;
    { std::lock_guard lock{m_input->mutex}; m_input->published = next; }
    if (next->structure_changed) { endResetModel(); if (!guard) return; }
    const bool first = !m_wallet_dir_loaded && next->loaded;
    m_wallet_dir_loaded = next->loaded;
    if (next->directory_epoch != m_directory_epoch || first || count_changed) {
        m_directory_epoch = next->directory_epoch;
        Q_EMIT walletListChanged(rowCount() > 0); if (!guard || m_stopping) return;
    }
    if (first) { Q_EMIT walletDirLoadedChanged(); if (!guard || m_stopping) return; }
    m_notice = 0; publishNotices(next->revision);
    if (guard) Q_EMIT projectionChanged();
}
void WalletListModel::publishNotices(quint64 revision)
{
    if (m_stopping || !m_visible || m_visible->revision != revision) return;
    const auto snapshot = m_visible; const QPointer<WalletListModel> guard{this};
    for (int budget = 0; budget < 32 && m_notice < snapshot->notices.size(); ++budget) {
        const auto& [row, roles] = snapshot->notices[m_notice++];
        Q_EMIT dataChanged(index(row), index(row), roles);
        if (!guard || m_stopping || m_visible != snapshot) return;
    }
    if (m_notice < snapshot->notices.size()) {
        try { m_services.post([guard, revision] { if (guard) guard->publishNotices(revision); }); }
        catch (...) { m_error = tr("Wallet catalog presentation could not be delivered. Refresh to retry."); m_pending = false; Q_EMIT projectionChanged(); }
    }
    else if (revision == m_revision) { m_pending = false; Q_EMIT projectionChanged(); }
}
int WalletListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() || !m_visible ? 0 : int(m_visible->rows.size()); }
QVariant WalletListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& item = m_visible->rows[index.row()];
    switch (role) {
    case Qt::DisplayRole: case DisplayNameRole: return item.display_name;
    case NameRole: return item.name;
    case FormatRole: return item.format;
    case LoadStateRole: return static_cast<int>(item.load_state);
    case ErrorMessageRole: return item.error;
    case BalanceRole: return item.balance_text;
    case BalanceSatoshiRole: return item.balance;
    case BalanceKnownRole: return item.balance_known;
    case WalletSectionRole: return item.section;
    case KeySchemeKindRole: return item.key_scheme;
    }
    return {};
}
QHash<int, QByteArray> WalletListModel::roleNames() const
{
    return {{NameRole, "name"}, {FormatRole, "format"}, {DisplayNameRole, "displayName"}, {LoadStateRole, "loadState"},
        {ErrorMessageRole, "errorMessage"}, {BalanceRole, "balance"}, {BalanceSatoshiRole, "balanceSatoshi"},
        {BalanceKnownRole, "balanceKnown"}, {KeySchemeKindRole, "keySchemeKind"}, {WalletSectionRole, "walletSection"}};
}
