// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/options_model.h>

#include <common/args.h>
#include <common/settings.h>
#include <common/system.h>
#include <common/init.h>
#include <interfaces/node.h>
#include <init.h>
#include <mapport.h>
#include <chainparamsbase.h>
#include <node/caches.h>
#include <node/chainstatemanager_args.h>
#include <qml/guiconstants.h>
#include <txdb.h>
#include <univalue.h>
#include <util/fs.h>
#include <util/fs_helpers.h>
#include <validation.h>

#include <cassert>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>

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
    m_dbcache_size_mib = SettingToInt(m_node.getPersistentSetting("dbcache"), DEFAULT_DB_CACHE >> 20);

    m_listen = SettingToBool(m_node.getPersistentSetting("listen"), DEFAULT_LISTEN);

    m_natpmp = SettingToBool(m_node.getPersistentSetting("natpmp"), DEFAULT_NATPMP);

    int64_t prune_value{SettingToInt(m_node.getPersistentSetting("prune"), 0)};
    m_prune = (prune_value > 1);
    m_prune_size_gb = m_prune ? PruneMiBtoGB(prune_value) : DEFAULT_PRUNE_TARGET_GB;

    m_script_threads = SettingToInt(m_node.getPersistentSetting("par"), DEFAULT_SCRIPTCHECK_THREADS);

    m_server = SettingToBool(m_node.getPersistentSetting("server"), false);

    m_dataDir = CurrentDataDirString(m_args);
    if (m_dataDir != getDefaultDataDirString()) {
        m_custom_datadir_string = m_dataDir;
    }

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

    QSettings settings;
    m_language = settings.value(SettingsKeys::LANGUAGE, "").toString();
    m_display_unit = settings.value(SettingsKeys::DISPLAY_UNIT, 0).toInt();

    buildAvailableLanguages();
}

void OptionsQmlModel::setDbcacheSizeMiB(int new_dbcache_size_mib)
{
    if (new_dbcache_size_mib != m_dbcache_size_mib) {
        m_dbcache_size_mib = new_dbcache_size_mib;
        if (m_onboarded) {
            m_node.updateRwSetting("dbcache", new_dbcache_size_mib);
        }
        Q_EMIT dbcacheSizeMiBChanged(new_dbcache_size_mib);
    }
}

void OptionsQmlModel::setListen(bool new_listen)
{
    if (new_listen != m_listen) {
        m_listen = new_listen;
        if (m_onboarded) {
            m_node.updateRwSetting("listen", new_listen);
        }
        Q_EMIT listenChanged(new_listen);
    }
}

void OptionsQmlModel::setNatpmp(bool new_natpmp)
{
    if (new_natpmp != m_natpmp) {
        m_natpmp = new_natpmp;
        if (m_onboarded) {
            m_node.updateRwSetting("natpmp", new_natpmp);
        }
        Q_EMIT natpmpChanged(new_natpmp);
    }
}

void OptionsQmlModel::setPrune(bool new_prune)
{
    if (new_prune != m_prune) {
        m_prune = new_prune;
        if (m_onboarded) {
            m_node.updateRwSetting("prune", pruneSetting());
        }
        Q_EMIT pruneChanged(new_prune);
    }
}

void OptionsQmlModel::setPruneSizeGB(int new_prune_size_gb)
{
    if (new_prune_size_gb != m_prune_size_gb) {
        m_prune_size_gb = new_prune_size_gb;
        if (m_onboarded) {
            m_node.updateRwSetting("prune", pruneSetting());
        }
        Q_EMIT pruneSizeGBChanged(new_prune_size_gb);
    }
}

void OptionsQmlModel::setScriptThreads(int new_script_threads)
{
    if (new_script_threads != m_script_threads) {
        m_script_threads = new_script_threads;
        if (m_onboarded) {
            m_node.updateRwSetting("par", new_script_threads);
        }
        Q_EMIT scriptThreadsChanged(new_script_threads);
    }
}

void OptionsQmlModel::setServer(bool new_server)
{
    if (new_server != m_server) {
        m_server = new_server;
        if (m_onboarded) {
            m_node.updateRwSetting("server", new_server);
        }
        Q_EMIT serverChanged(new_server);
    }
}

