// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/atomicsharedptr.h>
#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <qml/models/backuppolicyoperation.h>
#include <qml/bitcoin.h>
#include <qml/bitcoinqmlapplication.h>

#include <common/args.h>
#include <common/init.h>
#include <common/system.h>
#include <chainparams.h>
#include <init.h>
#include <interfaces/chain.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <logging.h>
#include <node/context.h>
#include <node/interface_ui.h>
#include <noui.h>
#include <qml/appmode.h>
#include <qml/applicationmodels.h>
#include <qml/backendexecutor.h>
#include <qml/models/imageexportoperation.h>
#include <qml/quithandler.h>
#include <qml/shutdowncoordinator.h>
#include <qml/startupdialog.h>
#include <qml/startupeffects.h>
#include <qml/qtlogsink.h>
#include <qml/bitcoinamount.h>
#include <qml/buildinfo.h>
#include <qml/clipboard.h>
#include <qml/datadir.h>
#include <qml/guiargs.h>
#include <qml/guipresentation.h>
#include <qml/legacy_settings_migration.h>
#include <qml/onboarding_settings.h>
#include <qml/urlopener.h>
#ifdef __ANDROID__
#include <qml/androidnotifier.h>
#endif
#include <qml/components/blockclockdial.h>
#include <qml/controls/linegraph.h>
#include <qml/guiconstants.h>
#include <qml/imageprovider.h>
#include <qml/initexecutor.h>
#include <qml/models/banlistmodel.h>
#include <qml/models/bitcoinaddress.h>
#include <qml/models/bitcoinurimodel.h>
#include <qml/models/blockclockmodel.h>
#include <qml/models/blockclockhistory.h>
#include <qml/models/chainmodel.h>
#include <qml/models/debuglogmodel.h>
#include <qml/models/flowdetailsmodel.h>
#include <qml/models/networktraffictower.h>
#include <qml/models/networkstatusmodel.h>
#include <qml/models/nodemodel.h>
#include <qml/models/monospaceoutputmodel.h>
#include <qml/models/desktoptrayiconcontroller.h>
#include <qml/models/desktopwindowbehaviormodel.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/models/options_model.h>
#include <qml/models/uipreferences.h>
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peerlistsortproxy.h>
#include <qml/models/peerlistmodel.h>
#include <qml/models/rpcconsolemodel.h>
#include <qml/models/settings_keys.h>
#include <qml/qmlfiledialog.h>
#include <qml/qrimageprovider.h>
#include <qml/networkstyle.h>
#include <qml/nativelistmodel.h>
#include <qml/util.h>
#ifdef ENABLE_TEST_AUTOMATION
#include <qml/test/testbridge.h>
#endif
#include <util/btcsignals.h>
#include <util/fs.h>
#include <util/fs_helpers.h>
#include <util/threadnames.h>
#include <util/translation.h>
#ifdef ENABLE_WALLET
#include <qml/models/activitydetailmodel.h>
#include <wallet/wallet.h>
#include <qml/models/activityfilterproxymodel.h>
#include <qml/models/addresslistmodel.h>
#include <qml/models/bumptransactionmodel.h>
#include <qml/models/paymentrequest.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/walletlistmodel.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <qml/walletqmlcontroller.h>
#endif

#include <cassert>
#include <cstdio>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <tuple>
#include <vector>

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QEventLoop>
#include <QFontDatabase>
#include <QIcon>
#include <QMetaObject>
#include <QPixmap>
#include <QGuiApplication>
#include <QJSEngine>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSettings>
#include <QScopeGuard>
#include <QString>
#include <QStyleHints>
#include <QTranslator>
#include <QThread>
#include <QUrl>
#include <QVariant>

QT_BEGIN_NAMESPACE
class QMessageLogContext;
QT_END_NAMESPACE

#if defined(QT_STATICPLUGIN)
#include <QtPlugin>
Q_IMPORT_PLUGIN(QtQmlPlugin)
Q_IMPORT_PLUGIN(QtQmlModelsPlugin)
Q_IMPORT_PLUGIN(QtQuick2DialogsPlugin)
Q_IMPORT_PLUGIN(QtQuick2DialogsPrivatePlugin)
Q_IMPORT_PLUGIN(QtQuick2Plugin)
Q_IMPORT_PLUGIN(QtQuick2WindowPlugin)
Q_IMPORT_PLUGIN(QtQuickControls1Plugin)
Q_IMPORT_PLUGIN(QmlFolderListModelPlugin)
Q_IMPORT_PLUGIN(QmlSettingsPlugin)
Q_IMPORT_PLUGIN(QtQuickLayoutsPlugin)
Q_IMPORT_PLUGIN(QtQuickControls2Plugin)
Q_IMPORT_PLUGIN(QtLabsPlatformPlugin)
Q_IMPORT_PLUGIN(QtQuickControls2BasicStylePlugin)
Q_IMPORT_PLUGIN(QtQuickControls2BasicStyleImplPlugin)
Q_IMPORT_PLUGIN(QtQuickTemplates2Plugin)
#endif

// Qt emits "OpenType support missing for ..." warnings when BitcoinCoreSans
// lacks glyphs for a script and Qt falls back to another font. These are
// harmless and noisy, so treat them like debug output rather than printing
// them unconditionally. Defined at global scope (not in the anonymous
// namespace below) so the classification can be unit tested.
bool IsBenignQtFontWarning(const QString& msg)
{
    return msg.startsWith(QLatin1String("OpenType support missing for"));
}

