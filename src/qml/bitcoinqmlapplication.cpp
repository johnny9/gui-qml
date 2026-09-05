// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>

#include <chainparams.h>
#include <clientversion.h>
#include <common/args.h>
#include <init.h>
#include <interfaces/chain.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <qml/appmode.h>
#include <qml/applicationrouter.h>
#include <qml/translationmanager.h>
#include <qml/models/languagesettingsmodel.h>
#include <qml/models/chainsyncmodel.h>
#include <qml/models/nodenetworkmodel.h>
#include <qml/models/runtimedialogmodel.h>
#include <qml/models/mempoolmodel.h>
#include <qml/models/nodeinformationmodel.h>
#include <qml/buildinfo.h>
#include <qml/clipboard.h>
#include <qml/components/blockclockdial.h>
#include <qml/controls/linegraph.h>
#include <qml/guiconstants.h>
#include <qml/imageprovider.h>
#include <qml/initexecutor.h>
#include <qml/models/banlistmodel.h>
#include <qml/models/chainmodel.h>
#include <qml/models/debuglogmodel.h>
#include <qml/models/desktoptrayiconcontroller.h>
#include <qml/models/desktopwindowbehaviormodel.h>
#include <qml/models/networkstatusmodel.h>
#include <qml/models/networktraffictower.h>
#include <qml/models/nodemodel.h>
#include <qml/models/options_model.h>
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peerlistmodel.h>
#include <qml/models/peerlistsortproxy.h>
#include <qml/models/rpcconsolemodel.h>
#include <qml/networkstyle.h>
#include <qml/test/testbridge.h>
#ifdef ENABLE_WALLET
#include <qml/wallet/walletmanager.h>
#include <qml/wallet/walletqrimageprovider.h>
#endif

#include <QMetaType>
#include <QJSEngine>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSettings>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QUrl>

#include <cassert>
#include <cstdlib>
#include <memory>

namespace {
void RegisterQmlTypes(AppMode& app_mode, BuildInfo& build_info, Clipboard& clipboard)
{
    static bool registered{false};
    static AppMode* app_mode_instance{nullptr};
    static BuildInfo* build_info_instance{nullptr};
    static Clipboard* clipboard_instance{nullptr};
    if (registered) return;

    app_mode_instance = &app_mode;
    build_info_instance = &build_info;
    clipboard_instance = &clipboard;
    qmlRegisterSingletonType<AppMode>("org.bitcoincore.qt", 1, 0, "AppMode", [](QQmlEngine*, QJSEngine*) -> QObject* {
        QQmlEngine::setObjectOwnership(app_mode_instance, QQmlEngine::CppOwnership);
        return app_mode_instance;
    });
    qmlRegisterSingletonType<BuildInfo>("org.bitcoincore.qt", 1, 0, "BuildInfo", [](QQmlEngine*, QJSEngine*) -> QObject* {
        QQmlEngine::setObjectOwnership(build_info_instance, QQmlEngine::CppOwnership);
        return build_info_instance;
    });
    qmlRegisterSingletonType<Clipboard>("org.bitcoincore.qt", 1, 0, "Clipboard", [](QQmlEngine*, QJSEngine*) -> QObject* {
        QQmlEngine::setObjectOwnership(clipboard_instance, QQmlEngine::CppOwnership);
        return clipboard_instance;
    });
    qmlRegisterType<BlockClockDial>("org.bitcoincore.qt", 1, 0, "BlockClockDial");
    qmlRegisterType<LineGraph>("org.bitcoincore.qt", 1, 0, "LineGraph");
    qmlRegisterUncreatableType<PeerDetailsModel>("org.bitcoincore.qt", 1, 0, "PeerDetailsModel", "");
    qmlRegisterUncreatableType<DebugLogModel>("org.bitcoincore.qt", 1, 0, "DebugLogModel", "");
    qmlRegisterUncreatableType<RpcConsoleModel>("org.bitcoincore.qt", 1, 0, "RpcConsoleModel", "");
    registered = true;
}
} // namespace

