// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletqmlcontroller.h>

#include <qml/models/walletqmlmodel.h>

#include <interfaces/node.h>
#include <support/allocators/secure.h>
#include <util/result.h>
#include <wallet/walletutil.h>
#include <util/threadnames.h>

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace {
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

bool NeedsWalletMigration(const bilingual_str& error)
{
    const QString original = QString::fromStdString(error.original);
    const QString translated = QString::fromStdString(error.translated);
    return (original.contains("legacy wallet", Qt::CaseInsensitive) ||
            translated.contains("legacy wallet", Qt::CaseInsensitive)) &&
           (original.contains("migrat", Qt::CaseInsensitive) ||
            translated.contains("migrat", Qt::CaseInsensitive));
}

bool IsBackupLikeFile(const QFileInfo& file_info)
{
    const QString file_name = file_info.fileName().toLower();
    return file_name.endsWith(".bak") || file_name.endsWith(".legacy.bak");
}
} // namespace

WalletQmlController::WalletQmlController(interfaces::Node& node, QObject *parent)
    : QObject(parent)
    , m_node(node)
    , m_empty_wallet(new WalletQmlModel(this))
    , m_selected_wallet(m_empty_wallet)
    , m_worker(new QObject)
    , m_worker_thread(new QThread(this))
{
    m_worker->moveToThread(m_worker_thread);
    m_worker_thread->start();
    QTimer::singleShot(0, m_worker, []() {
        util::ThreadRename("qml-walletctrl");
    });
}

WalletQmlController::~WalletQmlController()
{
    if (m_handler_load_wallet) {
        m_handler_load_wallet->disconnect();
    }
    m_worker_thread->quit();
    m_worker_thread->wait();
    delete m_worker;
    delete m_empty_wallet;
}

void WalletQmlController::setSelectedWallet(QString path)
{
    if (!m_wallets.empty()) {
        for (WalletQmlModel* wallet : m_wallets) {
            if (wallet->name() == path) {
                clearWalletLoadStatus();
                m_selected_wallet = wallet;
                Q_EMIT selectedWalletChanged();
                return;
            }
        }
    }

    startWalletLoad(path);
}

WalletQmlModel* WalletQmlController::selectedWallet() const
{
    return m_selected_wallet;
}

void WalletQmlController::unloadWallets()
{
    if (m_handler_load_wallet) {
        m_handler_load_wallet->disconnect();
    }
    m_selected_wallet = m_empty_wallet;
    Q_EMIT selectedWalletChanged();
    QMutexLocker locker(&m_wallets_mutex);
    for (WalletQmlModel* wallet : m_wallets) {
        delete wallet;
    }
    m_wallets.clear();
}

void WalletQmlController::createSingleSigWallet(const QString &name, const QString &passphrase)
{
    clearWalletLoadStatus();
    clearWalletMigrationStatus();
    const SecureString secure_passphrase{passphrase.toStdString()};
    const std::string wallet_name{name.toStdString()};
    auto wallet{m_node.walletLoader().createWallet(wallet_name, secure_passphrase, wallet::WALLET_FLAG_DESCRIPTORS, m_warning_messages)};
    QMutexLocker locker(&m_wallets_mutex);
    if (wallet) {
        m_selected_wallet = new WalletQmlModel(std::move(*wallet));
        m_wallets.push_back(m_selected_wallet);
        setNoWalletsFound(false);
        Q_EMIT selectedWalletChanged();
    } else {
        m_error_message = util::ErrorString(wallet);
    }
}

void WalletQmlController::importWallet(const QString& path)
{
    startWalletImport(path);
}

void WalletQmlController::clearWalletLoadStatus()
{
    setWalletLoadInProgress(false);
    setWalletLoadError(QString());
    setWalletLoadWarnings(QString());
}

void WalletQmlController::migrateWallet(const QString& path)
{
    startWalletMigration(path);
}

void WalletQmlController::clearWalletMigrationStatus()
{
    setWalletMigrationInProgress(false);
    setWalletMigrationError(QString());
}

QString WalletQmlController::normalizeWalletPath(const QString& path) const
{
    if (path.isEmpty()) {
        return {};
    }

    const QUrl url(path);
    QString normalized = url.isLocalFile() ? url.toLocalFile() : path;
    return QFileInfo(normalized).absoluteFilePath();
}

bool WalletQmlController::walletPathExists(const QString& path) const
{
    const QString normalized = normalizeWalletPath(path);
    return !normalized.isEmpty() && QFileInfo::exists(normalized);
}