namespace {
bool WalletEnabledFromArgs()
{
#ifdef ENABLE_WALLET
    return !gArgs.GetBoolArg("-disablewallet", wallet::DEFAULT_DISABLE_WALLET);
#else
    return false;
#endif
}

AppMode SetupAppMode()
{
    AppMode::Mode mode;
    #ifdef __ANDROID__
        mode = AppMode::MOBILE;
    #else
        mode = AppMode::DESKTOP;
    #endif // __ANDROID__

    return AppMode(mode, WalletEnabledFromArgs());
}

void RegisterQmlTypes(AppMode& app_mode, BuildInfo& build_info, Clipboard& clipboard, UrlOpener& url_opener, BitcoinUriModel& bitcoin_uri_model);

UiPreferencesStore* InstallUiPreferences(QQmlApplicationEngine& engine,
    const execution::SnapshotServices& services, bool await_read)
{
    auto* store = new UiPreferencesStore{services, UiPreferencesNamespace::Capture(), &engine};
    engine.rootContext()->setContextProperty("uiPreferencesStore", store);
    QEventLoop phase;
    QObject::connect(store, &UiPreferencesStore::stateChanged, &phase, &QEventLoop::quit);
    store->start();
    // Only pre-window lifecycle phases await their actual read. An error dialog
    // can be called while its Native producer is awaiting the GUI handoff.
    while (await_read && !store->ready() && store->error().isEmpty()) phase.exec();
    return store;
}

bool InitErrorMessageBox(
    const QString& summary, const execution::SnapshotServices& services,
    std::shared_ptr<const startupdiagnostics::Collection> full_message,
    QmlQuitHandler& quit_handler,
    const std::function<void(QQmlApplicationEngine&)>& engine_created,
    StartupDiagnosticsModel* existing_details = nullptr,
    std::shared_ptr<std::atomic_bool> diagnostic_loss = {})
{
    static AppMode error_app_mode = SetupAppMode();
    static BuildInfo error_build_info;
    static Clipboard error_clipboard;
    static UrlOpener error_url_opener;
    static BitcoinUriModel error_bitcoin_uri_model;
    RegisterQmlTypes(error_app_mode, error_build_info, error_clipboard, error_url_opener, error_bitcoin_uri_model);

    // The source outlives QML root destruction and singleton callbacks.
    std::unique_ptr<StartupDiagnosticsModel> owned_details;
    QQmlApplicationEngine engine;
    engine_created(engine);
    InstallUiPreferences(engine, services, false);

    auto* details = existing_details;
    if (!details) {
        owned_details = std::make_unique<StartupDiagnosticsModel>(services);
        details = owned_details.get();
        if (full_message) details->adopt(std::move(full_message));
        if (!details->project(false, summary, {}, {}, /*normalize=*/false)) {
            if (diagnostic_loss) *diagnostic_loss = true;
            engine.rootContext()->setContextProperty("message", details->error());
        }
    }
    if (engine.rootContext()->contextProperty("message").isNull()) engine.rootContext()->setContextProperty("message", summary);
    QObject::connect(details, &StartupDiagnosticsModel::finished, &engine, [details, diagnostic_loss] {
        if (diagnostic_loss && !details->error().isEmpty()) *diagnostic_loss = true;
    });
    engine.rootContext()->setContextProperty("startupDiagnostics", details);
    engine.load(QUrl(QStringLiteral("qrc:///qml/pages/initerrormessage.qml")));
    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (window) ExecStartupDialog(engine, *window, quit_handler);
    return false;
}

bool RecordStartupWarning(const std::shared_ptr<startupdiagnostics::Collection>& warnings, const bilingual_str& message,
                          const std::shared_ptr<std::atomic_bool>& diagnostic_loss)
{
    if (!warnings) { if (diagnostic_loss) *diagnostic_loss = true; return false; }
    const bool gui = QThread::currentThread() == QCoreApplication::instance()->thread();
    // Production Core startup runs on Native. An accidental GUI diagnostic
    // cannot deep-copy an unbounded std::string into the executor.
    const auto& text = message.translated;
    QString warning = gui ? QString::fromUtf8(text.data(), std::min<size_t>(text.size(), 16384))
                          : QString::fromStdString(text);
    if (gui && text.size() > 16384) {
        if (diagnostic_loss) *diagnostic_loss = true;
        warning += QStringLiteral(" ") + QObject::tr("[complete warning rejected: Core startup work ran on GUI]");
    }
    const bool accepted = warnings->append(std::move(warning), true);
    if (!accepted && diagnostic_loss) *diagnostic_loss = true;
    return accepted;
}

ownership::AtomicSharedPtr<qtlog::Queue> g_qt_log_queue;
ownership::AtomicSharedPtr<qtlog::FatalPolicy> g_qt_fatal_policy;

bool WriteQtLog(const qtlog::Record& record)
{
    if (record.overflow || (record.stage == qtlog::Stage::Startup && record.type != QtDebugMsg)) {
        const auto formatted = qtlog::FormatStderr(record);
        const auto written = std::fwrite(formatted.constData(), 1, static_cast<size_t>(formatted.size()), stderr);
        const auto flushed = std::fflush(stderr);
        return written == static_cast<size_t>(formatted.size()) && flushed == 0;
    }
    const auto message = qtlog::Message(record);
    if (record.type == QtDebugMsg || (record.type == QtWarningMsg && IsBenignQtFontWarning(message)))
        LogDebug(BCLog::QT, "GUI: %s\n", message.toStdString());
    else LogInfo("GUI: %s\n", message.toStdString());
    // The public Core logger returns void and does not expose fwrite status.
    // This acknowledgement proves Native handoff, not debug.log durability.
    return true;
}
void ForwardQtLog(QtMsgType type, const QMessageLogContext& context, const QString& message, qtlog::Stage stage)
{
    qtlog::Forward(type, context, message, stage, g_qt_fatal_policy.load(), g_qt_log_queue.load());
}
void DebugMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{ ForwardQtLog(type, context, message, qtlog::Stage::Main); }
void StartupDebugMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{ ForwardQtLog(type, context, message, qtlog::Stage::Startup); }

QString ChainQSettingsName(const QString& chain)
{
    if (chain.compare("MAIN") == 0) {
        return QAPP_APP_NAME_DEFAULT;
    } else if (chain.compare("TEST") == 0) {
        return QAPP_APP_NAME_TESTNET;
    } else if (chain.compare("TESTNET4") == 0) {
        return QAPP_APP_NAME_TESTNET4;
    } else if (chain.compare("SIGNET") == 0) {
        return QAPP_APP_NAME_SIGNET;
    } else if (chain.compare("REGTEST") == 0) {
        return QAPP_APP_NAME_REGTEST;
    }
    return QAPP_APP_NAME_DEFAULT;
}

void setupChainQSettings(QGuiApplication* app, const QString& chain)
{
    app->setApplicationName(ChainQSettingsName(chain));
}

void LoadFontResource(const QString& path)
{
    if (QFontDatabase::addApplicationFont(path) < 0) {
        qWarning() << "Failed to load font resource:" << path;
    }
}

#ifdef ENABLE_TEST_AUTOMATION
void ApplyTestSettingsDir()
{
    const std::string settings_dir{gArgs.GetArg("-test-settings-dir", "")};
    if (settings_dir.empty()) {
        return;
    }

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QString::fromStdString(settings_dir));
}
#endif

