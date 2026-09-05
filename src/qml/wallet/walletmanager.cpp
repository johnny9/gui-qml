// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletmanager.h>
#include <qml/wallet/walletsession.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <util/translation.h>

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>

struct WalletManager::Instance {
    // Destruction order matters: all borrowers disappear before the backend.
    std::unique_ptr<WalletSession> session;
    std::unique_ptr<WalletViewModel> view;
};

WalletManager::WalletManager(interfaces::Node& node, const QString& network, QObject* parent)
    : QObject(parent), m_node(node), m_network(network), m_executor(this), m_catalog(this), m_creation(*this, this)
{
    connect(&m_executor, &WalletOperationExecutor::drained, this, [this] {
        m_instances.clear();
        m_retired.clear();
        Q_EMIT drained();
    });
}

WalletManager::~WalletManager()
{
    if (m_load_handler) m_load_handler->disconnect();
    // The application normally drains before destroying the manager. Members
    // submitted to the executor hold their own backend until Core returns.
}

interfaces::WalletLoader& WalletManager::loader() const { return m_node.walletLoader(); }

QString WalletManager::canonicalIdentity(const QString& name) const
{
    const QString directory = QString::fromStdString(loader().getWalletDir());
    const QFileInfo file(QDir(directory).absoluteFilePath(name));
    const QString canonical = file.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(file.absoluteFilePath()) : canonical;
}

WalletViewModel* WalletManager::selectedWallet() const
{
    const auto found = m_instances.find(m_selected);
    return found == m_instances.end() ? nullptr : found->second->view.get();
}

void WalletManager::initialize()
{
    if (m_initialized || m_stopping) return;
    m_load_handler = loader().handleLoadWallet([this](std::unique_ptr<interfaces::Wallet> wallet) {
        auto shared = std::shared_ptr<interfaces::Wallet>(std::move(wallet));
        QMetaObject::invokeMethod(this, [this, shared = std::move(shared)] {
            if (m_stopping) return;
            const QString identity = canonicalIdentity(QString::fromStdString(shared->getWalletName()));
            // A matching worker result publishes this wallet only after setup.
            // Unrelated RPC loads update the catalog, never operation status.
            if (busy() && identity == m_deferred_identity) return;
            publish(shared);
            refresh();
        }, Qt::QueuedConnection);
    });
    for (auto& wallet : loader().getWallets()) publish(std::shared_ptr<interfaces::Wallet>(std::move(wallet)));
    m_initialized = true;
    refresh();
    Q_EMIT initializedChanged();
}

WalletViewModel* WalletManager::publish(std::shared_ptr<interfaces::Wallet> wallet)
{
    const QString identity = canonicalIdentity(QString::fromStdString(wallet->getWalletName()));
    const auto found = m_instances.find(identity);
    if (found != m_instances.end()) return found->second->view.get();
    auto instance = std::make_unique<Instance>();
    instance->session = std::make_unique<WalletSession>(std::move(wallet), m_next_session_id++, m_executor);
    // A Core load and immediate unload can both precede this queued GUI call.
    // Subscribe first, then reconcile membership, so an unload cannot fall in
    // the gap between taking a snapshot and installing the unload handler.
    bool still_loaded{false};
    for (auto& current : loader().getWallets()) {
        if (canonicalIdentity(QString::fromStdString(current->getWalletName())) == identity) still_loaded = true;
    }
    if (!still_loaded) return nullptr;
    instance->view = std::make_unique<WalletViewModel>(*instance->session, m_network);
    connect(instance->session.get(), &WalletSession::invalidated, this, [this, identity] { retire(identity); });
    connect(instance->session.get(), &WalletSession::actionBusyChanged, this, [this] {
        QTimer::singleShot(0, this, &WalletManager::collectRetired);
    });
    connect(instance->view->overview(), &WalletOverviewModel::changed, this, &WalletManager::refresh);
    WalletViewModel* view = instance->view.get();
    m_instances.emplace(identity, std::move(instance));
    // An explicit operation selects its primary result after setup completes.
    // Companion/unrelated load notifications must not select a transient result.
    if (m_selected.isEmpty() && !busy()) {
        m_selected = identity;
        Q_EMIT selectedWalletChanged();
    }
    return view;
}

void WalletManager::refresh()
{
    if (!m_initialized || m_stopping) return;
    std::map<QString, WalletListModel::Row> rows;
    for (const auto& [name, format] : loader().listWalletDir()) {
        const QString qname = QString::fromStdString(name);
        rows.emplace(canonicalIdentity(qname), WalletListModel::Row{qname, QString::fromStdString(format), false,
            qname.isEmpty() ? tr("Default wallet") : qname});
    }
    for (const auto& [identity, instance] : m_instances) {
        rows[identity] = {instance->session->name(), QStringLiteral("sqlite"), true, instance->view->overview()->displayName()};
    }
    std::vector<WalletListModel::Row> projection;
    for (auto& [identity, row] : rows) projection.push_back(std::move(row));
    m_catalog.replace(std::move(projection));
}

