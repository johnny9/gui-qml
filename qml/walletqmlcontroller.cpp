// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletqmlcontroller.h>
#include <qml/backendexecutor.h>

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
#include <wallet/scriptpubkeyman.h>
#include <wallet/wallet.h>
#include <util/translation.h>
#include <util/threadnames.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUrl>

struct WalletLoadNotifications {
    std::mutex mutex;
    std::atomic_bool stopping{false};
    std::vector<std::pair<QString, std::unique_ptr<interfaces::Wallet>>> wallets;

    std::vector<std::pair<QString, std::unique_ptr<interfaces::Wallet>>> takePendingWallets()
    {
        const std::lock_guard lock{mutex};
        return std::exchange(wallets, {});
    }
};

namespace {
QString NormalizeWalletPath(const QString& path)
{
    if (path.isEmpty()) return {};
    const QUrl url(path);
    const QString local = url.isLocalFile() ? url.toLocalFile() : path;
    return QDir::cleanPath(QDir::isAbsolutePath(local) ? local : QDir::current().filePath(local));
}

struct WalletLoadResult {
    std::unique_ptr<interfaces::Wallet> wallet;
    QString name;
    QString key_scheme;
    QString warnings;
    QString error;
    bool migration_required{false};
    bool passphrase_required{false};
};

struct WalletCatalog {
    QStringList names;
    QHash<QString, QString> aliases;
};

WalletCatalog ReadWalletCatalog(interfaces::Node& node)
{
    WalletCatalog result;
    QSettings settings;
    for (const auto& [name, format] : node.walletLoader().listWalletDir()) {
        result.names.append(QString::fromStdString(name));
    }
    settings.beginGroup(QStringLiteral("walletDisplayNames"));
    for (const QString& key : settings.allKeys()) {
        const QString alias = settings.value(key).toString().trimmed();
        if (!alias.isEmpty()) result.aliases.insert(key, alias);
    }
    return result;
}

QString WalletError(const bilingual_str& error)
{
    return QString::fromStdString(error.translated.empty() ? error.original : error.translated);
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
    const QString file_name = file_info.fileName().toLower();
    return file_name.endsWith(".bak") || file_name.endsWith(".legacy.bak");
}

bool ErrorContains(const QString& error, const QString& needle)
{
    return error.contains(needle, Qt::CaseInsensitive);
}
} // namespace

WalletQmlController::WalletQmlController(interfaces::Node& node, QObject *parent)
    : QObject(parent)
    , m_node(node)
    , m_empty_wallet(new WalletQmlModel(&node, this))
    , m_selected_wallet(m_empty_wallet)
    , m_wallet_command_executor(std::make_shared<BackendExecutor>())
    , m_notification_bridge(new QObject, [](QObject* bridge) {
        if (QThread::currentThread() == bridge->thread()) delete bridge;
        else bridge->deleteLater();
    })
    , m_load_notifications(std::make_shared<WalletLoadNotifications>())
    , m_open_local_path_fn([](const QString& path) {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    })
{
    m_empty_wallet->setNode(&m_node);
    m_wallet_command_executor->setObjectName(QStringLiteral("wallet-commands"));
    connect(m_wallet_command_executor.get(), &BackendExecutor::drained, this, [this] {
        m_controller_drained = true;
        checkShutdownFinished();
    });

}

WalletQmlController::~WalletQmlController()
{
    // Application shutdown waits for walletsDrained before destroying the node.
    beginShutdown();
    m_retiring_wallets.remove(m_empty_wallet);
    qDeleteAll(m_retiring_wallets);
    m_retiring_wallets.clear();
    delete m_empty_wallet;
}

void WalletQmlController::setSelectedWallet(QString path, QString wallet_format)
{
    if (m_retiring_wallet_names.contains(path)) {
        const QString error = tr("This wallet is still closing.");
        setWalletLoadError(error);
        Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::LoadError, error);
        return;
    }
    if (!m_initialized) {
        setWalletLoadError(tr("Wallets are still loading. Try again in a moment."));
        return;
    }

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

    startWalletLoad(path, wallet_format);
}

bool WalletQmlController::isWalletOpen(const QString& path)
{
    if (path.trimmed().isEmpty()) {
        return false;
    }

    QMutexLocker locker(&m_wallets_mutex);
    for (WalletQmlModel* wallet : m_wallets) {
        if (wallet->name() == path) {
            return true;
        }
    }
    return false;
}

void WalletQmlController::publishWalletInfo(WalletQmlModel* wallet_model)
{
    if (!wallet_model || !wallet_model->walletStateReady()) return;
    Q_EMIT walletInfoChanged(
        wallet_model->name(),
        wallet_model->balanceSatoshi(),
        static_cast<int>(wallet_model->keySchemeKind()));
}