void RegisterQmlTypes(AppMode& app_mode, BuildInfo& build_info, Clipboard& clipboard, UrlOpener& url_opener, BitcoinUriModel& bitcoin_uri_model)
{
    static bool registered{false};
    static AppMode* app_mode_instance{nullptr};
    static BuildInfo* build_info_instance{nullptr};
    static Clipboard* clipboard_instance{nullptr};
    static UrlOpener* url_opener_instance{nullptr};
    static BitcoinUriModel* bitcoin_uri_model_instance{nullptr};
    if (registered) return;
    app_mode_instance = &app_mode;
    build_info_instance = &build_info;
    clipboard_instance = &clipboard;
    url_opener_instance = &url_opener;
    bitcoin_uri_model_instance = &bitcoin_uri_model;

    qmlRegisterSingletonType<ApplicationModels>("org.bitcoincore.qt", 1, 0, "ApplicationModels",
        [](QQmlEngine* qml_engine, QJSEngine*) -> QObject* { return new ApplicationModels(qml_engine); });
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
    qmlRegisterSingletonType<UrlOpener>("org.bitcoincore.qt", 1, 0, "UrlOpener", [](QQmlEngine*, QJSEngine*) -> QObject* {
        QQmlEngine::setObjectOwnership(url_opener_instance, QQmlEngine::CppOwnership);
        return url_opener_instance;
    });
    qmlRegisterSingletonType<BitcoinUriModel>("org.bitcoincore.qt", 1, 0, "BitcoinUri", [](QQmlEngine*, QJSEngine*) -> QObject* {
        QQmlEngine::setObjectOwnership(bitcoin_uri_model_instance, QQmlEngine::CppOwnership);
        return bitcoin_uri_model_instance;
    });
    qmlRegisterUncreatableType<ImageExportOperation>("org.bitcoincore.qt", 1, 0, "ImageExportOperation", "Owned by an image export request");
    qmlRegisterUncreatableType<BackupFileOperation>("org.bitcoincore.qt", 1, 0, "BackupFileOperation", "Owned by a backup request");
    qmlRegisterType<QmlFileDialog>("org.bitcoincore.qt", 1, 0, "AppFileDialog");
    qmlRegisterType<BlockClockDial>("org.bitcoincore.qt", 1, 0, "BlockClockDial");
    qmlRegisterType<AppPreferences>("org.bitcoincore.qt", 1, 0, "AppPreferences");
    qmlRegisterUncreatableType<UiPreferencesStore>("org.bitcoincore.qt", 1, 0, "UiPreferencesStore", "Owned by the application engine");
    qmlRegisterType<LineGraph>("org.bitcoincore.qt", 1, 0, "LineGraph");
    qmlRegisterUncreatableType<PeerDetailsModel>("org.bitcoincore.qt", 1, 0, "PeerDetailsModel", "");
    qmlRegisterUncreatableType<NodeModel>("org.bitcoincore.qt", 1, 0, "NodeModel", "Provided by ApplicationModels");
    qmlRegisterAnonymousType<NativeListModel>("org.bitcoincore.qt", 1);
    qmlRegisterUncreatableType<CoreSettingsModel>("org.bitcoincore.qt",1,0,"CoreSettingsModel","Owned by settings");
    qmlRegisterUncreatableType<CoreProxyDraft>("org.bitcoincore.qt",1,0,"CoreProxyDraft","Owned by a settings form");
    qmlRegisterUncreatableType<OptionsTransactionLinks>("org.bitcoincore.qt", 1, 0, "OptionsTransactionLinks", "Owned by options");
    qmlRegisterUncreatableType<FlowDetailsModel>("org.bitcoincore.qt", 1, 0, "FlowDetailsModel", "Owned by a transaction review");
    qmlRegisterUncreatableType<DebugLogModel>("org.bitcoincore.qt", 1, 0, "DebugLogModel", "");
    qmlRegisterUncreatableType<RpcConsoleModel>("org.bitcoincore.qt", 1, 0, "RpcConsoleModel", "");
    qmlRegisterType<MonospaceOutputModel>("org.bitcoincore.qt", 1, 0, "MonospaceOutputModel");
    qmlRegisterType<BitcoinAmount>("org.bitcoincore.qt", 1, 0, "BitcoinAmount");
    qmlRegisterType<BitcoinAddress>("org.bitcoincore.qt", 1, 0, "BitcoinAddress");
    qmlRegisterUncreatableType<BitcoinUriParseOperation>("org.bitcoincore.qt", 1, 0, "BitcoinUriParseOperation", "Owned by a payment URI request");
#ifdef ENABLE_WALLET
    qmlRegisterType<ActivityFilterProxyModel>("org.bitcoincore.qt", 1, 0, "ActivityFilterProxyModel");
    qmlRegisterUncreatableType<TransactionActivityModel>("org.bitcoincore.qt", 1, 0, "TransactionActivityModel", "Owned by WalletQmlModel");
    qmlRegisterUncreatableType<ActivityDetailModel>("org.bitcoincore.qt", 1, 0, "ActivityDetailModel", "Owned by an Activity view");
    qmlRegisterUncreatableType<AddressListModel>("org.bitcoincore.qt", 1, 0, "AddressListModel", "");
    qmlRegisterUncreatableType<WalletProvisioningCheck>("org.bitcoincore.qt", 1, 0, "WalletProvisioningCheck", "Owned by the wallet controller");
    qmlRegisterType<PaymentRequest>("org.bitcoincore.qt", 1, 0, "PaymentRequest");
    qmlRegisterUncreatableType<ReceiveRequestOperation>("org.bitcoincore.qt", 1, 0, "ReceiveRequestOperation", "Owned by a receive request command");
    qmlRegisterUncreatableType<Transaction>("org.bitcoincore.qt", 1, 0, "Transaction", "");
    qmlRegisterUncreatableType<SendRecipient>("org.bitcoincore.qt", 1, 0, "SendRecipient", "");

    qmlRegisterUncreatableType<BumpTransactionModel>("org.bitcoincore.qt", 1, 0, "BumpTransactionModel",
                                                      "BumpTransactionModel cannot be instantiated from QML");
    qmlRegisterUncreatableType<WalletQmlModel>("org.bitcoincore.qt", 1, 0, "WalletQmlModel",
                                               "WalletQmlModel cannot be instantiated from QML");
    qmlRegisterUncreatableType<WalletQmlModelTransaction>("org.bitcoincore.qt", 1, 0, "WalletQmlModelTransaction",
                                                          "WalletQmlModelTransaction cannot be instantiated from QML");
    qmlRegisterUncreatableType<WalletListModel>("org.bitcoincore.qt", 1, 0, "WalletListModel",
                                                "WalletListModel cannot be instantiated from QML");
#else
    qmlRegisterTypeNotAvailable("org.bitcoincore.qt", 1, 0, "ActivityFilterProxyModel", "Wallet support is not compiled");
    qmlRegisterTypeNotAvailable("org.bitcoincore.qt", 1, 0, "TransactionActivityModel", "Wallet support is not compiled");
    qmlRegisterTypeNotAvailable("org.bitcoincore.qt", 1, 0, "ActivityDetailModel", "Wallet support is not compiled");
    qmlRegisterTypeNotAvailable("org.bitcoincore.qt", 1, 0, "AddressListModel", "Wallet support is not compiled");
    qmlRegisterTypeNotAvailable("org.bitcoincore.qt", 1, 0, "PaymentRequest", "Wallet support is not compiled");
    qmlRegisterTypeNotAvailable("org.bitcoincore.qt", 1, 0, "ReceiveRequestOperation", "Wallet support is not compiled");
    qmlRegisterTypeNotAvailable("org.bitcoincore.qt", 1, 0, "Transaction", "Wallet support is not compiled");
    qmlRegisterTypeNotAvailable("org.bitcoincore.qt", 1, 0, "SendRecipient", "Wallet support is not compiled");
    qmlRegisterTypeNotAvailable("org.bitcoincore.qt", 1, 0, "BumpTransactionModel", "Wallet support is not compiled");
    qmlRegisterTypeNotAvailable("org.bitcoincore.qt", 1, 0, "WalletQmlModel", "Wallet support is not compiled");
    qmlRegisterTypeNotAvailable("org.bitcoincore.qt", 1, 0, "WalletQmlModelTransaction", "Wallet support is not compiled");
    qmlRegisterTypeNotAvailable("org.bitcoincore.qt", 1, 0, "WalletListModel", "Wallet support is not compiled");
#endif

    registered = true;
}

enum class PreInitOnboardingStatus {
    NOT_SHOWN,
    COMPLETED,
    CANCELED,
    FAILED,
};

struct PreInitOnboardingContext {
    std::unique_ptr<OnboardingOptionsModel> onboarding_options_model;
    QScopedPointer<const NetworkStyle> network_style;
    std::unique_ptr<QQmlApplicationEngine> engine;
#ifdef ENABLE_TEST_AUTOMATION
    std::unique_ptr<TestBridge> test_bridge;
#endif
    QPointer<QQuickWindow> window;
    QPointer<UiPreferencesStore> preferences;

    void finishPreferences()
    {
        if (!preferences) return;
        if (window) window->contentItem()->setEnabled(false);
        preferences->closeAdmission();
        QEventLoop phase;
        bool drained{false};
        preferences->whenDrained([&] { drained = true; phase.quit(); });
        while (!drained) phase.exec();
    }

    void close()
    {
#ifdef ENABLE_TEST_AUTOMATION
        test_bridge.reset();
#endif
        if (window) {
            window->close();
        }
        engine.reset();
        preferences.clear();
        network_style.reset();
        onboarding_options_model.reset();
    }
};

bool ShouldShowPreInitOnboarding(const std::vector<std::string>& argv, bool can_listen_ipc, const execution::SnapshotServices& services)
{
    const auto status = startup::Run(services, [argv, can_listen_ipc] { return QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, can_listen_ipc); });
    return !status.ok || status.should_show_onboarding;
}