BitcoinQmlApplication::BitcoinQmlApplication(int& argc, char** argv)
    : QApplication{argc, argv}
{
#ifdef ENABLE_WALLET
    Q_INIT_RESOURCE(bitcoin_wallet);
    qmlRegisterUncreatableType<WalletManager>("org.bitcoincore.qt", 1, 0, "WalletManager", "Owned by the application");
    qmlRegisterUncreatableType<WalletViewModel>("org.bitcoincore.qt", 1, 0, "WalletViewModel", "Owned by the wallet manager");
    qmlRegisterUncreatableType<TransactionHistoryModel>("org.bitcoincore.qt", 1, 0, "TransactionHistoryModel", "Owned by the wallet view");
    qmlRegisterUncreatableType<WalletReceiveModel>("org.bitcoincore.qt", 1, 0, "WalletReceiveModel", "Owned by the wallet view");
    qmlRegisterUncreatableType<ReceiveRequestHistoryModel>("org.bitcoincore.qt", 1, 0, "ReceiveRequestHistoryModel", "Owned by the receive workflow");
    qmlRegisterUncreatableType<PaymentRequest>("org.bitcoincore.qt", 1, 0, "PaymentRequest", "Owned by the receive workflow");
    qmlRegisterUncreatableType<WalletSendModel>("org.bitcoincore.qt", 1, 0, "WalletSendModel", "Owned by the wallet view");
    qmlRegisterUncreatableType<SendRecipientsListModel>("org.bitcoincore.qt", 1, 0, "SendRecipientsListModel", "Owned by the send workflow");
    qmlRegisterUncreatableType<WalletOverviewModel>("org.bitcoincore.qt", 1, 0, "WalletOverviewModel", "Owned by the wallet view");
    qmlRegisterUncreatableType<WalletSecurityModel>("org.bitcoincore.qt", 1, 0, "WalletSecurityModel", "Owned by the wallet view");
    qmlRegisterUncreatableType<WalletStorageModel>("org.bitcoincore.qt", 1, 0, "WalletStorageModel", "Owned by the wallet view");
    qmlRegisterUncreatableType<WalletListModel>("org.bitcoincore.qt", 1, 0, "WalletListModel", "Owned by the wallet manager");
    qmlRegisterUncreatableType<WalletCreationModel>("org.bitcoincore.qt", 1, 0, "WalletCreationModel", "Owned by the wallet manager");
    qmlRegisterUncreatableType<WalletImportModel>("org.bitcoincore.qt", 1, 0, "WalletImportModel", "Owned by the wallet manager");
    qmlRegisterUncreatableType<WalletMigrationModel>("org.bitcoincore.qt", 1, 0, "WalletMigrationModel", "Owned by the wallet manager");
#endif
    setOrganizationName(QStringLiteral(QAPP_ORG_NAME));
    setOrganizationDomain(QStringLiteral(QAPP_ORG_DOMAIN));
    setApplicationName(QStringLiteral(QAPP_APP_NAME_DEFAULT));
#ifdef __ANDROID__
    m_app_mode = std::make_unique<AppMode>(AppMode::MOBILE);
#else
    m_app_mode = std::make_unique<AppMode>(AppMode::DESKTOP);
#endif
    m_build_info = std::make_unique<BuildInfo>();
    m_clipboard = std::make_unique<Clipboard>();
    m_translations = std::make_unique<TranslationManager>();
    RegisterQmlTypes(*m_app_mode, *m_build_info, *m_clipboard);
    setApplicationDisplayName(translate("bitcoin-core", "Bitcoin Core"));
    setQuitOnLastWindowClosed(false);
}