void WalletQmlController::publishOpenWalletsInfo()
{
    std::vector<WalletQmlModel*> snapshot;
    {
        QMutexLocker locker(&m_wallets_mutex);
        snapshot = m_wallets;
    }
    for (WalletQmlModel* wallet_model : snapshot) {
        publishWalletInfo(wallet_model);
    }
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
}

QString WalletQmlController::homePath() const
{
    return QDir::homePath();
}

bool WalletQmlController::openSelectedWalletLocation()
{
    clearWalletLocationOpenError();
    if (m_shutting_down || !m_selected_wallet || m_selected_wallet == m_empty_wallet) {
        setWalletLocationOpenError(tr("No wallet file is available to view."));
        return false;
    }
    const QString name = m_selected_wallet->name();
    return m_wallet_command_executor->submit(this, [node = &m_node, name]() -> std::pair<QString, QString> {
        QString path = name;
        if (!QDir::isAbsolutePath(path)) path = QDir(QString::fromStdString(node->walletLoader().getWalletDir())).filePath(path);
        const QFileInfo location(path);
        if (!location.exists()) return {{}, tr("Wallet file not found: %1").arg(path)};
        return {location.isDir() ? location.absoluteFilePath() : location.absolutePath(), {}};
    }, [this](const auto& result) {
        if (!result.second.isEmpty()) setWalletLocationOpenError(result.second);
        else if (!m_open_local_path_fn(result.first)) setWalletLocationOpenError(tr("Could not open wallet file location."));
    }, [this](std::exception_ptr) { setWalletLocationOpenError(tr("Could not find the wallet file location.")); });
}

void WalletQmlController::clearWalletLocationOpenError()
{
    setWalletLocationOpenError({});
}

void WalletQmlController::closeWallet(const QString& path)
{
    if (!m_initialized) {
        setWalletLoadError(tr("Wallets are still loading. Try again in a moment."));
        return;
    }

    if (path.trimmed().isEmpty()) {
        return;
    }

    clearWalletLoadStatus();
    clearWalletMigrationStatus();

    WalletQmlModel* wallet_to_close{nullptr};

    {
        QMutexLocker locker(&m_wallets_mutex);
        const auto wallet_it = std::find_if(m_wallets.begin(), m_wallets.end(), [&](WalletQmlModel* wallet) {
            return wallet->name() == path;
        });
        if (wallet_it == m_wallets.end()) {
            return;
        }

        wallet_to_close = *wallet_it;
    }

    retireWallet(wallet_to_close, true);
}

QString WalletQmlController::walletDisplayName(const QString& path) const
{
    const QString key = path.trimmed();
    return m_display_names.value(key, key);
}

bool WalletQmlController::setWalletDisplayName(const QString& path, const QString& display_name)
{
    const QString key = path.trimmed();
    if (key.isEmpty() || m_shutting_down) return false;
    const QString alias = display_name.trimmed();
    return m_wallet_command_executor->submit(this, [key, alias] {
        QSettings settings;
        const QString setting = QStringLiteral("walletDisplayNames/%1").arg(key);
        const QVariant previous = settings.value(setting);
        if (alias.isEmpty() || alias == key) settings.remove(setting);
        else settings.setValue(setting, alias);
        settings.sync();
        if (settings.status() == QSettings::NoError) return true;
        if (previous.isValid()) settings.setValue(setting, previous);
        else settings.remove(setting);
        return false;
    }, [this, key, alias](bool success) {
        if (success) {
            if (alias.isEmpty() || alias == key) m_display_names.remove(key);
            else m_display_names[key] = alias;
            for (auto* wallet : m_wallets) applyWalletDisplayName(wallet);
            Q_EMIT walletDisplayNamesChanged();
        } else setWalletLoadError(tr("The wallet name could not be saved."));
        Q_EMIT walletDisplayNameSaved(success);
    }, [this](std::exception_ptr) {
        setWalletLoadError(tr("The wallet name could not be saved."));
        Q_EMIT walletDisplayNameSaved(false);
    });
}

WalletQmlModel* WalletQmlController::selectedWallet() const
{
    return m_selected_wallet;
}

void WalletQmlController::unloadWallets()
{
    beginShutdown();
}

void WalletQmlController::beginShutdown()
{
    if (m_shutting_down) return;
    m_shutting_down = true;
    m_load_notifications->stopping = true;
    m_initialized = false;
    Q_EMIT initializedChanged();
    const auto wallets = m_wallets;
    for (auto* wallet : wallets) retireWallet(wallet, false);
    connect(m_empty_wallet, &WalletQmlModel::shutdownFinished, this, [this] {
        m_empty_wallet_drained = true;
        checkShutdownFinished();
    });
    m_empty_wallet->beginShutdown();
    m_notification_cleanup_executor = std::make_unique<BackendExecutor>();
    m_notification_cleanup_executor->setObjectName(QStringLiteral("wallet-notifications"));
    connect(m_notification_cleanup_executor.get(), &BackendExecutor::drained, this, [this] {
        m_notifications_drained = true;
        checkShutdownFinished();
    });
    m_notification_cleanup_executor->submit(this, [notifications = m_load_notifications] {
        auto wallets_to_release = notifications->takePendingWallets();
        wallets_to_release.clear();
    }, [] {});
    m_notification_cleanup_executor->shutdown();
    m_wallet_command_executor->submit(this, [handler = std::move(m_handler_load_wallet)]() mutable {
        if (handler) handler->disconnect();
        handler.reset();
    }, [] {});
    m_wallet_command_executor->shutdown();
}