PreInitOnboardingStatus RunPreInitOnboarding(PreInitOnboardingContext& context, const std::vector<std::string>& argv, bool can_listen_ipc, QmlQuitHandler& quit_handler,
    const execution::SnapshotServices& services,
    const std::function<void(QQmlApplicationEngine&)>& engine_created,
    const std::function<void(QQmlApplicationEngine&)>& window_created)
{
    const bool show_onboarding = ShouldShowPreInitOnboarding(argv, can_listen_ipc, services);
    if (quit_handler.isQuitRequested()) return PreInitOnboardingStatus::CANCELED;
    if (!show_onboarding) {
        startup::Run(services, [] { QmlDataDir::ApplyGuiDataDirSetting(gArgs); return true; });
        return quit_handler.isQuitRequested() ? PreInitOnboardingStatus::CANCELED : PreInitOnboardingStatus::NOT_SHOWN;
    }

    try {
        startup::Run(services, [] { SelectParams(gArgs.GetChainType()); return true; });
    } catch (const std::exception& e) {
        const std::string error{e.what()};
        startup::Run(services, [error] { return InitError(Untranslated(error)); });
        return PreInitOnboardingStatus::FAILED;
    }

    context.onboarding_options_model = std::make_unique<OnboardingOptionsModel>(argv, can_listen_ipc, nullptr, services);

    context.network_style.reset(NetworkStyle::instantiate(Params().GetChainType()));
    assert(!context.network_style.isNull());

    context.engine = std::make_unique<QQmlApplicationEngine>();
    engine_created(*context.engine);
    context.preferences = InstallUiPreferences(*context.engine, services, true);
    if (quit_handler.isQuitRequested()) return PreInitOnboardingStatus::CANCELED;
    context.engine->addImageProvider(QStringLiteral("images"), new ImageProvider{context.network_style.data()});
    context.engine->rootContext()->setContextProperty("optionsModel", context.onboarding_options_model.get());
    context.engine->rootContext()->setContextProperty("onboardingOptionsModel", context.onboarding_options_model.get());
    context.engine->load(QUrl(QStringLiteral("qrc:///qml/pages/preinit.qml")));
    if (context.engine->rootObjects().isEmpty()) {
        return PreInitOnboardingStatus::FAILED;
    }

#ifdef ENABLE_TEST_AUTOMATION
    if (gArgs.IsArgSet("-test-automation")) {
        const QString socket_path = QString::fromStdString(gArgs.GetArg("-test-automation", ""));
        if (!socket_path.isEmpty()) {
            context.test_bridge = std::make_unique<TestBridge>(context.engine.get(), socket_path);
        }
    }
#endif

    context.window = qobject_cast<QQuickWindow*>(context.engine->rootObjects().first());
    if (!context.window) {
        return PreInitOnboardingStatus::FAILED;
    }

    QEventLoop loop;
    QObject::connect(context.engine->rootObjects().first(), SIGNAL(finished()), &loop, SLOT(quit()));
    QObject::connect(context.window, SIGNAL(closing(QQuickCloseEvent*)), &loop, SLOT(quit()));
    QObject::connect(&quit_handler, &QmlQuitHandler::quitRequested, &loop, &QEventLoop::quit);
    if (window_created) window_created(*context.engine);
    if (!quit_handler.isQuitRequested()) loop.exec();

    const bool completed = context.engine->rootObjects().first()->property("completed").toBool();
    if (!completed || quit_handler.isQuitRequested()) {
        context.close();
        return PreInitOnboardingStatus::CANCELED;
    }

    if (context.window) context.window->contentItem()->setEnabled(false);
    return PreInitOnboardingStatus::COMPLETED;
}
} // namespace


int QmlGuiMain(int argc, char* argv[])
{
    BitcoinQmlApplication app;
    return app.runFromCommandLine(argc, argv);
}

int BitcoinQmlApplication::runFromCommandLine(int argc, char* argv[])
{
#ifdef WIN32
    common::WinCmdLineArgs winArgs;
    std::tie(argc, argv) = winArgs.get();
#endif // WIN32

    return run(argc, argv);
}

namespace {
// Core factories and destruction both run on Native. This owner deliberately
// holds no executor lease: actor drain must finish before interface retirement.
struct NativeApplicationInterfaces {
    std::unique_ptr<interfaces::Init> init;
    std::unique_ptr<interfaces::Node> node;
    std::unique_ptr<interfaces::Chain> chain;
    bool can_listen_ipc{false};
    void retire() { chain.reset(); node.reset(); init.reset(); }
};
}