quint64 WalletManager::beginOperation(const QString& name, WalletOperationExecutor::Work work, WalletOperationExecutor::Completion completion)
{
    if (!m_initialized || m_stopping || busy()) return 0;
    m_operation_id = m_next_operation_id++;
    m_deferred_identity = canonicalIdentity(name);
    const quint64 operation_id = m_operation_id;
    Q_EMIT busyChanged();
    const bool accepted = m_executor.submit(std::move(work), [this, operation_id, completion = std::move(completion)](WalletOperationResult result) mutable {
        result.operation_id = operation_id;
        if (!m_stopping && result.code == WalletOperationResult::Success && result.wallet) {
            auto view = publish(result.wallet);
            if (view) {
                m_selected = canonicalIdentity(view->session().name());
                Q_EMIT selectedWalletChanged();
            } else {
                result.code = WalletOperationResult::Unavailable;
                result.error = tr("The wallet was unloaded before the operation completed.");
                result.wallet.reset();
            }
        }
        m_operation_id = 0;
        m_deferred_identity.clear();
        Q_EMIT busyChanged();
        refresh();
        if (!m_stopping) completion(std::move(result));
    });
    if (!accepted) {
        m_operation_id = 0;
        m_deferred_identity.clear();
        Q_EMIT busyChanged();
        return 0;
    }
    return operation_id;
}

void WalletManager::selectWallet(const QString& name)
{
    if (!m_initialized || m_stopping) return;
    const QString identity = canonicalIdentity(name);
    if (m_instances.contains(identity)) {
        m_selected = identity;
        m_load_error.clear();
        Q_EMIT selectedWalletChanged();
        Q_EMIT loadStatusChanged();
        return;
    }
    for (const auto& [entry, format] : loader().listWalletDir()) {
        if (canonicalIdentity(QString::fromStdString(entry)) == identity && format == "bdb") {
            Q_EMIT migrationRequired(name);
            return;
        }
    }
    m_load_error.clear();
    auto* wallet_loader = &loader();
    if (!beginOperation(name, [wallet_loader, name] {
        std::vector<bilingual_str> warnings;
        auto loaded = wallet_loader->loadWallet(name.toStdString(), warnings);
        if (!loaded) return WalletOperationResult::failure(WalletOperationResult::CoreError, QString::fromStdString(util::ErrorString(loaded).translated));
        WalletOperationResult result;
        result.wallet = std::shared_ptr<interfaces::Wallet>(std::move(*loaded));
        for (const auto& warning : warnings) result.warnings.append(QString::fromStdString(warning.translated));
        return result;
    }, [this](WalletOperationResult result) {
        m_load_error = result.error;
        Q_EMIT loadStatusChanged();
    })) m_load_error = tr("Another wallet operation is in progress.");
    Q_EMIT loadStatusChanged();
}

void WalletManager::retire(const QString& identity)
{
    const auto found = m_instances.find(identity);
    if (found == m_instances.end()) return;
    m_retired.push_back(std::move(found->second));
    m_instances.erase(found);
    if (m_selected == identity) {
        m_selected = m_instances.empty() ? QString{} : m_instances.begin()->first;
        Q_EMIT selectedWalletChanged();
    }
    // QML and in-flight signal delivery can release their old references first.
    QTimer::singleShot(0, this, &WalletManager::collectRetired);
    refresh();
}

void WalletManager::collectRetired()
{
    std::erase_if(m_retired, [](const auto& instance) { return !instance->session->actionBusy(); });
}

void WalletManager::closeWallet(const QString& name)
{
    if (!m_initialized || m_stopping) return;
    const auto found = m_instances.find(canonicalIdentity(name));
    if (found == m_instances.end()) return;
    auto* session = found->second->session.get();
    // The serialized worker closes only after earlier session actions return.
    if (!beginOperation(name, [backend = session->m_wallet] {
        backend->remove();
        return WalletOperationResult{};
    }, [this](WalletOperationResult result) {
        m_load_error = result.error;
        Q_EMIT loadStatusChanged();
    })) {
        m_load_error = tr("Another wallet operation is in progress.");
        Q_EMIT loadStatusChanged();
        return;
    }
    session->invalidate();
}

void WalletManager::shutdown()
{
    if (m_stopping) return;
    m_stopping = true;
    if (m_load_handler) m_load_handler->disconnect();
    while (!m_instances.empty()) m_instances.begin()->second->session->invalidate();
    m_executor.drain();
}