QString WalletQmlController::inferWalletLoadTarget(const QString& normalized_path) const
{
    const QFileInfo selected_path(normalized_path);
    if (!selected_path.exists()) {
        return {};
    }

    const QString wallet_dir_path = QDir::cleanPath(
        QFileInfo(QString::fromStdString(m_node.walletLoader().getWalletDir())).absoluteFilePath());
    const QDir wallet_dir(wallet_dir_path);

    auto walletTargetFromDir = [&wallet_dir, &wallet_dir_path](const QString& dir_path) {
        const QString clean_dir_path = QDir::cleanPath(dir_path);
        const QString relative_path = wallet_dir.relativeFilePath(clean_dir_path);
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

    const QString file_name = selected_path.fileName();
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

QString WalletQmlController::resolveManagedWalletReference(const QString& path) const
{
    if (path.isEmpty()) {
        return {};
    }

    const QString candidate = QDir::cleanPath(path);

    // Wallet selector entries come from listWalletDir(), which reports wallet
    // names relative to -walletdir. Accept those names directly before trying
    // to interpret the value as a filesystem path.
    for (const auto& [wallet_path, info] : m_node.walletLoader().listWalletDir()) {
        Q_UNUSED(info);
        const QString listed_wallet = QDir::cleanPath(QString::fromStdString(wallet_path));
        if (listed_wallet == candidate) {
            return listed_wallet;
        }
    }

    const QString normalized_path = normalizeWalletPath(path);
    if (normalized_path.isEmpty() || !QFileInfo::exists(normalized_path)) {
        return {};
    }

    return inferWalletLoadTarget(normalized_path);
}

QString WalletQmlController::inferRestoreWalletName(const QString& normalized_path) const
{
    const QFileInfo selected_path(normalized_path);
    if (!selected_path.exists() || selected_path.isDir()) {
        return {};
    }

    QString wallet_name;
    const QString file_name = selected_path.fileName();

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

void WalletQmlController::startWalletImport(const QString& path)
{
    const QString normalized_path = normalizeWalletPath(path);
    clearWalletMigrationStatus();
    clearWalletLoadStatus();

    if (normalized_path.isEmpty()) {
        setWalletLoadError(tr("Choose a wallet backup file."));
        return;
    }

    if (!QFileInfo::exists(normalized_path)) {
        setWalletLoadError(tr("The selected wallet path does not exist."));
        return;
    }

    const QString restore_wallet_name = inferRestoreWalletName(normalized_path);

    m_wallet_load_requested = true;
    setWalletLoadInProgress(true);

    QTimer::singleShot(0, m_worker, [this, normalized_path, restore_wallet_name]() {
        std::vector<bilingual_str> warning_messages;
        // Import is intentionally modeled as restore-from-backup. The user is
        // selecting a wallet file to bring into managed wallet storage, not
        // asking the app to open an arbitrary wallet in place.
        auto wallet = m_node.walletLoader().restoreWallet(
            fs::PathFromString(normalized_path.toStdString()),
            restore_wallet_name.toStdString(),
            warning_messages);
        const QString warnings = JoinWarnings(warning_messages);

        if (!wallet) {
            const bilingual_str result_error = util::ErrorString(wallet);
            const QString error = QString::fromStdString(result_error.translated);
            QMetaObject::invokeMethod(this, [this, error, warnings]() {
                m_wallet_load_requested = false;
                setWalletLoadInProgress(false);
                setWalletLoadWarnings(warnings);
                setWalletLoadError(error.isEmpty() ? tr("Wallet import failed.") : error);
            });
            return;
        }

        QMetaObject::invokeMethod(this, [this, warnings]() {
            setWalletLoadWarnings(warnings);
        });
    });
}

void WalletQmlController::handleLoadWallet(std::unique_ptr<interfaces::Wallet> wallet)
{
    QMutexLocker locker(&m_wallets_mutex);
    if (!m_wallets.empty()) {
        QString name = QString::fromStdString(wallet->getWalletName());
        for (WalletQmlModel* wallet_model : m_wallets) {
            if (wallet_model->name() == name) {
                m_selected_wallet = wallet_model;
                Q_EMIT selectedWalletChanged();
                setWalletLoaded(true);
                setNoWalletsFound(false);
                if (m_wallet_load_requested) {
                    m_wallet_load_requested = false;
                    setWalletLoadInProgress(false);
                    Q_EMIT walletLoadSucceeded();
                }
                return;
            }
        }
    }

    auto wallet_model = new WalletQmlModel(std::move(wallet));
    wallet_model->moveToThread(this->thread());
    m_selected_wallet = wallet_model;
    m_wallets.push_back(m_selected_wallet);
    Q_EMIT selectedWalletChanged();
    setWalletLoaded(true);
    setNoWalletsFound(false);
    if (m_wallet_load_requested) {
        m_wallet_load_requested = false;
        setWalletLoadInProgress(false);
        Q_EMIT walletLoadSucceeded();
    }
}

void WalletQmlController::initialize()
{
    m_handler_load_wallet = m_node.walletLoader().handleLoadWallet([this](std::unique_ptr<interfaces::Wallet> wallet) {
        handleLoadWallet(std::move(wallet));
    });

    auto wallets = m_node.walletLoader().getWallets();
    for (auto& wallet : wallets) {
        m_wallets.push_back(new WalletQmlModel(std::move(wallet)));
    }
    if (!m_wallets.empty()) {
        m_selected_wallet = m_wallets.front();
        setWalletLoaded(true);
        Q_EMIT selectedWalletChanged();
    }

    if (m_node.walletLoader().listWalletDir().size() == 0) {
        setNoWalletsFound(true);
    } else {
        setNoWalletsFound(false);
    }

    m_initialized = true;
    Q_EMIT initializedChanged();
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

void WalletQmlController::startWalletLoad(const QString& path)
{
    clearWalletMigrationStatus();
    clearWalletLoadStatus();

    if (path.trimmed().isEmpty()) {
        setWalletLoadError(tr("Choose a wallet to open."));
        return;
    }

    // Wallet selection can arrive either as a wallet name from listWalletDir()
    // or as a concrete path under -walletdir. Resolve both into the wallet
    // reference expected by loadWallet() and migrateWallet().
    const QString load_target = resolveManagedWalletReference(path);
    if (load_target.isEmpty()) {
        setWalletLoadError(tr("The selected wallet is not available in the wallet directory."));
        return;
    }

    m_wallet_load_requested = true;
    setWalletLoadInProgress(true);

    QTimer::singleShot(0, m_worker, [this, load_target]() {
        std::vector<bilingual_str> warning_messages;
        // Loading is reserved for already-discovered wallet storage. Import
        // uses restoreWallet() through startWalletImport() instead.
        auto wallet = m_node.walletLoader().loadWallet(load_target.toStdString(), warning_messages);
        const QString warnings = JoinWarnings(warning_messages);

        if (!wallet) {
            const bilingual_str result_error = util::ErrorString(wallet);
            const QString error = QString::fromStdString(result_error.translated);
            const bool migration_required = !load_target.isEmpty() && NeedsWalletMigration(result_error);
            QMetaObject::invokeMethod(this, [this, error, warnings, migration_required, load_target]() {
                m_wallet_load_requested = false;
                setWalletLoadInProgress(false);
                if (migration_required) {
                    clearWalletLoadStatus();
                    Q_EMIT walletMigrationRequired(load_target);
                    return;
                }

                setWalletLoadWarnings(warnings);
                setWalletLoadError(error.isEmpty() ? tr("Wallet could not be opened.") : error);
            });
            return;
        }

        QMetaObject::invokeMethod(this, [this, warnings]() {
            setWalletLoadWarnings(warnings);
        });
    });
}

void WalletQmlController::startWalletMigration(const QString& path)
{
    clearWalletLoadStatus();
    clearWalletMigrationStatus();

    if (path.trimmed().isEmpty()) {
        setWalletMigrationError(tr("Choose a wallet to update."));
        Q_EMIT walletMigrationFailed();
        return;
    }

    const QString wallet_reference = resolveManagedWalletReference(path);
    if (wallet_reference.isEmpty()) {
        setWalletMigrationError(tr("The selected wallet is not available in the wallet directory."));
        Q_EMIT walletMigrationFailed();
        return;
    }

    setWalletMigrationInProgress(true);

    QTimer::singleShot(0, m_worker, [this, wallet_reference]() {
        const SecureString empty_passphrase;
        auto result = m_node.walletLoader().migrateWallet(wallet_reference.toStdString(), empty_passphrase);

        if (!result) {
            const QString error = QString::fromStdString(util::ErrorString(result).translated);
            QMetaObject::invokeMethod(this, [this, error]() {
                setWalletMigrationInProgress(false);
                setWalletMigrationError(error.isEmpty() ? tr("Wallet update failed.") : error);
                Q_EMIT walletMigrationFailed();
            });
            return;
        }

        QMetaObject::invokeMethod(this, [this, migration_result = std::move(*result)]() mutable {
            handleLoadWallet(std::move(migration_result.wallet));
            clearWalletMigrationStatus();
            Q_EMIT walletMigrationSucceeded();
        });
    });
}

void WalletQmlController::setWalletLoadInProgress(bool in_progress)
{
    if (m_wallet_load_in_progress != in_progress) {
        m_wallet_load_in_progress = in_progress;
        Q_EMIT walletLoadInProgressChanged();
    }
}

void WalletQmlController::setWalletLoadError(const QString& error)
{
    if (m_wallet_load_error != error) {
        m_wallet_load_error = error;
        Q_EMIT walletLoadErrorChanged();
    }
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

void WalletQmlController::setWalletMigrationError(const QString& error)
{
    if (m_wallet_migration_error != error) {
        m_wallet_migration_error = error;
        Q_EMIT walletMigrationErrorChanged();
    }
}