void WalletQmlController::retireWallet(WalletQmlModel* model, bool remove)
{
    if (!model || m_retiring_wallets.contains(model)) return;
    const QString name = model->name();
    m_retiring_wallets.insert(model);
    m_retiring_wallet_names.insert(name);
    removeWalletModel(model);
    connect(model, &WalletQmlModel::shutdownFinished, this, [this, model, name] {
        m_retiring_wallets.remove(model);
        m_retiring_wallet_names.remove(name);
        Q_EMIT walletLoadStateChanged(name, WalletListModel::LoadState::Closed, {});
        model->deleteLater();
        checkShutdownFinished();
    });
    model->beginShutdown(remove);
}

void WalletQmlController::checkShutdownFinished()
{
    if (m_shutting_down && m_controller_drained && m_notifications_drained && m_empty_wallet_drained && m_retiring_wallets.empty() && !m_shutdown_complete) {
        m_shutdown_complete = true;
        Q_EMIT walletsDrained();
    }
}

void WalletQmlController::registerWalletModel(WalletQmlModel* wallet_model)
{
    connect(wallet_model, &WalletQmlModel::walletUnloaded, this, [this, wallet_model]() {
        retireWallet(wallet_model, false);
    }, Qt::QueuedConnection);
}

void WalletQmlController::removeWalletModel(WalletQmlModel* wallet_model)
{
    if (!wallet_model || wallet_model == m_empty_wallet) {
        return;
    }

    QString wallet_name;
    WalletQmlModel* next_selected_wallet{nullptr};
    {
        QMutexLocker locker(&m_wallets_mutex);
        const auto wallet_it = std::find(m_wallets.begin(), m_wallets.end(), wallet_model);
        if (wallet_it == m_wallets.end()) {
            return;
        }

        wallet_name = wallet_model->name();
        for (WalletQmlModel* wallet : m_wallets) {
            if (wallet != wallet_model) {
                next_selected_wallet = wallet;
                break;
            }
        }
        m_wallets.erase(wallet_it);
    }

    if (m_selected_wallet == wallet_model) {
        m_selected_wallet = next_selected_wallet ? next_selected_wallet : m_empty_wallet;
        Q_EMIT selectedWalletChanged();
    }

    Q_EMIT walletLoadStateChanged(wallet_name,
                                  WalletListModel::LoadState::Closing,
                                  QString{});
    setWalletLoaded(next_selected_wallet != nullptr);
    // The retirement callback deletes the model after its workers drain.
}

WalletQmlModel* WalletQmlController::addOrSelectWalletModel(std::unique_ptr<interfaces::Wallet> wallet, const QString& name)
{
    if (!wallet) {
        return nullptr;
    }

    bool selected_existing_wallet{false};
    {
        QMutexLocker locker(&m_wallets_mutex);
        if (!m_wallets.empty()) {
            for (WalletQmlModel* wallet_model : m_wallets) {
                if (wallet_model->name() == name) {
                    m_selected_wallet = wallet_model;
                    selected_existing_wallet = true;
                    publishWalletInfo(wallet_model);
                    break;
                }
            }
        }
    }
    if (selected_existing_wallet) {
        m_wallet_command_executor->submit(this, [wallet = std::move(wallet)] {}, [] {});
        applyWalletDisplayName(m_selected_wallet);
        Q_EMIT walletLoadStateChanged(m_selected_wallet->name(),
                                      WalletListModel::LoadState::Open,
                                      QString{});
        Q_EMIT selectedWalletChanged();
        setWalletLoaded(true);
        setNoWalletsFound(false);
        return m_selected_wallet;
    }

    auto wallet_model = new WalletQmlModel(std::move(wallet), &m_node, nullptr, name);
    wallet_model->moveToThread(this->thread());
    registerWalletModel(wallet_model);
    {
        QMutexLocker locker(&m_wallets_mutex);
        applyWalletDisplayName(wallet_model);
        m_selected_wallet = wallet_model;
        m_wallets.push_back(m_selected_wallet);
    }
    subscribeWalletInfo(wallet_model);
    Q_EMIT walletLoadStateChanged(name,
                                  WalletListModel::LoadState::Open,
                                  QString{});
    publishWalletInfo(wallet_model);
    Q_EMIT selectedWalletChanged();
    setWalletLoaded(true);
    setNoWalletsFound(false);
    return wallet_model;
}