BitcoinQmlApplication::~BitcoinQmlApplication()
{
    // A Core initialization worker may be waiting for an unanswered GUI prompt.
    if (m_runtime_dialog_model) m_runtime_dialog_model->stop();
    // Interrupt blocking RPCs before joining their worker during fallback
    // teardown (for example when QML window creation failed).
    if (m_node && m_base_initialized && !m_shutdown_complete) m_node->startShutdown();
    m_test_bridge.reset();
    m_engine.reset();
    m_language_settings_model.reset();
#ifdef ENABLE_WALLET
    m_wallet_manager.reset();
#endif
    m_options_model.reset();
    m_node_information_model.reset();
    m_mempool_model.reset();
    m_node_network_model.reset();
    m_chain_sync_model.reset();
    m_navigation_model.reset();
    m_router.reset();
    m_rpc_console_model.reset();
    m_debug_log_model.reset();
    m_ban_list_model.reset();
    m_peer_model_sort_proxy.reset();
    m_peer_model.reset();
    m_desktop_tray_icon_controller.reset();
    m_desktop_window_behavior_model.reset();
    m_chain_model.reset();
    m_network_status_model.reset();
    m_network_traffic_tower.reset();
    m_network_style.reset();
    m_clipboard.reset();
    m_build_info.reset();
    m_app_mode.reset();
    m_init_executor.reset();
    m_node_model.reset();
    m_runtime_dialog_model.reset();
    m_chain.reset();
    if (m_node && m_base_initialized && !m_shutdown_complete) {
        m_node->startShutdown();
        m_node->appShutdown();
    }
}

void BitcoinQmlApplication::parameterSetup()
{
    gArgs.SoftSetBoolArg("-printtoconsole", false);
    InitLogging(gArgs);
    InitParameterInteraction(gArgs);
}

void BitcoinQmlApplication::createNode(interfaces::Init& init)
{
    assert(!m_node);
    assert(!m_chain);
    m_node = init.makeNode();
    m_chain = init.makeChain();
}

bool BitcoinQmlApplication::baseInitialize()
{
    assert(m_node);
    m_base_initialized = m_node->baseInitialize();
    return m_base_initialized;
}

