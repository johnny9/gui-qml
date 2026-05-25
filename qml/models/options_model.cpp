// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/options_model.h>

#include <common/args.h>
#include <common/init.h>
#include <common/settings.h>
#include <common/system.h>
#include <chainparamsbase.h>
#include <init.h>
#include <interfaces/node.h>
#include <mapport.h>
#include <node/caches.h>
#include <node/chainstatemanager_args.h>
#include <qml/guiconstants.h>
#include <txdb.h>
#include <univalue.h>
#include <util/fs.h>
#include <util/fs_helpers.h>
#include <validation.h>

#include <cassert>
#include <fstream>
#include <map>

#include <QDesktopServices>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStorageInfo>
#include <QStringList>
#include <QUrl>

namespace {
int PruneMiBtoGB(int64_t mib)
{
    return (mib * 1024 * 1024 + GB_BYTES - 1) / GB_BYTES;
}

int64_t PruneGBtoMiB(int gb)
{
    return gb * GB_BYTES / 1024 / 1024;
}

QString NormalizeCommandPath(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QUrl url(trimmed);
    return url.isLocalFile() ? url.toLocalFile() : trimmed;
}

QString FirstCommandToken(const QString& command)
{
    static const QRegularExpression TOKEN_RE(QStringLiteral(R"re(^\s*(?:"([^"]+)"|'([^']+)'|(\S+)))re"));
    const auto match = TOKEN_RE.match(command);
    if (!match.hasMatch()) {
        return {};
    }
    for (int i = 1; i <= 3; ++i) {
        const QString captured = match.captured(i);
        if (!captured.isEmpty()) {
            return captured;
        }
    }
    return {};
}

QString ExpandUserPath(const QString& path)
{
    if (path == QStringLiteral("~")) {
        return QDir::homePath();
    }
    if (path.startsWith(QStringLiteral("~/"))) {
        return QDir::homePath() + path.mid(1);
    }
    return path;
}

bool TokenLooksLikePath(const QString& token)
{
    return token.startsWith(QStringLiteral("/")) ||
           token.startsWith(QStringLiteral("./")) ||
           token.startsWith(QStringLiteral("../")) ||
           token.startsWith(QStringLiteral("~/")) ||
           token.contains(QLatin1Char('/')) ||
           token.contains(QLatin1Char('\\')) ||
           QDir::isAbsolutePath(token);
}

QString PathToQString(const fs::path &path)
{
    return QString::fromStdString(path.utf8string());
}

QString CurrentDataDirString(ArgsManager& args)
{
    const fs::path data_dir = args.GetDataDirBase();
    return data_dir.empty() ? PathToQString(GetDefaultDataDir()) : PathToQString(data_dir);
}

fs::path NetworkDataDir(const fs::path& data_dir)
{
    const std::string network_dir{BaseParams().DataDir()};
    return network_dir.empty() ? data_dir : data_dir / fs::PathFromString(network_dir);
}

bool ExistsNoThrow(const fs::path& path)
{
    try {
        return fs::exists(path);
    } catch (const fs::filesystem_error&) {
        return false;
    }
}

bool DirectoryHasEntries(const fs::path& path)
{
    try {
        if (!fs::is_directory(path)) return false;
        return fs::directory_iterator(path) != fs::directory_iterator{};
    } catch (const fs::filesystem_error&) {
        return false;
    }
}

bool SettingsFileHasValues(const fs::path& path)
{
    std::map<std::string, common::SettingsValue> values;
    std::vector<std::string> errors;
    return common::ReadSettings(path, values, errors) && !values.empty();
}

QString FormatBytes(quint64 bytes)
{
    constexpr double GB = 1000.0 * 1000.0 * 1000.0;
    return QObject::tr("%1 GB available").arg(QString::number(bytes / GB, 'f', 1));
}

int LegacyDisplayUnitToQtUnit(int legacy_unit)
{
    return legacy_unit == 1
        ? static_cast<int>(QmlBitcoinUnits::Unit::SAT)
        : static_cast<int>(QmlBitcoinUnits::Unit::BTC);
}
} // namespace

OptionsQmlModel::OptionsQmlModel(interfaces::Node& node, bool is_onboarded)
    : OptionsQmlModel(node, is_onboarded, gArgs, /*initialize_config_on_onboard=*/true)
{
}

OptionsQmlModel::OptionsQmlModel(interfaces::Node& node, bool is_onboarded, ArgsManager& args, bool initialize_config_on_onboard)
    : m_node{node}
    , m_args{args}
    , m_initialize_config_on_onboard{initialize_config_on_onboard}
    , m_onboarded{is_onboarded}
{
    loadPersistentSettings();
    buildAvailableLanguages();
    refreshDataDirAvailable(m_dataDir);
}

void OptionsQmlModel::loadPersistentSettings()
{
    loadPersistentOptionValues();

    m_dataDir = CurrentDataDirString(m_args);
    if (m_dataDir != getDefaultDataDirString()) {
        m_custom_datadir_string = m_dataDir;
    }
    setExistingCoreProfile(existingCoreDataDir(m_dataDir));
    resetModifiedFlags();
}

void OptionsQmlModel::loadPersistentOptionValues(bool notify)
{
    const int old_dbcache_size_mib = notify ? m_dbcache_size_mib : 0;
    const bool old_listen = notify ? m_listen : false;
    const int old_max_mempool_size_mb = notify ? m_max_mempool_size_mb : 0;
    const bool old_natpmp = notify ? m_natpmp : false;
    const bool old_prune = notify ? m_prune : false;
    const int old_prune_size_gb = notify ? m_prune_size_gb : 0;
    const int old_script_threads = notify ? m_script_threads : 0;
    const bool old_server = notify ? m_server : false;
    const bool old_proxy_enabled = notify ? m_proxy_enabled : false;
    const QString old_proxy_address = notify ? m_proxy_address : QString{};
    const bool old_tor_enabled = notify ? m_tor_enabled : false;
    const QString old_tor_address = notify ? m_tor_address : QString{};
    const QString old_external_signer_path = notify ? m_external_signer_path : QString{};
    const bool was_connection_dirty = notify ? connectionSettingsDirty() : false;
    const bool was_storage_dirty = notify ? storageSettingsDirty() : false;
    const bool was_developer_dirty = notify ? developerSettingsDirty() : false;
    const bool was_proxy_dirty = notify ? proxySettingsDirty() : false;
    const bool was_wallet_dirty = notify ? walletSettingsDirty() : false;

    m_dbcache_size_mib = SettingToInt(m_node.getPersistentSetting("dbcache"), DEFAULT_DB_CACHE >> 20);
    m_initial_dbcache_size_mib = m_dbcache_size_mib;

    m_listen = SettingToBool(m_node.getPersistentSetting("listen"), DEFAULT_LISTEN);
    m_initial_listen = m_listen;

    m_max_mempool_size_mb = SettingToInt(m_node.getPersistentSetting("maxmempool"), DEFAULT_MAX_MEMPOOL_SIZE_MB);

    m_natpmp = SettingToBool(m_node.getPersistentSetting("natpmp"), DEFAULT_NATPMP);
    m_initial_natpmp = m_natpmp;

    int64_t prune_value{SettingToInt(m_node.getPersistentSetting("prune"), 0)};
    m_prune = (prune_value > 1);
    m_prune_size_gb = m_prune ? PruneMiBtoGB(prune_value) : DEFAULT_PRUNE_TARGET_GB;
    m_initial_prune = m_prune;
    m_initial_prune_size_gb = m_prune_size_gb;

    m_script_threads = SettingToInt(m_node.getPersistentSetting("par"), DEFAULT_SCRIPTCHECK_THREADS);
    m_initial_script_threads = m_script_threads;

    m_server = SettingToBool(m_node.getPersistentSetting("server"), false);
    m_initial_server = m_server;

    QString proxy_setting = QString::fromStdString(SettingToString(m_node.getPersistentSetting("proxy"), ""));
    if (proxy_setting == "0") proxy_setting.clear();
    m_proxy_enabled = !proxy_setting.isEmpty();
    m_proxy_address = proxy_setting;

    QString onion_setting = QString::fromStdString(SettingToString(m_node.getPersistentSetting("onion"), ""));
    if (onion_setting == "0") onion_setting.clear();
    m_tor_enabled = !onion_setting.isEmpty();
    m_tor_address = onion_setting;

    m_external_signer_path = QString::fromStdString(SettingToString(m_node.getPersistentSetting("signer"), ""));

    m_initial_proxy_enabled = m_proxy_enabled;
    m_initial_proxy_address = m_proxy_address;
    m_initial_tor_enabled   = m_tor_enabled;
    m_initial_tor_address   = m_tor_address;
    m_initial_external_signer_path = m_external_signer_path;
    resetModifiedFlags();

    if (notify) {
        if (old_dbcache_size_mib != m_dbcache_size_mib) Q_EMIT dbcacheSizeMiBChanged(m_dbcache_size_mib);
        if (old_listen != m_listen) Q_EMIT listenChanged(m_listen);
        if (old_max_mempool_size_mb != m_max_mempool_size_mb) Q_EMIT maxMempoolSizeMBChanged(m_max_mempool_size_mb);
        if (old_natpmp != m_natpmp) Q_EMIT natpmpChanged(m_natpmp);
        if (old_prune != m_prune) Q_EMIT pruneChanged(m_prune);
        if (old_prune_size_gb != m_prune_size_gb) Q_EMIT pruneSizeGBChanged(m_prune_size_gb);
        if (old_script_threads != m_script_threads) Q_EMIT scriptThreadsChanged(m_script_threads);
        if (old_server != m_server) Q_EMIT serverChanged(m_server);
        if (old_proxy_enabled != m_proxy_enabled) Q_EMIT proxyEnabledChanged(m_proxy_enabled);
        if (old_proxy_address != m_proxy_address) Q_EMIT proxyAddressChanged(m_proxy_address);
        if (old_tor_enabled != m_tor_enabled) Q_EMIT torEnabledChanged(m_tor_enabled);
        if (old_tor_address != m_tor_address) Q_EMIT torAddressChanged(m_tor_address);
        if (old_external_signer_path != m_external_signer_path) Q_EMIT externalSignerPathChanged(m_external_signer_path);
        markConnectionDirtyChanged(was_connection_dirty);
        markStorageDirtyChanged(was_storage_dirty);
        markDeveloperDirtyChanged(was_developer_dirty);
        if (proxySettingsDirty() != was_proxy_dirty) Q_EMIT proxySettingsDirtyChanged();
        if (walletSettingsDirty() != was_wallet_dirty) Q_EMIT walletSettingsDirtyChanged();
    }

    QSettings settings;
    m_language = settings.value(SettingsKeys::LANGUAGE, "").toString();
    if (settings.contains(SettingsKeys::DISPLAY_UNIT)) {
        m_display_unit = settings.value(SettingsKeys::DISPLAY_UNIT, 0).toInt();
    } else if (settings.contains(SettingsKeys::LEGACY_DISPLAY_UNIT)) {
        m_display_unit = LegacyDisplayUnitToQtUnit(settings.value(SettingsKeys::LEGACY_DISPLAY_UNIT, 0).toInt());
        settings.setValue(SettingsKeys::DISPLAY_UNIT, m_display_unit);
    } else {
        m_display_unit = static_cast<int>(QmlBitcoinUnits::Unit::BTC);
        settings.setValue(SettingsKeys::DISPLAY_UNIT, m_display_unit);
    }
}

void OptionsQmlModel::setDbcacheSizeMiB(int new_dbcache_size_mib)
{
    if (new_dbcache_size_mib != m_dbcache_size_mib) {
        const bool was_dirty = developerSettingsDirty();
        m_dbcache_size_mib = new_dbcache_size_mib;
        m_dbcache_modified = true;
        if (m_onboarded) {
            m_node.updateRwSetting("dbcache", new_dbcache_size_mib);
        }
        markDeveloperDirtyChanged(was_dirty);
        Q_EMIT dbcacheSizeMiBChanged(new_dbcache_size_mib);
    }
}

void OptionsQmlModel::setListen(bool new_listen)
{
    if (new_listen != m_listen) {
        const bool was_dirty = connectionSettingsDirty();
        m_listen = new_listen;
        m_listen_modified = true;
        if (m_onboarded) {
            m_node.updateRwSetting("listen", new_listen);
        }
        markConnectionDirtyChanged(was_dirty);
        Q_EMIT listenChanged(new_listen);
    }
}

void OptionsQmlModel::setMaxMempoolSizeMB(int new_max_mempool_size_mb)
{
    if (new_max_mempool_size_mb != m_max_mempool_size_mb) {
        m_max_mempool_size_mb = new_max_mempool_size_mb;
        if (m_onboarded) {
            m_node.updateRwSetting("maxmempool", new_max_mempool_size_mb);
        }
        Q_EMIT maxMempoolSizeMBChanged(new_max_mempool_size_mb);
    }
}

void OptionsQmlModel::setNatpmp(bool new_natpmp)
{
    if (new_natpmp != m_natpmp) {
        const bool was_dirty = connectionSettingsDirty();
        m_natpmp = new_natpmp;
        m_natpmp_modified = true;
        if (m_onboarded) {
            m_node.updateRwSetting("natpmp", new_natpmp);
        }
        markConnectionDirtyChanged(was_dirty);
        Q_EMIT natpmpChanged(new_natpmp);
    }
}

void OptionsQmlModel::setPrune(bool new_prune)
{
    if (new_prune != m_prune) {
        const bool was_dirty = storageSettingsDirty();
        m_prune = new_prune;
        m_prune_modified = true;
        if (m_onboarded) {
            m_node.updateRwSetting("prune", pruneSetting());
        }
        markStorageDirtyChanged(was_dirty);
        Q_EMIT pruneChanged(new_prune);
    }
}

void OptionsQmlModel::setPruneSizeGB(int new_prune_size_gb)
{
    if (new_prune_size_gb != m_prune_size_gb) {
        const bool was_dirty = storageSettingsDirty();
        m_prune_size_gb = new_prune_size_gb;
        m_prune_size_modified = true;
        if (m_onboarded) {
            m_node.updateRwSetting("prune", pruneSetting());
        }
        markStorageDirtyChanged(was_dirty);
        Q_EMIT pruneSizeGBChanged(new_prune_size_gb);
    }
}

void OptionsQmlModel::setScriptThreads(int new_script_threads)
{
    if (new_script_threads != m_script_threads) {
        const bool was_dirty = developerSettingsDirty();
        m_script_threads = new_script_threads;
        m_script_threads_modified = true;
        if (m_onboarded) {
            m_node.updateRwSetting("par", new_script_threads);
        }
        markDeveloperDirtyChanged(was_dirty);
        Q_EMIT scriptThreadsChanged(new_script_threads);
    }
}

void OptionsQmlModel::setServer(bool new_server)
{
    if (new_server != m_server) {
        const bool was_dirty = connectionSettingsDirty();
        m_server = new_server;
        m_server_modified = true;
        if (m_onboarded) {
            m_node.updateRwSetting("server", new_server);
        }
        markConnectionDirtyChanged(was_dirty);
        Q_EMIT serverChanged(new_server);
    }
}

void OptionsQmlModel::setProxyEnabled(bool enabled)
{
    if (enabled != m_proxy_enabled) {
        bool was_dirty = proxySettingsDirty();
        m_proxy_enabled = enabled;
        m_proxy_modified = true;
        if (m_onboarded) {
            if (enabled && !m_proxy_address.isEmpty()) {
                m_node.updateRwSetting("proxy", m_proxy_address.toStdString());
            } else {
                m_node.updateRwSetting("proxy", common::SettingsValue{});
            }
        }
        if (proxySettingsDirty() != was_dirty) {
            Q_EMIT proxySettingsDirtyChanged();
        }
        Q_EMIT proxyEnabledChanged(enabled);
    }
}

void OptionsQmlModel::setProxyAddress(const QString& address)
{
    if (address != m_proxy_address) {
        bool was_dirty = proxySettingsDirty();
        m_proxy_address = address;
        m_proxy_modified = true;
        if (m_onboarded && m_proxy_enabled) {
            m_node.updateRwSetting("proxy", address.toStdString());
        }
        if (proxySettingsDirty() != was_dirty) {
            Q_EMIT proxySettingsDirtyChanged();
        }
        Q_EMIT proxyAddressChanged(address);
    }
}

void OptionsQmlModel::setTorEnabled(bool enabled)
{
    if (enabled != m_tor_enabled) {
        bool was_dirty = proxySettingsDirty();
        m_tor_enabled = enabled;
        m_tor_modified = true;
        if (m_onboarded) {
            if (enabled && !m_tor_address.isEmpty()) {
                m_node.updateRwSetting("onion", m_tor_address.toStdString());
            } else {
                m_node.updateRwSetting("onion", common::SettingsValue{});
            }
        }
        if (proxySettingsDirty() != was_dirty) {
            Q_EMIT proxySettingsDirtyChanged();
        }
        Q_EMIT torEnabledChanged(enabled);
    }
}

void OptionsQmlModel::setTorAddress(const QString& address)
{
    if (address != m_tor_address) {
        bool was_dirty = proxySettingsDirty();
        m_tor_address = address;
        m_tor_modified = true;
        if (m_onboarded && m_tor_enabled) {
            m_node.updateRwSetting("onion", address.toStdString());
        }
        if (proxySettingsDirty() != was_dirty) {
            Q_EMIT proxySettingsDirtyChanged();
        }
        Q_EMIT torAddressChanged(address);
    }
}

void OptionsQmlModel::setExternalSignerPath(const QString& path)
{
    const QString normalized_path = NormalizeCommandPath(path);
    if (normalized_path != m_external_signer_path) {
        bool was_dirty = walletSettingsDirty();
        m_external_signer_path = normalized_path;
        m_external_signer_modified = true;
        if (m_external_signer_path.isEmpty()) {
            m_node.forceSetting("signer", common::SettingsValue{});
        } else {
            m_node.forceSetting("signer", m_external_signer_path.toStdString());
        }
        if (m_onboarded) {
            if (m_external_signer_path.isEmpty()) {
                m_node.updateRwSetting("signer", common::SettingsValue{});
            } else {
                m_node.updateRwSetting("signer", m_external_signer_path.toStdString());
            }
        }
        if (walletSettingsDirty() != was_dirty) {
            Q_EMIT walletSettingsDirtyChanged();
        }
        Q_EMIT externalSignerPathChanged(m_external_signer_path);
    }
}

QString OptionsQmlModel::externalSignerPathValidationError(const QString& path) const
{
    const QString normalized_path = NormalizeCommandPath(path);
    if (normalized_path.isEmpty()) {
        return {};
    }

    const QString token = FirstCommandToken(normalized_path);
    if (token.isEmpty() || !TokenLooksLikePath(token)) {
        return {};
    }

    const QFileInfo info(ExpandUserPath(token));
    if (!info.exists()) {
        return tr("The configured signer path does not exist.");
    }
    if (!info.isFile()) {
        return tr("The configured signer path is not a file.");
    }
    if (!info.isExecutable()) {
        return tr("The configured signer path is not executable.");
    }
    return {};
}

common::SettingsValue OptionsQmlModel::pruneSetting() const
{
    assert(!m_prune || m_prune_size_gb >= 1);
    return m_prune ? PruneGBtoMiB(m_prune_size_gb) : 0;
}

QString OptionsQmlModel::getDefaultDataDirString()
{
    return PathToQString(GetDefaultDataDir());
}


QUrl OptionsQmlModel::getDefaultDataDirectory()
{
    QString path = getDefaultDataDirString();
    return QUrl::fromLocalFile(path);
}

bool OptionsQmlModel::setCustomDataDirArgs(QString path)
{
    const QString normalized_path = normalizeDataDirPath(path);
    if (normalized_path.isEmpty()) {
        setDataDirError(tr("Select a data directory."));
        return false;
    }
    if (!validateDataDirPath(normalized_path)) return false;

    if (m_dataDir != normalized_path) {
        if (!selectDataDirOptions(normalized_path)) return false;
        if (m_custom_datadir_string != normalized_path) {
            m_custom_datadir_string = normalized_path;
            Q_EMIT customDataDirStringChanged(m_custom_datadir_string);
        }
        m_dataDir = normalized_path;
        refreshDataDirAvailable(m_dataDir);
        Q_EMIT dataDirChanged(m_dataDir);
    } else if (m_custom_datadir_string != normalized_path) {
        m_custom_datadir_string = normalized_path;
        Q_EMIT customDataDirStringChanged(m_custom_datadir_string);
    }
    setDataDirError({});
    return true;
}

QString OptionsQmlModel::getCustomDataDirString()
{
    return m_custom_datadir_string;
}

void OptionsQmlModel::setDataDir(QString new_data_dir)
{
    const QString normalized_path = normalizeDataDirPath(new_data_dir);
    const QString default_data_dir = getDefaultDataDirString();
    const bool use_default = normalized_path.isEmpty() || normalized_path == default_data_dir;
    const QString selected_data_dir = use_default ? default_data_dir : normalized_path;

    if (!validateDataDirPath(selected_data_dir)) return;

    if (m_dataDir != selected_data_dir) {
        if (!selectDataDirOptions(selected_data_dir)) return;
        if (use_default) {
            if (!m_custom_datadir_string.isEmpty()) {
                m_custom_datadir_string.clear();
                Q_EMIT customDataDirStringChanged(m_custom_datadir_string);
            }
        } else if (m_custom_datadir_string != selected_data_dir) {
            m_custom_datadir_string = selected_data_dir;
            Q_EMIT customDataDirStringChanged(m_custom_datadir_string);
        }
        m_dataDir = selected_data_dir;
        refreshDataDirAvailable(m_dataDir);
        Q_EMIT dataDirChanged(m_dataDir);
    }
    setDataDirError({});
}

void OptionsQmlModel::applyDataDirArg(const QString& data_dir)
{
    const QString default_data_dir = getDefaultDataDirString();
    m_args.LockSettings([](common::Settings& settings) {
        settings.forced_settings.erase("datadir");
    });
    if (data_dir != default_data_dir) {
        m_args.ForceSetArg("-datadir", fs::PathToString(fs::PathFromString(data_dir.toStdString())));
    }
    m_args.ClearPathCache();
}

bool OptionsQmlModel::selectDataDirOptions(const QString& data_dir)
{
    const bool existing_profile = existingCoreDataDir(data_dir);
    applyDataDirArg(data_dir);

    QFileInfo info(data_dir);
    if (info.exists()) {
        std::string error;
        if (!m_args.ReadConfigFiles(error, /*ignore_invalid_keys=*/true)) {
            setDataDirError(QString::fromStdString(error));
            return false;
        }
        std::vector<std::string> read_errors;
        if (!m_args.ReadSettingsFile(&read_errors)) {
            setDataDirError(read_errors.empty()
                ? tr("Settings file could not be read.")
                : QString::fromStdString(read_errors.front()));
            return false;
        }
    } else {
        m_args.LockSettings([](common::Settings& settings) {
            settings.rw_settings.clear();
            settings.ro_config.clear();
        });
    }

    setExistingCoreProfile(existing_profile);
    loadPersistentOptionValues(/*notify=*/true);
    return true;
}

void OptionsQmlModel::setDataDirError(const QString& error)
{
    if (error == m_data_dir_error) return;
    m_data_dir_error = error;
    Q_EMIT dataDirErrorChanged(m_data_dir_error);
}

void OptionsQmlModel::setDataDirAvailable(const QString& available)
{
    if (available == m_data_dir_available) return;
    m_data_dir_available = available;
    Q_EMIT dataDirAvailableChanged(m_data_dir_available);
}

void OptionsQmlModel::setSettingsActionError(const QString& error)
{
    if (error == m_settings_action_error) return;
    m_settings_action_error = error;
    Q_EMIT settingsActionErrorChanged(m_settings_action_error);
}

void OptionsQmlModel::setExistingCoreProfile(bool existing_core_profile)
{
    if (existing_core_profile == m_existing_core_profile) return;
    m_existing_core_profile = existing_core_profile;
    Q_EMIT existingCoreProfileChanged();
}

QString OptionsQmlModel::normalizeDataDirPath(const QString& path) const
{
    QString normalized_path = path.trimmed();
    if (normalized_path.isEmpty()) return {};

#ifdef __ANDROID__
    normalized_path.replace(
        QStringLiteral("content://com.android.externalstorage.documents/tree/primary%3A"),
        QStringLiteral("/storage/self/primary/"));
#else
    const QUrl url(normalized_path);
    if (url.isLocalFile()) {
        normalized_path = url.toLocalFile();
    }
#endif // __ANDROID__

    return QDir::cleanPath(normalized_path);
}

bool OptionsQmlModel::existingCoreDataDir(const QString& path) const
{
    const fs::path data_dir = fs::PathFromString(path.toStdString());
    const fs::path network_dir = NetworkDataDir(data_dir);
    return SettingsFileHasValues(network_dir / BITCOIN_SETTINGS_FILENAME) ||
           ExistsNoThrow(network_dir / "blocks" / "index") ||
           ExistsNoThrow(network_dir / "chainstate") ||
           DirectoryHasEntries(network_dir / "wallets");
}

void OptionsQmlModel::refreshDataDirAvailable(const QString& path)
{
    fs::path parent_dir = fs::PathFromString(path.toStdString());
    fs::path previous_parent_dir;
    try {
        while (parent_dir.has_parent_path() && !fs::exists(parent_dir)) {
            parent_dir = parent_dir.parent_path();
            if (parent_dir == previous_parent_dir) break;
            previous_parent_dir = parent_dir;
        }
        const auto space_info = fs::space(parent_dir);
        setDataDirAvailable(FormatBytes(space_info.available));
    } catch (const fs::filesystem_error&) {
        const QStorageInfo storage(path);
        if (storage.isValid()) {
            setDataDirAvailable(FormatBytes(storage.bytesAvailable()));
        } else {
            setDataDirAvailable({});
        }
    }
}

bool OptionsQmlModel::validateDataDirPath(const QString& path)
{
    QFileInfo info(path);
    if (info.exists() && !info.isDir()) {
        setDataDirError(tr("The selected data directory path is not a directory."));
        return false;
    }

    fs::path parent_dir = fs::PathFromString(path.toStdString());
    fs::path previous_parent_dir;
    try {
        while (parent_dir.has_parent_path() && !fs::exists(parent_dir)) {
            parent_dir = parent_dir.parent_path();
            if (parent_dir == previous_parent_dir) break;
            previous_parent_dir = parent_dir;
        }
        const auto space_info = fs::space(parent_dir);
        setDataDirAvailable(FormatBytes(space_info.available));
        if (space_info.available < MIN_DISK_SPACE_FOR_BLOCK_FILES) {
            setDataDirError(tr("The selected data directory does not have enough free space."));
            return false;
        }
    } catch (const fs::filesystem_error&) {
        setDataDirError(tr("The selected data directory could not be created."));
        return false;
    }

    setDataDirError({});
    return true;
}

bool OptionsQmlModel::validateDataDirSelection()
{
    return validateDataDirPath(m_dataDir);
}

bool OptionsQmlModel::commitDataDir()
{
    if (!validateDataDirPath(m_dataDir)) return false;

    const QString default_data_dir = getDefaultDataDirString();
    if (m_dataDir == default_data_dir) {
        m_args.LockSettings([](common::Settings& settings) {
            settings.forced_settings.erase("datadir");
        });
    } else {
        const fs::path data_dir = fs::PathFromString(m_dataDir.toStdString());
        try {
            if (TryCreateDirectories(data_dir)) {
                TryCreateDirectories(data_dir / "wallets");
            }
        } catch (const fs::filesystem_error&) {
            setDataDirError(tr("The selected data directory could not be created."));
            return false;
        }
        m_args.LockSettings([](common::Settings& settings) {
            settings.forced_settings.erase("datadir");
        });
        m_args.ForceSetArg("-datadir", fs::PathToString(data_dir));
    }
    m_args.ClearPathCache();

    if (m_initialize_config_on_onboard) {
        if (auto error = common::InitConfig(
                m_args,
                [](const bilingual_str& msg, const std::vector<std::string>& details) {
                    Q_UNUSED(msg);
                    Q_UNUSED(details);
                    return false;
                })) {
            setDataDirError(QString::fromStdString(error->message.translated));
            return false;
        }

        InitLogging(m_args);
        InitParameterInteraction(m_args);
        if (!m_node.baseInitialize()) {
            setDataDirError(tr("Node startup initialization failed."));
            return false;
        }
    }

    setDataDirError({});
    Q_EMIT dataDirCommitted(m_dataDir);
    qDebug() << "Configured data directory:" << m_dataDir;
    return true;
}

void OptionsQmlModel::buildAvailableLanguages()
{
    m_available_languages.clear();
    m_available_languages << "";  // empty = system default

    QDir translations_dir(":/translations");
    QStringList files = translations_dir.entryList({"bitcoin_*.qm"}, QDir::Files);
    QStringList tags;
    for (const QString& file : files) {
        // Strip "bitcoin_" prefix and ".qm" suffix to get locale tag.
        QString tag = file;
        tag.remove(0, 8);       // remove "bitcoin_"
        tag.chop(3);            // remove ".qm"
        // Skip QML-app-specific translation resources (e.g. "qml_es").
        if (tag.startsWith(QStringLiteral("qml_"))) continue;
        tags << tag;
    }
    tags.sort(Qt::CaseInsensitive);
    m_available_languages << tags;
}

void OptionsQmlModel::setLanguage(const QString& new_language)
{
    if (new_language != m_language) {
        m_language = new_language;
        QSettings settings;
        settings.setValue(SettingsKeys::LANGUAGE, m_language);
        Q_EMIT languageChanged();
    }
}

QString OptionsQmlModel::languageSummary() const
{
    return languageLabel(m_language);
}

QString OptionsQmlModel::languageLabel(const QString& locale_tag) const
{
    if (locale_tag.isEmpty()) {
        return QObject::tr("System default");
    }
    QLocale locale(locale_tag);
    QString native = locale.nativeLanguageName();
    if (native.isEmpty()) {
        return locale_tag;
    }
    // Capitalize first letter of native name.
    native[0] = native[0].toUpper();
    QString english = QLocale::languageToString(locale.language());
    // Append territory disambiguation when the tag includes a territory code.
    if (locale_tag.contains('_')) {
        QString native_territory = locale.nativeTerritoryName();
        if (!native_territory.isEmpty()) {
            native += QStringLiteral(" (%1)").arg(native_territory);
        }
        english += QStringLiteral(" (%1)").arg(QLocale::territoryToString(locale.territory()));
    }
    return QStringLiteral("%1 \u2014 %2").arg(native, english);
}

void OptionsQmlModel::setDisplayUnit(int new_display_unit)
{
    if (new_display_unit != m_display_unit) {
        m_display_unit = new_display_unit;
        QSettings settings;
        settings.setValue(SettingsKeys::DISPLAY_UNIT, m_display_unit);
        settings.remove(SettingsKeys::LEGACY_DISPLAY_UNIT);
        Q_EMIT displayUnitChanged(m_display_unit);
    }
}

QmlBitcoinUnits::Unit OptionsQmlModel::displayUnitEnum() const
{
    switch (m_display_unit) {
    case static_cast<int>(QmlBitcoinUnits::Unit::BTC): return QmlBitcoinUnits::Unit::BTC;
    case static_cast<int>(QmlBitcoinUnits::Unit::mBTC): return QmlBitcoinUnits::Unit::mBTC;
    case static_cast<int>(QmlBitcoinUnits::Unit::uBTC): return QmlBitcoinUnits::Unit::uBTC;
    case static_cast<int>(QmlBitcoinUnits::Unit::SAT): return QmlBitcoinUnits::Unit::SAT;
    }
    return QmlBitcoinUnits::Unit::BTC;
}

QString OptionsQmlModel::displayUnitLabel() const
{
    switch (displayUnitEnum()) {
    case QmlBitcoinUnits::Unit::BTC: return QStringLiteral("BTC");
    case QmlBitcoinUnits::Unit::mBTC: return QStringLiteral("mBTC");
    case QmlBitcoinUnits::Unit::uBTC: return QStringLiteral("bits");
    case QmlBitcoinUnits::Unit::SAT: return QStringLiteral("sat");
    }
    return QStringLiteral("BTC");
}

QString OptionsQmlModel::displayUnitLabelForAmount(qint64 satoshi) const
{
    switch (displayUnitEnum()) {
    case QmlBitcoinUnits::Unit::BTC: return QStringLiteral("₿");
    case QmlBitcoinUnits::Unit::mBTC: return QStringLiteral("mBTC");
    case QmlBitcoinUnits::Unit::uBTC: return QStringLiteral("bits");
    case QmlBitcoinUnits::Unit::SAT:
        return (qAbs(satoshi) == 1) ? QStringLiteral("sat") : QStringLiteral("sats");
    }
    return QStringLiteral("₿");
}

void OptionsQmlModel::markConnectionDirtyChanged(bool was_dirty)
{
    if (connectionSettingsDirty() != was_dirty) {
        Q_EMIT connectionSettingsDirtyChanged();
    }
}

void OptionsQmlModel::markStorageDirtyChanged(bool was_dirty)
{
    if (storageSettingsDirty() != was_dirty) {
        Q_EMIT storageSettingsDirtyChanged();
    }
}

void OptionsQmlModel::markDeveloperDirtyChanged(bool was_dirty)
{
    if (developerSettingsDirty() != was_dirty) {
        Q_EMIT developerSettingsDirtyChanged();
    }
}

void OptionsQmlModel::resetModifiedFlags()
{
    m_dbcache_modified = false;
    m_listen_modified = false;
    m_natpmp_modified = false;
    m_prune_modified = false;
    m_prune_size_modified = false;
    m_script_threads_modified = false;
    m_server_modified = false;
    m_proxy_modified = false;
    m_tor_modified = false;
    m_external_signer_modified = false;
}

void OptionsQmlModel::resetDirtyBaselines()
{
    m_initial_dbcache_size_mib = m_dbcache_size_mib;
    m_initial_listen = m_listen;
    m_initial_natpmp = m_natpmp;
    m_initial_prune = m_prune;
    m_initial_prune_size_gb = m_prune_size_gb;
    m_initial_script_threads = m_script_threads;
    m_initial_server = m_server;
    m_initial_proxy_enabled = m_proxy_enabled;
    m_initial_proxy_address = m_proxy_address;
    m_initial_tor_enabled = m_tor_enabled;
    m_initial_tor_address = m_tor_address;
    m_initial_external_signer_path = m_external_signer_path;
    resetModifiedFlags();
    Q_EMIT connectionSettingsDirtyChanged();
    Q_EMIT storageSettingsDirtyChanged();
    Q_EMIT developerSettingsDirtyChanged();
    Q_EMIT proxySettingsDirtyChanged();
    Q_EMIT walletSettingsDirtyChanged();
}

bool OptionsQmlModel::resetGuiSettings()
{
    m_node.resetSettings();
    QSettings settings;
    settings.clear();
    settings.setValue(SettingsKeys::DATA_DIR, m_dataDir);
    settings.setValue(QStringLiteral("fReset"), true);
    settings.sync();
    setSettingsActionError({});
    return true;
}

bool OptionsQmlModel::openBitcoinConf()
{
    const fs::path config_path = m_args.GetConfigFilePath();
    std::ofstream config_file{config_path, std::ios_base::app};
    if (!config_file.good()) {
        setSettingsActionError(tr("Could not open bitcoin.conf for writing."));
        return false;
    }
    config_file.close();

    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(PathToQString(config_path)));
#ifdef Q_OS_MACOS
    if (!opened && QProcess::startDetached(QStringLiteral("/usr/bin/open"), QStringList{QStringLiteral("-t"), PathToQString(config_path)})) {
        setSettingsActionError({});
        return true;
    }
#endif
    if (!opened) {
        setSettingsActionError(tr("Could not open bitcoin.conf."));
        return false;
    }
    setSettingsActionError({});
    return true;
}


bool OptionsQmlModel::onboard()
{
    if (!commitDataDir()) return false;

    QSettings settings;
    if (m_dataDir == getDefaultDataDirString()) {
        settings.remove(SettingsKeys::DATA_DIR);
    } else {
        settings.setValue(SettingsKeys::DATA_DIR, m_dataDir);
    }

    if (m_external_signer_modified) {
        if (m_external_signer_path.isEmpty()) {
            m_node.forceSetting("signer", common::SettingsValue{});
        } else {
            m_node.forceSetting("signer", m_external_signer_path.toStdString());
        }
    }
    if (m_dbcache_modified) {
        m_node.updateRwSetting("dbcache", m_dbcache_size_mib);
    }
    if (m_listen_modified) {
        m_node.updateRwSetting("listen", m_listen);
    }
    if (m_natpmp_modified) {
        m_node.updateRwSetting("natpmp", m_natpmp);
    }
    if (m_prune_modified || m_prune_size_modified) {
        m_node.updateRwSetting("prune", pruneSetting());
    }
    if (m_script_threads_modified) {
        m_node.updateRwSetting("par", m_script_threads);
    }
    if (m_server_modified) {
        m_node.updateRwSetting("server", m_server);
    }
    if (m_proxy_modified) {
        if (m_proxy_enabled && !m_proxy_address.isEmpty()) {
            m_node.updateRwSetting("proxy", m_proxy_address.toStdString());
        } else {
            m_node.updateRwSetting("proxy", common::SettingsValue{});
        }
    }
    if (m_tor_modified) {
        if (m_tor_enabled && !m_tor_address.isEmpty()) {
            m_node.updateRwSetting("onion", m_tor_address.toStdString());
        } else {
            m_node.updateRwSetting("onion", common::SettingsValue{});
        }
    }
    if (m_external_signer_modified && !m_external_signer_path.isEmpty()) {
        m_node.updateRwSetting("signer", m_external_signer_path.toStdString());
    }
    m_node.updateRwSetting("qml_onboarded", true);
    m_onboarded = true;
    resetDirtyBaselines();
    return true;
}