void WalletQmlController::createWalletAsync(const QString& name, SecureString passphrase,
    uint64_t wallet_creation_flags, WalletLoadAction load_action, bool report_create_error, WalletSetupFn setup_wallet)
{
    if (m_shutting_down) return;
    const QString wallet_name = name.trimmed();
    m_deferred_wallet_name = wallet_name;
    setWalletLoadInProgress(true);
    m_wallet_command_executor->submit(this, [node = &m_node, wallet_name, passphrase = std::move(passphrase),
        wallet_creation_flags, setup_wallet = std::move(setup_wallet)]() mutable {
        WalletLoadResult result;
        const auto catalog = ReadWalletCatalog(*node);
        for (const auto& existing : catalog.names) {
            if (existing.compare(wallet_name, Qt::CaseInsensitive) == 0 ||
                catalog.aliases.value(existing).compare(wallet_name, Qt::CaseInsensitive) == 0) {
                result.error = tr("A wallet with this name already exists");
                return result;
            }
        }
        std::vector<bilingual_str> warnings;
        auto wallet = node->walletLoader().createWallet(wallet_name.toStdString(), passphrase, wallet_creation_flags, warnings);
        QmlUtil::ClearSecureString(passphrase);
        result.warnings = JoinWarnings(warnings);
        if (!wallet) {
            result.error = WalletError(util::ErrorString(wallet));
        } else {
            if (setup_wallet) result.error = setup_wallet(**wallet);
            if (!result.error.isEmpty()) (*wallet)->remove();
            else {
                result.name = QString::fromStdString((*wallet)->getWalletName());
                result.wallet = std::move(*wallet);
            }
        }
        return result;
    }, [this, report_create_error, load_action](WalletLoadResult result) {
        m_deferred_wallet_name.clear();
        setWalletLoadWarnings(result.warnings);
        setWalletLoadInProgress(false);
        if (!result.wallet) {
            const QString error = result.error.isEmpty() ? tr("Wallet creation failed.") : result.error;
            if (report_create_error) setWalletCreateError(error);
            else setWalletLoadError(error);
            return;
        }
        addOrSelectWalletModel(std::move(result.wallet), result.name);
        refreshWalletCatalog();
        if (load_action == WalletLoadAction::Load) Q_EMIT walletLoadSucceeded();
        else Q_EMIT walletCreateSucceeded();
    }, [this, report_create_error](std::exception_ptr) {
        m_deferred_wallet_name.clear();
        setWalletLoadInProgress(false);
        if (report_create_error) setWalletCreateError(tr("Wallet creation failed."));
        else setWalletLoadError(tr("Wallet creation failed."));
    });
}

void WalletQmlController::createSingleSigWallet(const QString &name, const QString &passphrase)
{
    if (m_wallet_load_in_progress) {
        return;
    }
    clearWalletCreateStatus();
    clearWalletLoadStatus();
    clearWalletMigrationStatus();
    if (!m_initialized) {
        setWalletCreateError(tr("Wallets are still loading. Try again in a moment."));
        return;
    }

    const QString name_error = walletNameAvailabilityError(name);
    if (!name_error.isEmpty()) {
        setWalletCreateError(name_error);
        return;
    }

    SecureString secure_passphrase{QmlUtil::SecureStringFromQString(passphrase)};
    createWalletAsync(name.trimmed(),
                      std::move(secure_passphrase),
                      wallet::WALLET_FLAG_DESCRIPTORS,
                      WalletLoadAction::Create,
                      /*report_create_error=*/true);
}

bool WalletQmlController::createExternalSignerWallet(const QString& name)
{
    clearWalletLoadStatus();
    clearWalletMigrationStatus();
    if (!m_initialized) {
        setWalletLoadError(tr("Wallets are still loading. Try again in a moment."));
        return false;
    }

    const QString wallet_name = name.trimmed();
    const QString name_error = walletNameAvailabilityError(wallet_name);
    if (!name_error.isEmpty()) {
        setWalletLoadError(name_error);
        return false;
    }

    refreshExternalSignerStatus();
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
    createWalletAsync(wallet_name,
                      SecureString{},
                      flags,
                      WalletLoadAction::Load,
                      /*report_create_error=*/false);
    return true;
}