bool BitcoinQmlApplication::createWindow()
{
    assert(m_node);
    assert(m_chain);
    assert(!m_node_model);

    qRegisterMetaType<interfaces::BlockAndHeaderTipInfo>("interfaces::BlockAndHeaderTipInfo");

    m_runtime_dialog_model = std::make_unique<RuntimeDialogModel>(*m_node);
    m_runtime_dialog_model->addStartupWarnings(m_startup_warnings);
    m_node_model = std::make_unique<NodeLifecycleModel>(*m_node, m_runtime_dialog_model.get());
    m_chain_sync_model = std::make_unique<ChainSyncModel>(*m_node);
    m_node_network_model = std::make_unique<NodeNetworkModel>(*m_node);
    m_mempool_model = std::make_unique<MempoolModel>(*m_node);
    m_node_information_model = std::make_unique<NodeInformationModel>(*m_node, *m_chain_sync_model, *m_node_network_model, *m_runtime_dialog_model);
    m_router = std::make_unique<ApplicationRouter>();
    m_router->registerDestination({QStringLiteral("node"), QUrl{QStringLiteral("qrc:///qml/pages/node/NodeRunner.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Node"), true, QStringLiteral("")});
    m_router->registerDestination({QStringLiteral("peers"), QUrl{QStringLiteral("qrc:///qml/pages/node/Peers.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Peers"), true, QStringLiteral("")});
    m_router->registerDestination({QStringLiteral("banned-peers"), QUrl{QStringLiteral("qrc:///qml/pages/node/BannedPeers.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Banned peers"), true, QStringLiteral("")});
    m_router->registerDestination({QStringLiteral("traffic"), QUrl{QStringLiteral("qrc:///qml/pages/node/NetworkTraffic.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Network traffic"), true, QStringLiteral("")});
    m_router->registerDestination({QStringLiteral("mempool"), QUrl{QStringLiteral("qrc:///qml/pages/node/MempoolInformationSettings.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Mempool"), true, QStringLiteral("")});
    m_router->registerDestination({QStringLiteral("console"), QUrl{QStringLiteral("qrc:///qml/pages/node/CommandConsole.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Console"), true, QStringLiteral("")});
    m_router->registerDestination({QStringLiteral("debug-log"), QUrl{QStringLiteral("qrc:///qml/pages/settings/SettingsDebugLog.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Debug log"), true, QStringLiteral("")});
    m_router->registerDestination({QStringLiteral("settings"), QUrl{QStringLiteral("qrc:///qml/pages/settings/SettingsDisplay.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Settings"), true, QStringLiteral("")});
    m_router->registerDestination({QStringLiteral("peer-details"), QUrl{QStringLiteral("qrc:///qml/pages/node/PeerDetails.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Peer details"), false, QStringLiteral("peers")});
    m_router->registerDestination({QStringLiteral("shutdown"), QUrl{QStringLiteral("qrc:///qml/pages/node/Shutdown.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Shutting down"), false, QStringLiteral("")});
    m_router->registerDestination({QStringLiteral("settings/window"), QUrl{QStringLiteral("qrc:///qml/pages/settings/SettingsWindowBehavior.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Window"), false, QStringLiteral("settings")});
    m_router->registerDestination({QStringLiteral("settings/storage"), QUrl{QStringLiteral("qrc:///qml/pages/settings/SettingsStorage.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Storage"), false, QStringLiteral("settings")});
    m_router->registerDestination({QStringLiteral("settings/connection"), QUrl{QStringLiteral("qrc:///qml/pages/settings/SettingsConnection.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Connection"), false, QStringLiteral("settings")});
    m_router->registerDestination({QStringLiteral("settings/about"), QUrl{QStringLiteral("qrc:///qml/pages/settings/SettingsAbout.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "About"), false, QStringLiteral("settings")});
    m_router->registerDestination({QStringLiteral("settings/theme"), QUrl{QStringLiteral("qrc:///qml/pages/settings/SettingsTheme.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Theme"), false, QStringLiteral("settings")});
    m_router->registerDestination({QStringLiteral("settings/block-clock"), QUrl{QStringLiteral("qrc:///qml/pages/settings/SettingsBlockClockDisplayMode.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Block clock"), false, QStringLiteral("settings")});
    m_router->registerDestination({QStringLiteral("settings/unit"), QUrl{QStringLiteral("qrc:///qml/pages/settings/SettingsDisplayUnit.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Display unit"), false, QStringLiteral("settings")});
    m_router->registerDestination({QStringLiteral("settings/language"), QUrl{QStringLiteral("qrc:///qml/pages/settings/SettingsLanguage.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Language"), false, QStringLiteral("settings")});
    m_router->registerDestination({QStringLiteral("settings/design-system"), QUrl{QStringLiteral("qrc:///qml/pages/settings/SettingsDesignSystem.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Design system"), false, QStringLiteral("settings")});
    m_router->registerDestination({QStringLiteral("settings/developer"), QUrl{QStringLiteral("qrc:///qml/pages/settings/SettingsDeveloper.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Developer"), false, QStringLiteral("settings")});
    m_router->registerDestination({QStringLiteral("settings/proxy"), QUrl{QStringLiteral("qrc:///qml/pages/settings/SettingsProxy.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Proxy"), false, QStringLiteral("settings")});
    m_router->navigate(QStringLiteral("node"));
    m_navigation_model = std::make_unique<NavigationModel>(*m_router);
    connect(m_translations.get(), &TranslationManager::languageChanged, m_router.get(), &ApplicationRouter::retranslate);
    connect(m_node_model.get(), &NodeLifecycleModel::requestedShutdown, m_router.get(), &ApplicationRouter::beginShutdown);
    connect(m_node_model.get(), &NodeLifecycleModel::requestedShutdown, m_chain_sync_model.get(), &ChainSyncModel::stop);
    connect(m_node_model.get(), &NodeLifecycleModel::requestedShutdown, m_node_network_model.get(), &NodeNetworkModel::stop);
    connect(m_node_model.get(), &NodeLifecycleModel::requestedShutdown, m_mempool_model.get(), &MempoolModel::stop);
    connect(m_node_model.get(), &NodeLifecycleModel::requestedShutdown, m_node_information_model.get(), [this] { m_node_information_model->setReady(false); });
    m_init_executor = std::make_unique<QmlInitExecutor>(*m_node);
    connect(m_node_model.get(), &NodeLifecycleModel::requestedInitialize, m_init_executor.get(), &QmlInitExecutor::initialize);
    connect(m_init_executor.get(), &QmlInitExecutor::initializeResult, m_node_model.get(), &NodeLifecycleModel::initializeResult);
    connect(m_init_executor.get(), &QmlInitExecutor::initializeResult, m_chain_sync_model.get(), &ChainSyncModel::initializeResult);
    connect(m_init_executor.get(), &QmlInitExecutor::initializeResult, m_mempool_model.get(), &MempoolModel::initializeResult);
    connect(m_node_model.get(), &NodeLifecycleModel::initializationFinished, m_node_information_model.get(), &NodeInformationModel::setReady);
    connect(m_node_model.get(), &NodeLifecycleModel::nodeInitialized, m_node_network_model.get(), &NodeNetworkModel::refreshPeerCounts);
    connect(m_init_executor.get(), &QmlInitExecutor::shutdownResult, m_node_model.get(), &NodeLifecycleModel::shutdownResult);
#ifdef ENABLE_WALLET
    if (!gArgs.GetBoolArg("-disablewallet", false)) {
        m_wallet_manager = std::make_unique<WalletManager>(*m_node, QString::fromStdString(gArgs.GetChainTypeString()));
        m_router->registerDestination({QStringLiteral("wallet/send"), QUrl{QStringLiteral("qrc:///qml/pages/wallet/Send.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Send"), false, QStringLiteral("wallets"), false});
        connect(m_wallet_manager.get(), &WalletManager::selectedWalletChanged, m_router.get(), [this] {
            // Drop routes borrowing the old wallet before it can be destroyed.
            m_router->setDestinationEnabled(QStringLiteral("wallet/send"), false);
            auto* selected = m_wallet_manager->selectedWallet();
            m_router->setDestinationEnabled(QStringLiteral("wallet/send"), selected && selected->send()->available());
        });
        m_router->registerDestination({QStringLiteral("wallets"), QUrl{QStringLiteral("qrc:///qml/pages/wallet/WalletOverview.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Wallets"), true, {}, false});
        m_router->registerDestination({QStringLiteral("wallet-activity"), QUrl{QStringLiteral("qrc:///qml/pages/wallet/WalletActivity.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Activity"), false, QStringLiteral("wallets"), false});
        m_router->registerDestination({QStringLiteral("wallet-receive"), QUrl{QStringLiteral("qrc:///qml/pages/wallet/WalletReceive.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Receive"), false, QStringLiteral("wallets"), false});
        m_router->registerDestination({QStringLiteral("wallet-addresses"), QUrl{QStringLiteral("qrc:///qml/pages/wallet/WalletAddresses.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Addresses"), false, QStringLiteral("wallets"), false});
        m_router->registerDestination({QStringLiteral("wallet-address"), QUrl{QStringLiteral("qrc:///qml/pages/wallet/WalletAddressDetails.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Address details"), false, QStringLiteral("wallets"), false});
        m_router->registerDestination({QStringLiteral("wallet-message"), QUrl{QStringLiteral("qrc:///qml/pages/wallet/WalletMessage.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Sign / verify message"), false, QStringLiteral("wallets"), false});
        m_router->registerDestination({QStringLiteral("wallet-transaction"), QUrl{QStringLiteral("qrc:///qml/pages/wallet/WalletTransactionDetails.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Transaction details"), false, QStringLiteral("wallets"), false});
        connect(m_wallet_manager.get(), &WalletManager::selectedWalletChanged, m_router.get(), [this] {
            const bool selected{m_wallet_manager->selectedWallet() != nullptr};
            m_router->setDestinationEnabled(QStringLiteral("wallet-activity"), selected);
            m_router->setDestinationEnabled(QStringLiteral("wallet-receive"), selected);
            m_router->setDestinationEnabled(QStringLiteral("wallet-addresses"), selected);
            m_router->setDestinationEnabled(QStringLiteral("wallet-address"), selected);
            m_router->setDestinationEnabled(QStringLiteral("wallet-message"), selected);
            m_router->setDestinationEnabled(QStringLiteral("wallet-transaction"), selected);
        });
        connect(m_wallet_manager.get(), &WalletManager::initializedChanged, m_router.get(), [this] {
            m_router->setDestinationEnabled(QStringLiteral("wallets"), m_wallet_manager->initialized());
        });
        connect(m_node_model.get(), &NodeLifecycleModel::nodeInitialized, m_wallet_manager.get(), &WalletManager::initialize);
        connect(m_wallet_manager.get(), &WalletManager::drained, m_init_executor.get(), &QmlInitExecutor::shutdown);
    }
#endif
    connect(m_init_executor.get(), &QmlInitExecutor::runawayException, this, &BitcoinQmlApplication::handleRunawayException);
    connect(m_node_model.get(), &NodeLifecycleModel::shutdownComplete, this, [this] {
        m_shutdown_complete = true;
        exit(node().getExitStatus());
    });

    m_network_traffic_tower = std::make_unique<NetworkTrafficTower>(*m_node);
    m_network_status_model = std::make_unique<NetworkStatusModel>();
    m_chain_model = std::make_unique<ChainModel>(*m_chain);
    m_chain_model->setCurrentNetworkName(QString::fromStdString(gArgs.GetChainTypeString()));

    if (gArgs.IsArgSet("-resetguisettings")) {
        QSettings settings;
        settings.remove(QStringLiteral("fHideTrayIcon"));
        settings.remove(QStringLiteral("fMinimizeToTray"));
        settings.remove(QStringLiteral("fMinimizeOnClose"));
    }

    connect(m_chain_sync_model.get(), &ChainSyncModel::setTimeRatioList, m_chain_model.get(), &ChainModel::setTimeRatioList);
    connect(m_chain_sync_model.get(), &ChainSyncModel::setTimeRatioListInitial, m_chain_model.get(), &ChainModel::setTimeRatioListInitial);

    m_desktop_window_behavior_model = std::make_unique<DesktopWindowBehaviorModel>();
    m_desktop_tray_icon_controller = std::make_unique<DesktopTrayIconController>();
    m_desktop_tray_icon_controller->setRouter(*m_router);
    connect(m_translations.get(), &TranslationManager::languageChanged,
        m_desktop_tray_icon_controller.get(), &DesktopTrayIconController::retranslate);
    connect(this, &QGuiApplication::lastWindowClosed, this, [this] {
        if (m_desktop_tray_icon_controller && m_desktop_tray_icon_controller->visible()) return;
        requestShutdown();
    });

    m_peer_model = std::make_unique<PeerListModel>(*m_node, nullptr);
    connect(m_node_model.get(), &NodeLifecycleModel::requestedShutdown, m_peer_model.get(), &PeerListModel::stop);
    m_peer_model_sort_proxy = std::make_unique<PeerListSortProxy>(nullptr);
    m_peer_model_sort_proxy->setSourceModel(m_peer_model.get());
    connect(m_router.get(), &ApplicationRouter::routeChanged, m_peer_model.get(), [this] {
        const auto route = m_router->currentRoute();
        if (!m_router->shuttingDown() && (route == QStringLiteral("peers") || route == QStringLiteral("peer-details"))) {
            m_peer_model->startAutoRefresh();
        } else {
            m_peer_model->stopAutoRefresh();
        }
    });

    m_ban_list_model = std::make_unique<BanListModel>(*m_node);
    connect(m_node_model.get(), &NodeLifecycleModel::requestedShutdown, m_ban_list_model.get(), &BanListModel::stop);
    connect(m_node_model.get(), &NodeLifecycleModel::nodeInitialized, m_ban_list_model.get(), &BanListModel::refresh);

    m_debug_log_model = std::make_unique<DebugLogModel>(gArgs.GetDataDirNet() / "debug.log");
    m_rpc_console_model = std::make_unique<RpcConsoleModel>(*m_node);
    connect(m_node_model.get(), &NodeLifecycleModel::requestedShutdown, m_rpc_console_model.get(), &RpcConsoleModel::stop);
    connect(m_node_model.get(), &NodeLifecycleModel::requestedShutdown, m_network_traffic_tower.get(), &NetworkTrafficTower::stop);
    connect(m_node_model.get(), &NodeLifecycleModel::requestedShutdown, m_debug_log_model.get(), &DebugLogModel::stop);
    connect(m_node_model.get(), &NodeLifecycleModel::nodeInitialized, m_rpc_console_model.get(), &RpcConsoleModel::onNodeInitialized);
    m_options_model = std::make_unique<OptionsQmlModel>(*m_node);

    m_network_style.reset(NetworkStyle::instantiate(Params().GetChainType()));
    assert(m_network_style);
    setApplicationName(m_network_style->getAppName());
    setWindowIcon(m_network_style->getAppIcon());

    m_language_settings_model = std::make_unique<LanguageSettingsModel>(*m_node, gArgs, *m_translations);
    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_translations->attachEngine(*m_engine);
#ifdef ENABLE_WALLET
    m_engine->rootContext()->setContextProperty("walletManager", m_wallet_manager.get());
    if (m_wallet_manager) m_engine->addImageProvider(QStringLiteral("walletqr"), new WalletQRImageProvider);
#ifdef USE_QRCODE
    m_engine->rootContext()->setContextProperty("walletQrAvailable", m_wallet_manager != nullptr);
#else
    m_engine->rootContext()->setContextProperty("walletQrAvailable", false);
#endif
#else
    m_engine->rootContext()->setContextProperty("walletManager", static_cast<QObject*>(nullptr));
#endif
    m_engine->addImageProvider(QStringLiteral("images"), new ImageProvider{m_network_style.get()});
    QQmlContext* const context{m_engine->rootContext()};
    context->setContextProperty(QStringLiteral("networkTrafficTower"), m_network_traffic_tower.get());
    context->setContextProperty(QStringLiteral("networkStatusModel"), m_network_status_model.get());
    context->setContextProperty(QStringLiteral("nodeLifecycleModel"), m_node_model.get());
    context->setContextProperty(QStringLiteral("chainSyncModel"), m_chain_sync_model.get());
    context->setContextProperty(QStringLiteral("nodeNetworkModel"), m_node_network_model.get());
    context->setContextProperty(QStringLiteral("runtimeDialogModel"), m_runtime_dialog_model.get());
    context->setContextProperty(QStringLiteral("mempoolModel"), m_mempool_model.get());
    context->setContextProperty(QStringLiteral("nodeInformationModel"), m_node_information_model.get());
    context->setContextProperty(QStringLiteral("applicationRouter"), m_router.get());
    context->setContextProperty(QStringLiteral("navigationModel"), m_navigation_model.get());
    context->setContextProperty(QStringLiteral("languageSettingsModel"), m_language_settings_model.get());
    context->setContextProperty(QStringLiteral("chainModel"), m_chain_model.get());
    context->setContextProperty(QStringLiteral("peerTableModel"), m_peer_model.get());
    context->setContextProperty(QStringLiteral("peerListModelProxy"), m_peer_model_sort_proxy.get());
    context->setContextProperty(QStringLiteral("banListModel"), m_ban_list_model.get());
    context->setContextProperty(QStringLiteral("debugLogModel"), m_debug_log_model.get());
    context->setContextProperty(QStringLiteral("rpcConsoleModel"), m_rpc_console_model.get());
    context->setContextProperty(QStringLiteral("optionsModel"), m_options_model.get());
    context->setContextProperty(QStringLiteral("desktopWindowBehaviorModel"), m_desktop_window_behavior_model.get());
    context->setContextProperty(QStringLiteral("desktopTrayIconController"), m_desktop_tray_icon_controller.get());

    m_desktop_tray_icon_controller->setBasePixmap(
        m_network_style->getTrayAndWindowIcon().pixmap(QSize(256, 256)));
    m_desktop_tray_icon_controller->setToolTip(
        QString(tr("%1 client").arg(CLIENT_NAME) + " " + m_network_style->getTitleAddText()).trimmed());
    m_desktop_tray_icon_controller->setVisible(
        m_desktop_window_behavior_model->desktopPlatform() && m_desktop_window_behavior_model->showTrayIcon());
    connect(m_desktop_tray_icon_controller.get(), &DesktopTrayIconController::supportedChanged,
        m_desktop_window_behavior_model.get(), [this](bool supported) {
            if (!supported) m_desktop_window_behavior_model->setShowTrayIcon(false);
        });

    m_engine->load(QUrl{QStringLiteral("qrc:///qml/pages/MainWindow.qml")});
    if (m_engine->rootObjects().isEmpty()) return false;

    auto* const window{qobject_cast<QQuickWindow*>(m_engine->rootObjects().constFirst())};
    if (!window) return false;
    m_desktop_tray_icon_controller->setMainWindow(window);
    if (m_initial_window_geometry.isValid()) window->setGeometry(m_initial_window_geometry);
    m_node_model->startShutdownPolling();
    // NodeLifecycleModel interrupts Core first. All preceding direct slots drain
    // feature workers before this last slot queues destruction of Core state.
    connect(m_node_model.get(), &NodeLifecycleModel::requestedShutdown, this, [this] {
#ifdef ENABLE_WALLET
        if (m_wallet_manager) { m_wallet_manager->shutdown(); return; }
#endif
        m_init_executor->shutdown();
    });
    return true;
}

bool BitcoinQmlApplication::createTestBridge(const QString& socket_path)
{
    assert(m_engine);
    assert(!m_test_bridge);
    m_test_bridge = std::make_unique<TestBridge>(*m_engine, socket_path);
    return m_test_bridge->isListening();
}

[[noreturn]] void BitcoinQmlApplication::handleRunawayException(const QString& message)
{
    m_node_model->handleRunawayException(message);
    ::exit(EXIT_FAILURE);
}

void BitcoinQmlApplication::requestInitialize()
{
    assert(m_node_model);
    QTimer::singleShot(0, m_node_model.get(), &NodeLifecycleModel::start);
}

void BitcoinQmlApplication::requestShutdown()
{
    assert(m_node_model);
    m_node_model->requestShutdown();
}

void BitcoinQmlApplication::addStartupWarnings(const QStringList& warnings)
{
    m_startup_warnings = warnings;
}

void BitcoinQmlApplication::setInitialWindowGeometry(const QRect& geometry)
{
    m_initial_window_geometry = geometry;
}

void BitcoinQmlApplication::installLanguage(const QString& language)
{
    m_translations->setLanguage(language);
}

TranslationManager& BitcoinQmlApplication::translations() const
{
    return *m_translations;
}

ApplicationRouter& BitcoinQmlApplication::router() const
{
    assert(m_router);
    return *m_router;
}

interfaces::Node& BitcoinQmlApplication::node() const
{
    assert(m_node);
    return *m_node;
}

NodeLifecycleModel& BitcoinQmlApplication::nodeModel() const
{
    assert(m_node_model);
    return *m_node_model;
}

QQmlApplicationEngine& BitcoinQmlApplication::engine() const
{
    assert(m_engine);
    return *m_engine;
}

WalletManager* BitcoinQmlApplication::walletManager() const
{
#ifdef ENABLE_WALLET
    return m_wallet_manager.get();
#else
    return nullptr;
#endif
}