void OptionsQmlModel::setProxyEnabled(bool enabled)
{
    if (enabled != m_proxy_enabled) {
        bool was_dirty = proxySettingsDirty();
        m_proxy_enabled = enabled;
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
    if (!validateFreshDataDirPath(normalized_path)) return false;

    if (m_custom_datadir_string != normalized_path) {
        m_custom_datadir_string = normalized_path;
        Q_EMIT customDataDirStringChanged(m_custom_datadir_string);
    }
    if (m_dataDir != normalized_path) {
        m_dataDir = normalized_path;
        Q_EMIT dataDirChanged(m_dataDir);
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

    if (!validateFreshDataDirPath(selected_data_dir)) return;

    if (selected_data_dir == m_dataDir && (use_default ? m_custom_datadir_string.isEmpty() : m_custom_datadir_string == selected_data_dir)) {
        setDataDirError({});
        return;
    }

    if (use_default) {
        m_custom_datadir_string.clear();
        Q_EMIT customDataDirStringChanged(m_custom_datadir_string);
    } else if (m_custom_datadir_string != selected_data_dir) {
        m_custom_datadir_string = selected_data_dir;
        Q_EMIT customDataDirStringChanged(m_custom_datadir_string);
    }

    if (m_dataDir != selected_data_dir) {
        m_dataDir = selected_data_dir;
        Q_EMIT dataDirChanged(m_dataDir);
    }
    setDataDirError({});
}

void OptionsQmlModel::setDataDirError(const QString& error)
{
    if (error == m_data_dir_error) return;
    m_data_dir_error = error;
    Q_EMIT dataDirErrorChanged(m_data_dir_error);
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
    return ExistsNoThrow(data_dir / BITCOIN_CONF_FILENAME) ||
           ExistsNoThrow(network_dir / BITCOIN_SETTINGS_FILENAME) ||
           ExistsNoThrow(network_dir / "blocks" / "index") ||
           ExistsNoThrow(network_dir / "chainstate") ||
           ExistsNoThrow(network_dir / "wallets");
}

bool OptionsQmlModel::validateDataDirPath(const QString& path)
{
    QFileInfo info(path);
    if (info.exists() && !info.isDir()) {
        setDataDirError(tr("The selected data directory path is not a directory."));
        return false;
    }

    try {
        fs::path parent_dir = fs::PathFromString(path.toStdString());
        fs::path previous_parent_dir;
        while (parent_dir.has_parent_path() && !fs::exists(parent_dir)) {
            parent_dir = parent_dir.parent_path();
            if (parent_dir == previous_parent_dir) break;
            previous_parent_dir = parent_dir;
        }
        const auto space_info = fs::space(parent_dir);
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

bool OptionsQmlModel::validateFreshDataDirPath(const QString& path)
{
    if (!validateDataDirPath(path)) return false;
    if (!m_onboarded && existingCoreDataDir(path)) {
        setDataDirError(tr("This folder already contains Bitcoin Core data. Reusing existing profiles is not supported by this onboarding flow yet."));
        return false;
    }
    return true;
}

bool OptionsQmlModel::validateDataDirSelection()
{
    return validateFreshDataDirPath(m_dataDir);
}

bool OptionsQmlModel::commitDataDir()
{
    if (!validateFreshDataDirPath(m_dataDir)) return false;

    const QString default_data_dir = getDefaultDataDirString();
    if (m_dataDir == default_data_dir) {
        m_args.LockSettings([](common::Settings& settings) {
            settings.forced_settings.erase("datadir");
        });
    } else {
        if (!validateDataDirPath(m_dataDir)) return false;
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
        m_args.SoftSetArg("-datadir", fs::PathToString(data_dir));
    }
    m_args.ClearPathCache();

    if (m_initialize_config_on_onboard) {
        if (auto error = common::InitConfig(m_args)) {
            setDataDirError(QString::fromStdString(error->message.translated));
            return false;
        }

        InitLogging(m_args);
        InitParameterInteraction(m_args);
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
        Q_EMIT displayUnitChanged(m_display_unit);
    }
}

QString OptionsQmlModel::displayUnitLabel() const
{
    return (m_display_unit == 1) ? QStringLiteral("sat") : QStringLiteral("BTC");
}

QString OptionsQmlModel::displayUnitLabelForAmount(qint64 satoshi) const
{
    if (m_display_unit != 1) return QStringLiteral("₿");
    return (qAbs(satoshi) == 1) ? QStringLiteral("sat") : QStringLiteral("sats");
}


bool OptionsQmlModel::onboard()
{
    if (!commitDataDir()) return false;

    m_node.resetSettings();
    QSettings settings(QSettings::UserScope, QAPP_ORG_NAME, QAPP_APP_NAME_DEFAULT);
    if (m_dataDir == getDefaultDataDirString()) {
        settings.remove(SettingsKeys::DATA_DIR);
    } else {
        settings.setValue(SettingsKeys::DATA_DIR, m_dataDir);
    }
    if (m_external_signer_path.isEmpty()) {
        m_node.forceSetting("signer", common::SettingsValue{});
    } else {
        m_node.forceSetting("signer", m_external_signer_path.toStdString());
    }
    if (m_dbcache_size_mib != DEFAULT_DB_CACHE >> 20) {
        m_node.updateRwSetting("dbcache", m_dbcache_size_mib);
    }
    if (m_listen) {
        m_node.updateRwSetting("listen", m_listen);
    }
    if (m_natpmp) {
        m_node.updateRwSetting("natpmp", m_natpmp);
    }
    if (m_prune) {
        m_node.updateRwSetting("prune", pruneSetting());
    }
    if (m_script_threads != DEFAULT_SCRIPTCHECK_THREADS) {
        m_node.updateRwSetting("par", m_script_threads);
    }
    if (m_server) {
        m_node.updateRwSetting("server", m_server);
    }
    if (m_proxy_enabled && !m_proxy_address.isEmpty()) {
        m_node.updateRwSetting("proxy", m_proxy_address.toStdString());
    }
    if (m_tor_enabled && !m_tor_address.isEmpty()) {
        m_node.updateRwSetting("onion", m_tor_address.toStdString());
    }
    if (!m_external_signer_path.isEmpty()) {
        m_node.updateRwSetting("signer", m_external_signer_path.toStdString());
    }
    m_onboarded = true;
    m_initial_proxy_enabled = m_proxy_enabled;
    m_initial_proxy_address = m_proxy_address;
    m_initial_tor_enabled   = m_tor_enabled;
    m_initial_tor_address   = m_tor_address;
    m_initial_external_signer_path = m_external_signer_path;
    return true;
}