void WalletQmlController::createWatchOnlyWallet(const QString &name, const QString &xpub)
{
    if (m_wallet_load_in_progress) {
        return;
    }
    clearWalletLoadStatus();
    clearWalletMigrationStatus();
    if (!m_initialized) {
        setWalletLoadError(tr("Wallets are still loading. Try again in a moment."));
        return;
    }

    const std::string xpub_str = xpub.trimmed().toStdString();
    CExtPubKey ext_pubkey = DecodeExtPubKey(xpub_str);
    if (!ext_pubkey.pubkey.IsValid()) {
        setWalletLoadError(tr("Invalid extended public key."));
        return;
    }

    const QString name_error = walletNameAvailabilityError(name);
    if (!name_error.isEmpty()) {
        setWalletLoadError(name_error);
        return;
    }

    const uint64_t creation_flags = wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS |
                                    wallet::WALLET_FLAG_DESCRIPTORS |
                                    wallet::WALLET_FLAG_BLANK_WALLET;
    std::vector<std::pair<std::string, bool>> descriptors = {
        {"wpkh(" + xpub_str + "/0/*)", /*internal=*/false},
        {"wpkh(" + xpub_str + "/1/*)", /*internal=*/true},
    };

    createWalletAsync(name.trimmed(),
                      SecureString{},
                      creation_flags,
                      WalletLoadAction::Create,
                      /*report_create_error=*/false,
                      [descriptors = std::move(descriptors)](interfaces::Wallet& wallet) {
        int descriptors_added = 0;
        wallet::CWallet* raw_wallet = wallet.wallet();
        if (!raw_wallet) {
            return WalletQmlController::tr("Failed to import descriptors into watch-only wallet.");
        }

        LOCK(raw_wallet->cs_wallet);
        for (const auto& [desc_str, internal] : descriptors) {
            FlatSigningProvider keys;
            std::string error;
            auto parsed = Parse(desc_str, keys, error, /*require_checksum=*/false);
            if (parsed.empty()) {
                continue;
            }
            wallet::WalletDescriptor w_desc(
                std::move(parsed.at(0)),
                TicksSinceEpoch<std::chrono::seconds>(Now<NodeSeconds>()),
                /*range_start=*/0,
                /*range_end=*/0,
                /*next_index=*/0);
            auto spk_manager_res = raw_wallet->AddWalletDescriptor(w_desc, keys, /*label=*/"", internal);
            if (spk_manager_res) {
                raw_wallet->AddActiveScriptPubKeyMan(
                    spk_manager_res.value().get().GetID(),
                    OutputType::BECH32,
                    internal);
                ++descriptors_added;
            }
        }
        raw_wallet->ConnectScriptPubKeyManNotifiers();

        // A watch-only wallet needs both external and internal descriptors;
        // succeeding with only one would leave a half-created wallet.
        if (descriptors_added != static_cast<int>(descriptors.size())) {
            return WalletQmlController::tr("Failed to import descriptors into watch-only wallet.");
        }
        return QString{};
    });
}