int BitcoinQmlApplication::run(int argc, char* argv[])
{
    // The process sink outlives QApplication, including destructor diagnostics.
    // Its only synchronous join occurs after GUI lifetime has ended.
    qtlog::NativeSink qt_log_sink{WriteQtLog};
    const auto diagnostic_loss = std::make_shared<std::atomic_bool>(false);
    g_qt_log_queue.store(qt_log_sink.queue());
    g_qt_fatal_policy.store(qtlog::CaptureFatalPolicy());
    const auto previous_log_handler = qInstallMessageHandler(StartupDebugMessageHandler);
    bool logging_finished{false};
    const auto finish_qt_logging = [&] {
        if (std::exchange(logging_finished, true)) return;
        qInstallMessageHandler(previous_log_handler);
        g_qt_log_queue.store({});
        g_qt_fatal_policy.store({});
        qt_log_sink.finishAfterGui();
    };
    const auto finish_logging_on_exception = qScopeGuard(finish_qt_logging);
    const auto run_gui = [&]() -> int {
        noui_connect();
        Q_INIT_RESOURCE(bitcoin_qml);
        Q_INIT_RESOURCE(bitcoin_compat);
        qRegisterMetaType<interfaces::BlockAndHeaderTipInfo>("interfaces::BlockAndHeaderTipInfo");
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
        // Qt gets a synthetic argument list so it never parses the process command line:
        // a bitcoin: URI from a desktop handler can smuggle options that Qt would consume
        // before Bitcoin Core rejects them. See https://achow101.com/2021/02/0.18-uri-vuln.
        int qt_argc = 1;
        char qt_application_name[]{"bitcoin-core-app"};
        char* qt_argv[]{qt_application_name, nullptr};
        QApplication app(qt_argc, qt_argv);
        QmlQuitHandler quit_handler;
        app.setQuitOnLastWindowClosed(false);
        const auto native_interfaces = std::make_shared<NativeApplicationInterfaces>();
        const auto drain_workers_before_application_destruction = qScopeGuard([native_interfaces] {
            startup::DrainThenRetire([native_interfaces] { native_interfaces->retire(); });
        });
        QQuickStyle::setStyle(QStringLiteral("Basic"));

        BackendExecutor startup_executor{execution::Limits::Bounded()};
        const auto amount_admission = std::make_shared<std::atomic_bool>(false);
        BitcoinAmount::setDefaultAdmissionGate(amount_admission);
        const auto reset_amount_admission = qScopeGuard([amount_admission] {
            *amount_admission = true;
            BitcoinAmount::setDefaultAdmissionGate({});
        });
        startup_executor.setObjectName(QStringLiteral("startup"));
        const execution::SnapshotServices startup_services{startup_executor.scheduler(), presentation::QueuedPost()};
        BlockClockModel::setDefaultServices(startup_services);
        const auto reset_block_clock_services = qScopeGuard([] { BlockClockModel::setDefaultServices({}); });
        auto startup_warnings = startupdiagnostics::Create(startup_services.scheduler);
        if (!startup_warnings) { qCritical("Startup diagnostic retention capacity is unavailable"); return EXIT_FAILURE; }
        bool startup_canceled{false};
        btcsignals::scoped_connection gui_startup_message_handler{::uiInterface.ThreadSafeMessageBox.connect(
            [&](const bilingual_str& message, unsigned int style) {
                if (style & CClientUIInterface::ICON_WARNING) {
                    RecordStartupWarning(startup_warnings, message, diagnostic_loss); return false;
                }
                const bool gui = QThread::currentThread() == app.thread();
                auto full_message = startupdiagnostics::Create(startup_services.scheduler);
                const auto& translated = message.translated;
                const QString prepared = gui ? QString::fromUtf8(translated.data(), std::min<size_t>(translated.size(), 16384))
                                             : QString::fromStdString(translated);
                if (full_message && !full_message->append(prepared, false)) full_message.reset();
                if (!full_message) *diagnostic_loss = true;
                else full_message->seal();
                if (gui && translated.size() > 16384) *diagnostic_loss = true;
                if (!gui) qCritical().noquote() << QString::fromStdString(message.original);
                else qCritical().noquote() << QString::fromUtf8(message.original.data(), std::min<size_t>(message.original.size(), 16384));
                const QString summary = prepared.left(16384) + (gui && translated.size() > 16384
                    ? QStringLiteral(" ") + QObject::tr("[complete error rejected: Core startup work ran on GUI]") : QString{}) + (!full_message
                    ? QStringLiteral(" ") + QObject::tr("[complete error unavailable: diagnostic capacity exhausted]") : QString{});
                bool result{false};
                const auto handle_message = [&] {
                    if (!startup_canceled && !quit_handler.isQuitRequested()) {
                        result = InitErrorMessageBox(summary, startup_services, full_message, quit_handler,
                            [this](QQmlApplicationEngine& engine) { qmlEngineCreated(engine); }, nullptr, diagnostic_loss);
                    }
                };
                if (gui) handle_message();
                else QMetaObject::invokeMethod(&app, handle_message, Qt::BlockingQueuedConnection);
                return result;
            })};

        SetupEnvironment();
        util::ThreadSetInternalName("main");

        // must be set before parsing command-line options; otherwise,
        // if invalid parameters were passed, QSetting initialization would fail
        // and the error will be displayed on terminal.
        // must be set before OptionsModel is initialized or translations are loaded,
        // as it is used to locate QSettings
        app.setOrganizationName(QAPP_ORG_NAME);
        app.setOrganizationDomain(QAPP_ORG_DOMAIN);
        app.setApplicationName(QAPP_APP_NAME_DEFAULT);

        startup::Run(startup_services, [native_interfaces, argc, argv] {
            native_interfaces->init = interfaces::MakeGuiInit(argc, argv);
            native_interfaces->can_listen_ipc = native_interfaces->init->canListenIpc();
            return true;
        });
        auto& init = native_interfaces->init;
        const bool can_listen_ipc = native_interfaces->can_listen_ipc;

        // Manages translators swapped on runtime language changes.
        // app_translator: bitcoin-qt strings (shared C++ layer)
        // qml_translator: QML-app-specific strings
        std::unique_ptr<QTranslator> app_translator;
        std::unique_ptr<QTranslator> qml_translator;
        const auto reset_translator = [](std::unique_ptr<QTranslator>& t) {
            if (t) { QCoreApplication::removeTranslator(t.get()); t.reset(); }
        };
        const auto install_language = [&](const QString& lang) {
            reset_translator(app_translator);
            reset_translator(qml_translator);
            if (!lang.isEmpty()) {
                auto t = std::make_unique<QTranslator>();
                if (t->load(QStringLiteral(":/translations/bitcoin_%1.qm").arg(lang)))
                    { QCoreApplication::installTranslator(t.get()); app_translator = std::move(t); }

                auto tq = std::make_unique<QTranslator>();
                if (tq->load(QStringLiteral(":/translations/bitcoin_qml_%1.qm").arg(lang)))
                    { QCoreApplication::installTranslator(tq.get()); qml_translator = std::move(tq); }
            }
        };

        // Parse command-line options. We do this after qt in order to show an error if there are problems parsing these.
        const auto parse_result = startup::Run(startup_services, [&, can_listen_ipc] {
            SetupServerArgs(gArgs, can_listen_ipc);
            SetupQmlGuiArgs(gArgs);
            std::string error;
            const bool parsed = gArgs.ParseParameters(argc, argv, error);
            return std::pair{parsed, QString::fromStdString(error)};
        });
        if (!parse_result.first) {
            startup::Run(startup_services, [error = parse_result.second] {
                return InitError(Untranslated(strprintf("Cannot parse command line arguments: %s\n", error.toStdString())));
            });
            return EXIT_FAILURE;
        }
#ifdef ENABLE_TEST_AUTOMATION
        ApplyTestSettingsDir();
#endif

        setupChainQSettings(&app, QString::fromStdString(gArgs.GetChainTypeString()).toUpper());
        if (gArgs.GetBoolArg("-resetguisettings", false)) {
            QString reset_error;
            reset_error = startup::Run(startup_services, [] {
                QString error;
                if (!QmlDataDir::ResetGuiSettings(gArgs, &error) && error.isEmpty()) error = QObject::tr("Unable to reset settings.");
                return error;
            });
            if (quit_handler.isQuitRequested()) return EXIT_SUCCESS;
            if (!reset_error.isEmpty()) {
                startup::Run(startup_services, [reset_error] { return InitError(Untranslated(reset_error.toStdString())); });
                return EXIT_FAILURE;
            }
        }

        LoadFontResource(":/fonts/bitcoincoresans/regular");
        LoadFontResource(":/fonts/bitcoincoresans/semibold");
        LoadFontResource(":/fonts/robotomono/regular");

        AppMode app_mode = SetupAppMode();
        BuildInfo build_info;
        Clipboard clipboard;
        UrlOpener url_opener;
        BitcoinUriModel bitcoin_uri_model;
        RegisterQmlTypes(app_mode, build_info, clipboard, url_opener, bitcoin_uri_model);

        const QString cli_lang = QString::fromStdString(gArgs.GetArg("-lang", ""));
        const QString startup_language = cli_lang.isEmpty()
            ? startup::Run(startup_services, [] { return QSettings().value(SettingsKeys::LANGUAGE, QmlLegacySettings::ReadLegacyGuiLanguage(QString::fromStdString(gArgs.GetChainTypeString()))).toString(); })
            : cli_lang;
        if (quit_handler.isQuitRequested()) return EXIT_SUCCESS;
        install_language(startup_language);

        std::vector<std::string> command_line_args;
        command_line_args.reserve(argc);
        for (int i = 0; i < argc; ++i) {
            command_line_args.emplace_back(argv[i]);
        }

        PreInitOnboardingContext pre_init_onboarding_context;
        const PreInitOnboardingStatus pre_init_onboarding_status{
            RunPreInitOnboarding(pre_init_onboarding_context, command_line_args, can_listen_ipc, quit_handler, startup_services,
                                [this](QQmlApplicationEngine& engine) { qmlEngineCreated(engine); },
                                [this](QQmlApplicationEngine& engine) { onboardingCreated(engine); })
        };
        switch (pre_init_onboarding_status) {
        case PreInitOnboardingStatus::COMPLETED:
            break;
        case PreInitOnboardingStatus::CANCELED:
            return EXIT_SUCCESS;
        case PreInitOnboardingStatus::FAILED:
            return EXIT_FAILURE;
        case PreInitOnboardingStatus::NOT_SHOWN:
            break;
        }

        auto& node = native_interfaces->node;
        auto& chain = native_interfaces->chain;
        const auto qt_info = QmlUtil::CaptureQtInfo();
        const auto prepare_node = [&, qt_info] {
            if (common::InitConfig(gArgs, [](const bilingual_str& message, const std::vector<std::string>& details) {
                    return InitError(message, details);
                })) {
                return false;
            }
            const auto legacy_migration = QmlLegacySettings::MigrateCoreSettings(gArgs, QmlLegacySettings::MigrationMode::Persist);
            if (!legacy_migration.error.isEmpty()) {
                return InitError(Untranslated(legacy_migration.error.toStdString()));
            }
            if (legacy_migration.settings_changed) {
                std::vector<std::string> settings_errors;
                if (!gArgs.WriteSettingsFile(&settings_errors)) {
                    return InitError(_("Settings file could not be written"), settings_errors);
                }
            }
            // GUI programs should not print to the console unnecessarily.
            gArgs.SoftSetBoolArg("-printtoconsole", false);
            InitLogging(gArgs);
            InitParameterInteraction(gArgs);
            QmlUtil::LogQtInfo(qt_info);
            node = init->makeNode();
            chain = init->makeChain();
            if (!node->baseInitialize()) return false;

            // Use Qt's effective settings identity with the resolved network, before
            // any window-behavior model reads it. This also works on the onboarding worker.
            if (gArgs.IsArgSet("-resetguisettings")) {
                const QSettings defaults;
                QSettings settings{defaults.format(), defaults.scope(), defaults.organizationName(),
                                   ChainQSettingsName(QString::fromStdString(gArgs.GetChainTypeString()).toUpper())};
                settings.remove(QStringLiteral("fHideTrayIcon"));
                settings.remove(QStringLiteral("fMinimizeToTray"));
                settings.remove(QStringLiteral("fMinimizeOnClose"));
                settings.sync();
                if (settings.status() != QSettings::NoError) InitWarning(Untranslated(QObject::tr("Unable to save window settings after reset.").toStdString()));
            }
            return true;
        };

        bool prepared{false};
        if (pre_init_onboarding_status == PreInitOnboardingStatus::COMPLETED) {
            // The onboarding window stays visible until MainWindow is ready.
            // Its model owns the settings write, node preparation and worker cleanup.
            auto& context = pre_init_onboarding_context;
            QEventLoop preparation_loop;
            bool preparation_finished{false};
            QObject::connect(context.window, &QWindow::visibleChanged, &preparation_loop,
                             [&](bool visible) { if (!visible) startup_canceled = true; });
            QObject::connect(context.onboarding_options_model.get(), &OnboardingOptionsModel::nodePrepared,
                             &preparation_loop, [&](bool success, const QString& error) {
                if (!error.isEmpty()) startup::Run(startup_services, [error] { return InitError(Untranslated(error.toStdString())); });
                prepared = success;
                preparation_finished = true;
                preparation_loop.quit();
            });
            context.onboarding_options_model->prepareNode(gArgs, prepare_node);
            while (!preparation_finished) preparation_loop.exec();
            if (startup_canceled || quit_handler.isQuitRequested()) return EXIT_SUCCESS;
        } else {
            prepared = startup::Run(startup_services, prepare_node);
        }
        if (!prepared) return EXIT_FAILURE;
        gui_startup_message_handler.disconnect();
        startup::Run(startup_services, [startup_warnings] { startup_warnings->seal(); return true; });
        startup_executor.shutdown();

        const bool wallet_enabled = WalletEnabledFromArgs();
        app_mode.setWalletEnabled(wallet_enabled);

        constexpr bool backend_ready{false};
        NodeModel::Services node_services;
        node_services.diagnostic_loss = diagnostic_loss;
        NodeModel node_model{*node, backend_ready, std::move(node_services)};
        const auto presentation_post = presentation::QueuedPost();
        BlockClockModel::setDefaultServices({node_model.queryScheduler(), presentation_post});
        BitcoinAddress::setDefaultServices({node_model.queryScheduler(), presentation_post});
        const auto reset_address_services = qScopeGuard([] { BitcoinAddress::setDefaultServices({}); });
        bitcoin_uri_model.setServices({node_model.queryScheduler(), presentation_post});
        node_model.addStartupWarnings(std::move(startup_warnings));
        auto block_clock_history{std::make_shared<BlockClockHistory>()};
        QmlInitExecutor init_executor{*node, [history = block_clock_history, node = node.get(), chain = chain.get()] {
            return SubscribeBlockClockHistory(*node, *chain, history);
        }, diagnostic_loss};
        QmlShutdownCoordinator shutdown_coordinator{init_executor};
        QPointer<QQuickWindow> main_window;
        bool shutdown_requested{false};
        bool shutdown_finished{false};
        QObject::connect(&shutdown_coordinator, &QmlShutdownCoordinator::finished, &app, [&] { shutdown_finished = true; });
        // Read the file the node actually writes, so -debuglogfile=<file> and
        // -nodebuglogfile are honoured rather than assuming <datadir>/debug.log.
        DebugLogModel debug_log_model{LogInstance().m_file_path, LogInstance().m_print_to_file,
            {node_model.queryScheduler(), presentation_post}};
#ifdef ENABLE_WALLET
        std::unique_ptr<WalletQmlController> wallet_controller;
        if (wallet_enabled) {
            wallet_controller = std::make_unique<WalletQmlController>(*node, walletopening::ChainPreferences(*chain),
                walletopening::FeePolicy{[chain = chain.get()] { return chain->relayMinFee().GetFeePerK(); }});
            QObject::connect(
                &init_executor, &QmlInitExecutor::initializeResult, wallet_controller.get(),
                [wallet_controller = wallet_controller.get(), &shutdown_requested](bool success, interfaces::BlockAndHeaderTipInfo, bool, bool backend_shutdown_requested) {
                    if (success && !shutdown_requested && !backend_shutdown_requested) {
                        wallet_controller->initialize();
                    }
                });
        }
#endif
        QObject::connect(&node_model, &NodeModel::requestedInitialize, &init_executor, &QmlInitExecutor::initialize);
        QObject::connect(&node_model, &NodeModel::requestedShutdown,
                         &shutdown_coordinator, &QmlShutdownCoordinator::requestShutdown);
        shutdown_coordinator.addAdmissionFence([&] { node_model.closeAdmission(); });
        shutdown_coordinator.addAdmissionFence([amount_admission] { *amount_admission = true; });
        shutdown_coordinator.addAdmissionFence([&] { bitcoin_uri_model.closeAdmission(); });
        shutdown_coordinator.addAdmissionFence([] { BitcoinAddress::setDefaultServices({}); });
        MonospaceOutputModel::setDefaultServices({node_model.queryScheduler(),
            presentation_post, [&node_model](std::function<void()> work) { return node_model.retireValues(std::move(work)); }, {}});
        const auto reset_output_services = qScopeGuard([] { MonospaceOutputModel::setDefaultServices({}); });
        shutdown_coordinator.addAdmissionFence([] { MonospaceOutputModel::closeAll(); });
        shutdown_coordinator.addAdmissionFence([] { BlockClockDial::closeAll(); });
        shutdown_coordinator.addBeforeInterruptTask([&](std::function<void()> done) {
            BlockClockDial::whenRetired(&shutdown_coordinator, std::move(done));
        });
        shutdown_coordinator.addBeforeInterruptTask([&](std::function<void()> done) {
            MonospaceOutputModel::whenRetired(&shutdown_coordinator, [done = std::move(done)] {
                MonospaceOutputModel::setDefaultServices({});
                done();
            });
        });
        QObject::connect(&shutdown_coordinator, &QmlShutdownCoordinator::shutdownStarted, &node_model, [&] {
            shutdown_requested = true;
            for (auto* window : QGuiApplication::allWindows()) {
                if (auto* quick_window = qobject_cast<QQuickWindow*>(window)) quick_window->contentItem()->setEnabled(false);
            }
            node_model.requestShutdown();
        });
        shutdown_coordinator.addParticipant(&node_model, &NodeModel::workDrained, [&] { node_model.beginShutdown(); });
        shutdown_coordinator.addParticipant(&debug_log_model, &DebugLogModel::drained, [&] { debug_log_model.stop(); });
#ifdef ENABLE_WALLET
        if (wallet_controller) {
            shutdown_coordinator.addAdmissionFence([&] { wallet_controller->closeAdmission(); });
            shutdown_coordinator.addParticipant(wallet_controller.get(), &WalletQmlController::walletsDrained,
                                                [&] { wallet_controller->beginShutdown(); });
        }
#endif
        QObject::connect(&init_executor, &QmlInitExecutor::initializeResult, &node_model, &NodeModel::initializeResult);
        QObject::connect(&init_executor, &QmlInitExecutor::runawayException, &node_model, [&](const QString& message) {
            if (main_window) main_window->contentItem()->setEnabled(true);
            node_model.handleLifecycleException(message, init_executor.fatalDiagnosticScheduler());
            if (!main_window) {
                InitErrorMessageBox(QObject::tr("A fatal node error occurred."), {node_model.queryScheduler(), presentation_post}, {}, quit_handler,
                                    [this](QQmlApplicationEngine& engine) { qmlEngineCreated(engine); },
                                    static_cast<StartupDiagnosticsModel*>(node_model.startupDiagnostics()), diagnostic_loss);
                node_model.requestShutdown();
            }
        });

        NetworkTrafficTower network_traffic_tower{*node, 1000, {node_model.queryScheduler(), presentation_post}};
        shutdown_coordinator.addAdmissionFence([&] { network_traffic_tower.closeAdmission(); });
        QObject::connect(&node_model, &NodeModel::chainStateReady,
                         &network_traffic_tower, &NetworkTrafficTower::startSampling);
        shutdown_coordinator.addParticipant(&network_traffic_tower, &NetworkTrafficTower::drained,
                                            [&] { network_traffic_tower.beginShutdown(); });
        NetworkStatusModel network_status_model;
#ifdef __ANDROID__
        AndroidNotifier android_notifier{node_model};
#endif

        ChainModel chain_model{QString::fromStdString(gArgs.GetChainTypeString())};
        BlockClockModel block_clock_model{block_clock_history,
            execution::SnapshotServices{node_model.queryScheduler(), presentation_post}};
        shutdown_coordinator.addAdmissionFence([&] { block_clock_model.closeAdmission(); });
        setupChainQSettings(&app, chain_model.networkName());



        DesktopWindowBehaviorModel desktop_window_behavior_model{
#ifdef Q_OS_ANDROID
            false,
#else
            true,
#endif
            {node_model.queryScheduler(), presentation_post}};
        shutdown_coordinator.addAdmissionFence([&] { desktop_window_behavior_model.closeAdmission(); });
        DesktopTrayIconController desktop_tray_icon_controller;

        qGuiApp->setQuitOnLastWindowClosed(false);
        QObject::connect(qGuiApp, &QGuiApplication::lastWindowClosed, &desktop_tray_icon_controller, [&] {
            // When the tray icon is visible the node keeps running in the background.
            if (desktop_tray_icon_controller.visible()) return;
            node_model.requestShutdown();
        });

        PeerListModel peer_model{*node, nullptr, backend_ready, {node_model.queryScheduler(), {}}};
        shutdown_coordinator.addAdmissionFence([&] { peer_model.closeAdmission(); });
        QObject::connect(&node_model, &NodeModel::chainStateReady,
                         &peer_model, &PeerListModel::backendInitialized);
        PeerListSortProxy peer_model_sort_proxy{nullptr};
        shutdown_coordinator.addAdmissionFence([&] { peer_model_sort_proxy.closeAdmission(); });
        peer_model_sort_proxy.setSourceModel(&peer_model);

        BanListModel ban_list_model{*node, nullptr, backend_ready, {node_model.queryScheduler(), {}}};
        shutdown_coordinator.addAdmissionFence([&] { ban_list_model.closeAdmission(); });
        QObject::connect(&node_model, &NodeModel::bannedListChanged,
                         &ban_list_model, &BanListModel::refresh);
        QObject::connect(&node_model, &NodeModel::chainStateReady,
                         &ban_list_model, &BanListModel::backendInitialized);

        QObject::connect(&shutdown_coordinator, &QmlShutdownCoordinator::shutdownStarted, &peer_model, [&] {
            peer_model.beginShutdown();
            ban_list_model.beginShutdown();
            block_clock_model.closeAdmission();
            QObject::disconnect(&node_model, nullptr, &ban_list_model, nullptr);
            QObject::disconnect(&node_model, nullptr, &block_clock_model, nullptr);
        });

        // Wallet and QR work share the existing bounded executor. Node-only
        // configurations retain one application service, with the same drain gate.
        std::unique_ptr<BackendExecutor> qr_executor;
        std::shared_ptr<execution::Scheduler> qr_scheduler;
#ifdef ENABLE_WALLET
        if (wallet_controller) qr_scheduler = wallet_controller->backgroundScheduler();
#endif
        if (!qr_scheduler) {
            qr_executor = std::make_unique<BackendExecutor>(execution::Limits::Bounded());
            qr_scheduler = qr_executor->scheduler();
            shutdown_coordinator.addParticipant(qr_executor.get(), &BackendExecutor::drained,
                                                [&] { qr_executor->shutdown(); });
        }

        auto engine = std::make_unique<QQmlApplicationEngine>();
        qmlEngineCreated(*engine);

        // The pre-init engine can have written the same persisted keys. Finish
        // those original writes before reading the final chain profile.
        pre_init_onboarding_context.finishPreferences();
        auto* ui_preferences = InstallUiPreferences(*engine,
            {node_model.queryScheduler(), presentation_post}, true);
        shutdown_coordinator.addAdmissionFence([ui_preferences] { ui_preferences->closeAdmission(); });
        shutdown_coordinator.addBeforeInterruptTask([ui_preferences](std::function<void()> done) {
            ui_preferences->whenDrained(std::move(done));
        });

        QScopedPointer<const NetworkStyle> network_style{NetworkStyle::instantiate(Params().GetChainType())};
        assert(!network_style.isNull());
        engine->addImageProvider(QStringLiteral("images"), new ImageProvider{network_style.data()});
        engine->addImageProvider(QStringLiteral("qr"), new QRImageProvider{std::move(qr_scheduler), QRImageProvider::RenderImage, engine.get()});

        engine->rootContext()->setContextProperty("networkTrafficTower", &network_traffic_tower);
        engine->rootContext()->setContextProperty("networkStatusModel", &network_status_model);
        engine->rootContext()->setContextProperty("nodeModel", &node_model);
        engine->rootContext()->setContextProperty("chainModel", &chain_model);
        engine->rootContext()->setContextProperty("blockClockModel", &block_clock_model);
        engine->rootContext()->setContextProperty("peerTableModel", &peer_model);
        engine->rootContext()->setContextProperty("peerListModelProxy", &peer_model_sort_proxy);
        engine->rootContext()->setContextProperty("banListModel", &ban_list_model);
        engine->rootContext()->setContextProperty("debugLogModel", &debug_log_model);

        RpcConsoleModel rpc_console_model{*node, {node_model.queryScheduler(), presentation_post}};
        shutdown_coordinator.addParticipant(&rpc_console_model, &RpcConsoleModel::drained,
                                            [&] { rpc_console_model.beginShutdown(); });
        shutdown_coordinator.addAdmissionFence([&] { rpc_console_model.closeAdmission(); });
        QObject::connect(&node_model, &NodeModel::nodeInitialized,
                         &rpc_console_model, &RpcConsoleModel::onNodeInitialized);
        engine->rootContext()->setContextProperty("rpcConsoleModel", &rpc_console_model);

#ifdef ENABLE_WALLET
        std::unique_ptr<WalletListModel> wallet_list_model;
        if (wallet_enabled) {
            wallet_list_model = std::make_unique<WalletListModel>(*node,
                execution::SnapshotServices{wallet_controller->backgroundScheduler(), presentation_post});
            shutdown_coordinator.addAdmissionFence([&] { wallet_list_model->closeAdmission(); });
            QObject::connect(wallet_controller.get(), &WalletQmlController::walletLoadStateChanged,
                             wallet_list_model.get(), &WalletListModel::setWalletLoadState);
            QObject::connect(wallet_controller.get(), &WalletQmlController::walletInfoChanged,
                             wallet_list_model.get(), &WalletListModel::setWalletInfo);
            QObject::connect(wallet_controller.get(), &WalletQmlController::walletDisplayNamesChanged,
                             wallet_list_model.get(), &WalletListModel::refreshDisplayNames);
            QObject::connect(wallet_list_model.get(), &WalletListModel::walletListChanged,
                             wallet_controller.get(), [controller = wallet_controller.get()](bool has_wallets) {
                                 controller->setNoWalletsFound(!has_wallets);
                             });
            // listWalletDir() rebuilds m_items from scratch — any per-row info
            // pushed earlier (e.g. at controller initialize() time, before the
            // picker was ever opened) has nowhere to land. Re-publish open wallets'
            // info every time the model resets so newly created rows pick it up.
            QObject::connect(wallet_list_model.get(), &QAbstractItemModel::modelReset,
                             wallet_controller.get(), &WalletQmlController::publishOpenWalletsInfo);
            QObject::connect(wallet_controller.get(), &WalletQmlController::initializedChanged,
                             wallet_list_model.get(), [controller = wallet_controller.get(), list_model = wallet_list_model.get()]() {
                                 if (controller->initialized()) {
                                     list_model->listWalletDir();
                                 }
                             });
            engine->rootContext()->setContextProperty("walletController", wallet_controller.get());
            engine->rootContext()->setContextProperty("walletListModel", wallet_list_model.get());
        }
#endif

        OptionsQmlModel options_model(*node, gArgs, {node_model.queryScheduler(), presentation_post});
        shutdown_coordinator.addAdmissionFence([&] { options_model.closeAdmission(); });
        shutdown_coordinator.addBeforeInterruptParticipant(&options_model, &OptionsQmlModel::shutdownFinished,
                                            [&] { options_model.beginShutdown(); });
#ifdef ENABLE_WALLET
        if (wallet_list_model) {
            wallet_list_model->setDisplayUnit(options_model.displayUnit());
            QObject::connect(&options_model, &OptionsQmlModel::displayUnitChanged,
                             wallet_list_model.get(), &WalletListModel::setDisplayUnit);
        }
#endif
        engine->rootContext()->setContextProperty("optionsModel", &options_model);
#ifdef ENABLE_TEST_AUTOMATION
        engine->rootContext()->setContextProperty("testAutomationEnabled", true);
#else
        engine->rootContext()->setContextProperty("testAutomationEnabled", false);
#endif
        // Keep the startup language until the settings snapshot is ready.
        if (options_model.settingsReady()) install_language(options_model.language());

        // Retranslate the QML UI immediately when the user picks a new language.
        QObject::connect(&options_model, &OptionsQmlModel::languageChanged, [&]() {
            install_language(options_model.language());
            engine->retranslate();
        });

        desktop_tray_icon_controller.setBasePixmap(
            network_style->getTrayAndWindowIcon().pixmap(QSize(256, 256)));
        desktop_tray_icon_controller.setToolTip(
            QString(QObject::tr("%1 client").arg(CLIENT_NAME) + " " + network_style->getTitleAddText()).trimmed());
        desktop_tray_icon_controller.setVisible(
            app_mode.isDesktop() && desktop_window_behavior_model.showTrayIcon());
        QObject::connect(&desktop_tray_icon_controller, &DesktopTrayIconController::supportedChanged,
            [&desktop_window_behavior_model](bool supported) {
                if (!supported) desktop_window_behavior_model.setShowTrayIcon(false);
        });
        engine->rootContext()->setContextProperty("desktopWindowBehaviorModel", &desktop_window_behavior_model);
        engine->rootContext()->setContextProperty("desktopTrayIconController", &desktop_tray_icon_controller);

        engine->setInitialProperties({
            {QStringLiteral("walletAvailableForUi"), wallet_enabled},
            {QStringLiteral("appModeDesktopForUi"), app_mode.mode() == AppMode::DESKTOP},
            {QStringLiteral("preInitOnboardingRanForUi"), pre_init_onboarding_status == PreInitOnboardingStatus::COMPLETED},
        });
        const auto shutdown_before_startup_return = [&] {
            pre_init_onboarding_context.close();
            QEventLoop loop;
            QObject::connect(&shutdown_coordinator, &QmlShutdownCoordinator::finished, &loop, &QEventLoop::quit);
            node_model.requestShutdown();
            while (!shutdown_finished) loop.exec();
        };
        if (quit_handler.isQuitRequested()) {
            shutdown_before_startup_return();
            return EXIT_SUCCESS;
        }
        engine->load(QUrl(QStringLiteral("qrc:///qml/pages/MainWindow.qml")));
        if (engine->rootObjects().isEmpty()) {
            shutdown_before_startup_return();
            return EXIT_FAILURE;
        }

        main_window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
        if (!main_window) {
            shutdown_before_startup_return();
            return EXIT_FAILURE;
        }
        desktop_tray_icon_controller.setMainWindow(main_window);
        if (pre_init_onboarding_context.window) {
            main_window->setGeometry(pre_init_onboarding_context.window->geometry());
        }
        pre_init_onboarding_context.close();

        if (quit_handler.isQuitRequested() || shutdown_requested) {
            shutdown_before_startup_return();
            return EXIT_SUCCESS;
        }
        QObject::connect(&quit_handler, &QmlQuitHandler::quitRequested,
                         &node_model, &NodeModel::requestShutdown);
        const auto exit_main_event_loop = [] {
            QCoreApplication::exit(0);
        };
        QObject::connect(&shutdown_coordinator, &QmlShutdownCoordinator::finished,
                         qGuiApp, exit_main_event_loop, Qt::QueuedConnection);

#ifdef ENABLE_TEST_AUTOMATION
        std::unique_ptr<TestBridge> test_bridge;
        if (gArgs.IsArgSet("-test-automation")) {
            QString socket_path = QString::fromStdString(gArgs.GetArg("-test-automation", ""));
            if (socket_path.isEmpty()) {
                // Default to a socket in the data directory.
                socket_path = QString::fromStdString(
                    (gArgs.GetDataDirNet() / "test_bridge.sock").utf8string());
            }
            test_bridge = std::make_unique<TestBridge>(engine.get(), socket_path);
        }
#endif

        // Install qDebug() message handler to route to debug.log
        qInstallMessageHandler(DebugMessageHandler);

        qInfo() << "Graphics API in use:" << QmlUtil::GraphicsApi(main_window);

        node_model.startShutdownPolling();
        m_node = node.get();
        m_node_model = &node_model;
        m_engine = engine.get();
        const auto clear_runtime = qScopeGuard([this] {
            m_node = nullptr;
            m_node_model = nullptr;
            m_engine = nullptr;
        });
        windowCreated();
        const int exit_code{qGuiApp->exec()};
#ifdef ENABLE_TEST_AUTOMATION
        test_bridge.reset();
#endif
        engine.reset();
        return exit_code;
    };
    const auto exit_code = run_gui();
    // QApplication and every GUI owner have been destroyed. Flush and join the
    // process writer before deciding the actual externally visible exit status.
    finish_qt_logging();
    const auto receipt = qt_log_sink.queue()->receipt();
    return startupdiagnostics::FinalExitStatus(qtlog::FinalExitStatus(exit_code, receipt), diagnostic_loss->load());
}

interfaces::Node& BitcoinQmlApplication::node() const
{
    assert(m_node);
    return *m_node;
}

NodeModel& BitcoinQmlApplication::nodeModel() const
{
    assert(m_node_model);
    return *m_node_model;
}

QQmlApplicationEngine& BitcoinQmlApplication::engine() const
{
    assert(m_engine);
    return *m_engine;
}

QQuickWindow* BitcoinQmlApplication::window() const
{
    return qobject_cast<QQuickWindow*>(engine().rootObjects().constFirst());
}

void BitcoinQmlApplication::requestShutdown()
{
    nodeModel().requestShutdown();
}