void WalletQmlController::importWallet(const QString& path)
{
    if (!m_initialized) {
        setWalletLoadError(tr("Wallets are still loading. Try again in a moment."));
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
    setWalletLoadError(QString());
    setWalletLoadWarnings(QString());
}

void WalletQmlController::migrateWallet(const QString& path, const QString& passphrase)
{
    if (!m_initialized) {
        setWalletMigrationError(tr("Wallets are still loading. Try again in a moment."));
        return;
    }
    startWalletMigration(path, QmlUtil::SecureStringFromQString(passphrase));
}

void WalletQmlController::clearWalletMigrationStatus()
{
    setWalletMigrationError(QString());
}

bool WalletQmlController::validateXpub(const QString& xpub) const
{
    CExtPubKey ext_pubkey = DecodeExtPubKey(xpub.trimmed().toStdString());
    return ext_pubkey.pubkey.IsValid();
}

void WalletQmlController::requestOpenWalletSettings()
{
    Q_EMIT openWalletSettingsRequested();
}

void WalletQmlController::refreshExternalSignerStatus()
{
    const QString signer_path = QString::fromStdString(
        SettingToString(m_node.getPersistentSetting("signer"), "")).trimmed();
    const bool path_configured = !signer_path.isEmpty();
    if (path_configured) {
        m_node.forceSetting("signer", signer_path.toStdString());
    } else {
        m_node.forceSetting("signer", common::SettingsValue{});
    }
    int signer_count = 0;
    QString signer_name;
    QString error;

    try {
        auto signers = m_node.listExternalSigners();
        signer_count = static_cast<int>(signers.size());
        if (signer_count == 1) {
            signer_name = QString::fromStdString(signers.front()->getName());
        } else if (signer_count > 1) {
            error = tr("More than one external signer was found. Connect only one device.");
        }
    } catch (const std::runtime_error&) {
        error = tr("The signer command did not return valid output. Check that the path is correct.");
    }

    setExternalSignerStatus(path_configured, signer_count, signer_name, error);
}

void WalletQmlController::requestOpenReceive()
{
    Q_EMIT openReceiveRequested();
}

void WalletQmlController::requestClosePaymentRequestDetail()
{
    Q_EMIT closePaymentRequestDetailRequested();
}

QString WalletQmlController::normalizeWalletPath(const QString& path) const
{
    if (path.isEmpty()) {
        return {};
    }

    const QUrl url(path);
    QString normalized = url.isLocalFile() ? url.toLocalFile() : path;
    return QDir::cleanPath(QDir::isAbsolutePath(normalized) ? normalized : QDir::current().filePath(normalized));
}

bool WalletQmlController::walletNameExists(const QString& name) const
{
    const QString candidate = name.trimmed();
    for (const QString& real_name : m_catalog_names) {
        if (real_name.compare(candidate, Qt::CaseInsensitive) == 0 ||
            walletDisplayName(real_name).compare(candidate, Qt::CaseInsensitive) == 0) return true;
    }
    for (const auto* wallet : m_wallets) {
        if (wallet->name().compare(candidate, Qt::CaseInsensitive) == 0 ||
            wallet->displayName().compare(candidate, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

QString WalletQmlController::walletNameAvailabilityError(const QString& name) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return tr("Enter a wallet name.");
    }

    if (walletNameExists(trimmed)) {
        return tr("A wallet with this name already exists");
    }

    return {};
}

QString WalletQmlController::walletImportErrorTitle() const
{
    const QString error = m_wallet_load_error.trimmed();
    if (error.isEmpty()) {
        return {};
    }

    if (ErrorContains(error, "legacy wallet") || ErrorContains(error, "migrat")) {
        return tr("This wallet needs to be migrated");
    }
    if (ErrorContains(error, "does not exist")) {
        return tr("We couldn't find that file");
    }
    if (ErrorContains(error, "already exists")) {
        return tr("A wallet with this name already exists");
    }
    if (ErrorContains(error, "verification failed") || ErrorContains(error, "not supported")) {
        return tr("This wallet type is not supported");
    }
    return tr("This wallet couldn't be imported");
}

QString WalletQmlController::walletImportErrorDescription() const
{
    const QString error = m_wallet_load_error.trimmed();
    if (error.isEmpty()) {
        return {};
    }

    if (ErrorContains(error, "legacy wallet") || ErrorContains(error, "migrat")) {
        return tr("The selected backup appears to come from a legacy wallet format. It needs to be migrated before it can be used here.");
    }
    if (ErrorContains(error, "does not exist")) {
        return tr("The selected file is no longer available at that location. Choose the backup file again and retry.");
    }
    if (ErrorContains(error, "already exists")) {
        return tr("Importing this backup would create a wallet that already exists in your wallet directory. Remove or rename the existing wallet first, then try again.");
    }
    if (ErrorContains(error, "verification failed") || ErrorContains(error, "not supported")) {
        return tr("The file looks like a wallet backup, but this application could not verify or open it in a supported format.");
    }
    return tr("The selected file could not be restored as a wallet. Check that it is a valid wallet backup, then try another file.");
}

QString WalletQmlController::walletImportErrorHelpText() const
{
    const QString error = m_wallet_load_error.trimmed();
    if (error.isEmpty()) {
        return {};
    }

    if (ErrorContains(error, "legacy wallet") || ErrorContains(error, "migrat")) {
        return tr("If this is a legacy wallet backup, migrate it with the wallet migration tool before importing it here.");
    }
    return error;
}

QString WalletQmlController::inferWalletLoadTarget(interfaces::Node& node, const QString& normalized_path)
{
    const QFileInfo selected_path(normalized_path);
    if (!selected_path.exists()) {
        return {};
    }

    const QString wallet_dir_path = QDir::cleanPath(
        QFileInfo(QString::fromStdString(node.walletLoader().getWalletDir())).absoluteFilePath());
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

QString WalletQmlController::resolveManagedWalletReference(interfaces::Node& node, const QString& path, QString* wallet_format)
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

    const QString load_target = inferWalletLoadTarget(node, normalized_path);
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

QString WalletQmlController::inferRestoreWalletName(const QString& normalized_path)
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
    if (m_shutting_down || m_wallet_load_in_progress || m_wallet_migration_in_progress) return;
    const QString normalized_path = NormalizeWalletPath(path);
    clearWalletMigrationStatus();
    clearWalletLoadStatus();
    clearLastImportedWalletInfo();
    if (normalized_path.isEmpty()) {
        setWalletLoadError(tr("Choose a wallet backup file."));
        return;
    }
    setWalletLoadInProgress(true);
    m_wallet_command_executor->submit(this, [node = &m_node, normalized_path] {
        WalletLoadResult result;
        if (!QFileInfo::exists(normalized_path)) {
            result.error = tr("The selected wallet path does not exist.");
            return result;
        }
        const QString name = inferRestoreWalletName(normalized_path);
        std::vector<bilingual_str> warnings;
        auto wallet = node->walletLoader().restoreWallet(fs::PathFromString(normalized_path.toStdString()),
            name.toStdString(), warnings, true);
        result.warnings = JoinWarnings(warnings);
        if (!wallet) result.error = WalletError(util::ErrorString(wallet));
        else {
            result.name = QString::fromStdString((*wallet)->getWalletName());
            result.key_scheme = describeImportedWalletKeyScheme(**wallet);
            result.wallet = std::move(*wallet);
        }
        return result;
    }, [this](WalletLoadResult result) {
        setWalletLoadInProgress(false);
        setWalletLoadWarnings(result.warnings);
        if (!result.wallet) {
            setWalletLoadError(result.error.isEmpty() ? tr("Wallet import failed.") : result.error);
            return;
        }
        setLastImportedWalletInfo(result.name, result.key_scheme);
        addOrSelectWalletModel(std::move(result.wallet), result.name);
        refreshWalletCatalog();
        Q_EMIT walletImportSucceeded();
    }, [this](std::exception_ptr) {
        setWalletLoadInProgress(false);
        setWalletLoadError(tr("Wallet import failed."));
    });
}

void WalletQmlController::consumeWalletNotifications()
{
    if (m_shutting_down) return;
    auto loaded_wallets = m_load_notifications->takePendingWallets();
    for (auto& [name, wallet] : loaded_wallets) handleLoadWallet(std::move(wallet), name);
}

void WalletQmlController::handleLoadWallet(std::unique_ptr<interfaces::Wallet> wallet, const QString& name)
{
    if (m_shutting_down || m_retiring_wallet_names.contains(name) || (!m_deferred_wallet_name.isEmpty() && name == m_deferred_wallet_name)) {
        m_wallet_command_executor->submit(this, [wallet = std::move(wallet)] {}, [] {});
        return;
    }
    addOrSelectWalletModel(std::move(wallet), name);
}

void WalletQmlController::initialize()
{
    if (m_initialized || m_initializing || m_shutting_down) return;
    m_initializing = true;
    struct InitialWallets {
        std::unique_ptr<interfaces::Handler> handler;
        std::vector<WalletLoadResult> wallets;
        WalletCatalog catalog;
        bool disabled{false};
    };
    const QPointer<WalletQmlController> guard{this};
    m_wallet_command_executor->submit(this, [node = &m_node, guard, bridge = m_notification_bridge, notifications = m_load_notifications] {
        InitialWallets result;
        result.disabled = gArgs.GetBoolArg("-disablewallet", false);
        if (result.disabled) return result;
        result.handler = node->walletLoader().handleLoadWallet([guard, bridge, notifications](std::unique_ptr<interfaces::Wallet> wallet) {
            if (notifications->stopping) return;
            const QString name = QString::fromStdString(wallet->getWalletName());
            {
                std::lock_guard lock(notifications->mutex);
                if (notifications->stopping) return;
                notifications->wallets.emplace_back(name, std::move(wallet));
            }
            QMetaObject::invokeMethod(bridge.get(), [guard] {
                if (guard) guard->consumeWalletNotifications();
            }, Qt::QueuedConnection);
        });
        for (auto& wallet : node->walletLoader().getWallets()) {
            WalletLoadResult item;
            item.name = QString::fromStdString(wallet->getWalletName());
            item.wallet = std::move(wallet);
            result.wallets.push_back(std::move(item));
        }
        result.catalog = ReadWalletCatalog(*node);
        return result;
    }, [this](InitialWallets result) {
        m_initializing = false;
        if (result.disabled) return;
        m_handler_load_wallet = std::move(result.handler);
        m_catalog_names = result.catalog.names;
        m_display_names = result.catalog.aliases;
        for (auto& item : result.wallets) addOrSelectWalletModel(std::move(item.wallet), item.name);
        setNoWalletsFound(m_catalog_names.isEmpty() && m_wallets.empty());
        if (!m_wallets.empty()) {
            m_selected_wallet = m_wallets.front();
            Q_EMIT selectedWalletChanged();
        }
        m_initialized = true;
        Q_EMIT walletCatalogChanged();
        Q_EMIT initializedChanged();
        refreshExternalSignerStatus();
    }, [this](std::exception_ptr) {
        m_initializing = false;
        setWalletLoadError(tr("Wallet discovery failed."));
    });
}

void WalletQmlController::refreshWalletCatalog()
{
    if (m_shutting_down) return;
    m_wallet_command_executor->submit(this, [node = &m_node] { return ReadWalletCatalog(*node); }, [this](WalletCatalog result) {
        m_catalog_names = std::move(result.names);
        m_display_names = std::move(result.aliases);
        Q_EMIT walletCatalogChanged();
        Q_EMIT walletDisplayNamesChanged();
    }, [this](std::exception_ptr) { setWalletLoadError(tr("Wallet discovery failed.")); });
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
    if (m_shutting_down || m_wallet_load_in_progress || m_wallet_migration_in_progress) return;
    if (m_retiring_wallet_names.contains(path)) {
        setWalletLoadError(tr("This wallet is still closing."));
        return;
    }
    clearWalletMigrationStatus();
    clearWalletLoadStatus();
    if (path.trimmed().isEmpty()) {
        setWalletLoadError(tr("Choose a wallet to open."));
        return;
    }
    setWalletLoadInProgress(true);
    Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::Loading, QString{});
    m_wallet_command_executor->submit(this, [node = &m_node, path, wallet_format] {
        WalletLoadResult result;
        QString format = wallet_format;
        result.name = format.isEmpty() ? resolveManagedWalletReference(*node, path, &format) : QDir::cleanPath(path);
        if (result.name.isEmpty()) {
            result.error = tr("The selected wallet is not available in the wallet directory.");
            return result;
        }
        if (format == QStringLiteral("bdb")) {
            result.migration_required = true;
            return result;
        }
        std::vector<bilingual_str> warnings;
        auto wallet = node->walletLoader().loadWallet(result.name.toStdString(), warnings);
        result.warnings = JoinWarnings(warnings);
        if (!wallet) result.error = WalletError(util::ErrorString(wallet));
        else {
            result.name = QString::fromStdString((*wallet)->getWalletName());
            result.wallet = std::move(*wallet);
        }
        return result;
    }, [this, path](WalletLoadResult result) {
        setWalletLoadInProgress(false);
        setWalletLoadWarnings(result.warnings);
        if (result.migration_required) {
            Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::Closed, {});
            Q_EMIT walletMigrationRequired(result.name);
        } else if (!result.wallet) {
            const QString error = result.error.isEmpty() ? tr("Wallet could not be opened.") : result.error;
            setWalletLoadError(error);
            Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::LoadError, error);
        } else {
            addOrSelectWalletModel(std::move(result.wallet), result.name);
            Q_EMIT walletLoadSucceeded();
        }
    }, [this, path](std::exception_ptr) {
        setWalletLoadInProgress(false);
        setWalletLoadError(tr("Wallet could not be opened."));
        Q_EMIT walletLoadStateChanged(path, WalletListModel::LoadState::LoadError, m_wallet_load_error);
    });
}

void WalletQmlController::startWalletMigration(const QString& path, SecureString passphrase)
{
    if (m_shutting_down || m_wallet_load_in_progress || m_wallet_migration_in_progress) return;
    clearWalletLoadStatus();
    clearWalletMigrationStatus();
    setWalletMigrationInProgress(true);
    m_wallet_command_executor->submit(this, [node = &m_node, path, passphrase = std::move(passphrase)]() mutable {
        WalletLoadResult result;
        result.name = resolveManagedWalletReference(*node, path);
        if (result.name.isEmpty()) {
            result.error = tr("The selected wallet is not available in the wallet directory.");
            return result;
        }
        if (passphrase.empty() && node->walletLoader().isEncrypted(result.name.toStdString())) {
            result.passphrase_required = true;
            return result;
        }
        auto migration = node->walletLoader().migrateWallet(result.name.toStdString(), passphrase);
        QmlUtil::ClearSecureString(passphrase);
        if (!migration) result.error = WalletError(util::ErrorString(migration));
        else if (migration->wallet) {
            result.name = QString::fromStdString(migration->wallet->getWalletName());
            result.wallet = std::move(migration->wallet);
        }
        return result;
    }, [this](WalletLoadResult result) {
        setWalletMigrationInProgress(false);
        if (result.passphrase_required) {
            Q_EMIT walletMigrationPassphraseRequired(result.name);
        } else if (!result.error.isEmpty()) {
            setWalletMigrationError(result.error);
            Q_EMIT walletMigrationFailed();
        } else {
            if (result.wallet) addOrSelectWalletModel(std::move(result.wallet), result.name);
            refreshWalletCatalog();
            Q_EMIT walletMigrationSucceeded();
        }
    }, [this](std::exception_ptr) {
        setWalletMigrationInProgress(false);
        setWalletMigrationError(tr("Wallet update failed."));
        Q_EMIT walletMigrationFailed();
    });
}

void WalletQmlController::setWalletCreateError(const QString& error)
{
    if (m_wallet_create_error != error) {
        m_wallet_create_error = error;
        Q_EMIT walletCreateErrorChanged();
    }
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

QString WalletQmlController::describeImportedWalletKeyScheme(interfaces::Wallet& imported_wallet)
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

QString WalletQmlController::makeSuggestedExternalSignerWalletName(const QString& signer_name) const
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

void WalletQmlController::setExternalSignerStatus(bool path_configured, int signer_count, const QString& signer_name, const QString& error)
{
    const QString suggested_name = makeSuggestedExternalSignerWalletName(signer_name);
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
