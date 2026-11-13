// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/onboardingoptionsmodel.h>

#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <qml/retainedsnapshot.h>
#include <qml/core_settings.h>
#include <qml/datadir.h>
#include <qml/guiconstants.h>
#include <qml/onboarding_settings.h>
#include <qml/onboarding_storage.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QUrl>
#include <QVariantMap>

#include <exception>
#include <stdexcept>
#include <utility>
#include <tuple>

namespace {
QString DataDirDisplay(QString path){
#ifdef __ANDROID__
    path.replace("content://com.android.externalstorage.documents/tree/primary%3A","/storage/self/primary/");
#endif
    return path;
}
struct InitialOnboarding {
    QmlOnboardingSettings::OnboardingStartupStatus status;
    QString default_path;
    QString active_display;
    QUrl default_url;
    bool is_default{false};
};
}
OnboardingOptionsModel::OnboardingOptionsModel(std::vector<std::string> argv,bool ipc,QObject* parent,execution::SnapshotServices services)
    :QObject(parent),m_services(std::move(services)),m_can_listen_ipc(ipc)
{
    if(!m_services.scheduler){m_executor=std::make_unique<BackendExecutor>(execution::Limits::Bounded());m_services.scheduler=m_executor->scheduler();}
    if(!m_services.post)m_services.post=presentation::QueuedPost();
    m_work=std::make_unique<execution::OwnedWork>(m_services);m_preparation_work=std::make_unique<execution::OwnedWork>(m_services);
    m_core_settings.setAdmissionReady(false);m_core_settings.configureServices(m_services);
    connect(&m_core_settings,&CoreSettingsModel::operationChanged,this,[this]{if(!m_stopping)Q_EMIT canFinishChanged();});
    m_argv=execution::RetainSnapshot<std::vector<std::string>>(m_services.scheduler,[&]{return std::make_unique<std::vector<std::string>>(std::move(argv));});
    m_core_settings.setAfterChangeHandler([this](const QmlCoreSettings::Change& change,CoreSettingsModel::ChangeOrigin origin){
        const QPointer<OnboardingOptionsModel> observer{this};QmlCoreSettings::EmitCoreSettingSignals(*this,change);if(!observer||m_stopping)return;
        if(origin==CoreSettingsModel::ChangeOrigin::User&&(QmlCoreSettings::PruneChanged(change)||QmlCoreSettings::PruneSizeGBChanged(change))){m_prune_auto_recommended=false;emitStorageStatusChanged();}
    });
    if(!m_argv){m_preview_error=tr("Startup settings capacity is unavailable.");m_preview_pending=false;return;}
    m_preview_in_flight=true;const auto request_id=++m_preview_request_id;
    const QPointer<OnboardingOptionsModel> observer{this};
    m_work->submit([argv=m_argv,ipc]{InitialOnboarding result;result.default_path=QmlDataDir::DefaultDataDirString();result.default_url=QUrl::fromLocalFile(result.default_path);
        result.status=QmlOnboardingSettings::ResolveOnboardingStartupStatus(*argv,ipc);result.active_display=DataDirDisplay(result.status.active_data_dir);result.is_default=QmlDataDir::IsDefaultDataDir(result.status.active_data_dir);return result;},
        [observer,request_id](const InitialOnboarding& result){
            if(!observer||observer->m_stopping)return;
            observer->m_preview_in_flight=false;
            observer->m_default_data_dir=result.default_path;observer->m_default_data_directory=result.default_url;
            Q_EMIT observer->dataDirDefaultsChanged();if(!observer||observer->m_stopping)return;
            if(request_id!=observer->m_preview_request_id){observer->startPreview();return;}
            if(!result.status.ok){observer->setPreviewError(result.status.error);if(observer&&!observer->m_stopping)observer->setPreviewPending(false);return;}
            observer->m_data_dir=result.status.active_data_dir;observer->m_data_dir_source=result.status.data_dir_source;
            observer->m_custom_datadir_string=result.is_default?QString{}:observer->m_data_dir;observer->m_custom_datadir_display=result.is_default?QString{}:result.active_display;
            Q_EMIT observer->dataDirChanged(observer->m_data_dir);if(!observer||observer->m_stopping)return;
            Q_EMIT observer->customDataDirStringChanged(observer->m_custom_datadir_string);if(observer&&!observer->m_stopping)observer->startPreview();
        },[observer](std::exception_ptr){if(!observer||observer->m_stopping)return;observer->m_preview_in_flight=false;
            observer->setPreviewError(tr("The startup settings could not be read."));if(observer&&!observer->m_stopping)observer->setPreviewPending(false);});
}
void OnboardingOptionsModel::closeAdmission()
{
    m_stopping=true;++m_preview_request_id;++m_storage_request_id;++m_storage_projection_revision;++m_path_revision;
    m_work->close();m_argv.reset();m_core_settings.closeAdmission();
}
void OnboardingOptionsModel::publishShutdown()
{
    if(m_shutdown_published)return;
    m_shutdown_published=true;Q_EMIT shutdownFinished();
}
void OnboardingOptionsModel::beginShutdown()
{
    if(std::exchange(m_shutdown_requested,true))return;
    closeAdmission();
    if(m_preparing||m_preparation_terminal_pending)return;
    const QPointer<OnboardingOptionsModel> observer{this};m_preparation_work->close();
    if(m_executor){m_executor->whenDrained(this,[observer]{if(observer)observer->publishShutdown();});m_executor->shutdown();}
    else m_work->close([observer]{if(observer)observer->publishShutdown();});
}

void OnboardingOptionsModel::setPreviewPending(bool pending)
{
    const QPointer<OnboardingOptionsModel> observer{this};
    if(m_stopping)return;
    if (m_preview_pending == pending) return;
    m_preview_pending = pending;
    Q_EMIT validationPendingChanged();
    if(!observer||m_stopping)return;
    Q_EMIT canFinishChanged();
    if(!observer||m_stopping)return;
}

OnboardingOptionsModel::~OnboardingOptionsModel(){closeAdmission();m_preparation_work->close();if(m_executor)m_executor->shutdown();}

void OnboardingOptionsModel::prepareNode(ArgsManager& args,std::function<bool()> prepare_node)
{
    if(m_preparation_started)return;
    if(m_stopping){const QPointer<OnboardingOptionsModel> observer{this};m_services.post([observer]{if(observer)Q_EMIT observer->nodePrepared(false,tr("The application is shutting down."));});return;}
    const auto request=applyRequest();m_preparation_started=true;m_preparing=true;
    const auto receipt=m_preparation_state;receipt->store(PreparationPending);
    closeAdmission();const QPointer<OnboardingOptionsModel> observer{this};
    const bool admitted=m_preparation_work->submit([&args,request,receipt,prepare_node=std::move(prepare_node)] {
        try {
        QString error;if(!QmlOnboardingSettings::ApplyToArgs(args,request.data_dir,request.touched_settings,request.values,&error)){
            if(error.isEmpty())error=tr("Onboarding settings could not be applied.");
            receipt->store(PreparationFailed);return std::pair{false,error};}
        const bool success=prepare_node();receipt->store(success?PreparationSucceeded:PreparationFailed);return std::pair{success,QString{}};
        } catch(...){receipt->store(PreparationUncertain);throw;}
    },[observer](const std::pair<bool,QString>& result){if(observer)observer->finishNodePreparation(result.first,result.second);},
    [observer,receipt](std::exception_ptr failure){if(receipt->load()==PreparationPending)receipt->store(PreparationFailed);if(!observer)return;QString error;
        try{std::rethrow_exception(failure);}catch(const std::exception& e){error=QString::fromUtf8(e.what());}catch(...){error=tr("Unknown exception preparing the node.");}
        observer->finishNodePreparation(false,error);
    });
    if(!admitted)receipt->store(PreparationFailed);
    if(observer)Q_EMIT preparationStateChanged();
    if(observer)Q_EMIT canFinishChanged();
}
void OnboardingOptionsModel::finishNodePreparation(bool success,const QString& error)
{
    if(m_preparation_terminal_pending||m_preparation_published)return;
    m_preparing=false;m_preparation_terminal_pending=true;
    const QPointer<OnboardingOptionsModel> observer{this};
    const auto publish=[observer,success,error]{
        if(!observer||observer->m_preparation_published)return;
        observer->m_preparation_published=true;
        Q_EMIT observer->preparationStateChanged();if(!observer)return;
        Q_EMIT observer->nodePrepared(success,error);if(!observer)return;observer->m_preparation_terminal_pending=false;
        if(observer->m_shutdown_requested)observer->publishShutdown();
    };
    m_preparation_work->close();
    if(m_executor){m_executor->whenDrained(this,publish);m_executor->shutdown();}
    else m_preparation_work->close(publish);
}
QString OnboardingOptionsModel::getDefaultDataDirString() const{return m_default_data_dir;}
QUrl OnboardingOptionsModel::getDefaultDataDirectory() const{return m_default_data_directory;}

QString OnboardingOptionsModel::getCustomDataDirString() const{return m_custom_datadir_display;}

QString OnboardingOptionsModel::validateCustomDataDir(const QString& path) const
{
    return path==m_path_source||path==m_data_dir?m_preview_error:tr("The directory has not been checked.");
}
bool OnboardingOptionsModel::selectCustomDataDir(const QString& path)
{
    if(m_stopping)return false;
    const QPointer<OnboardingOptionsModel> observer{this};
    m_core_settings.setAdmissionReady(false);if(!observer||m_stopping)return false;m_core_settings.invalidatePreviewContext();const auto revision=++m_path_revision;++m_preview_request_id;++m_storage_request_id;m_path_source=path;m_preview_pending=true;
    const bool admitted=m_work->submit([path]{if(path.size()>32768)throw std::length_error("Data directory path is too long");const auto normalized=QmlDataDir::NormalizeLocalPath(path);return std::pair{normalized,DataDirDisplay(normalized)};},
        [observer,revision](const std::pair<QString,QString>& result){if(!observer||observer->m_stopping||revision!=observer->m_path_revision)return;
            const auto& [normalized,display]=result;observer->m_custom_datadir_display=display;
            if(normalized.isEmpty()){observer->setPreviewError(tr("Choose a data directory."));if(observer&&!observer->m_stopping)observer->setPreviewPending(false);return;}
            observer->m_data_dir_source=QmlOnboardingSettings::DataDirSource::UserSelection;
            if(observer->m_custom_datadir_string!=normalized){observer->m_custom_datadir_string=normalized;Q_EMIT observer->customDataDirStringChanged(normalized);}
            if(observer&&!observer->m_stopping)observer->setDataDir(normalized);
        },[observer,revision](std::exception_ptr){if(!observer||observer->m_stopping||revision!=observer->m_path_revision)return;
            observer->setPreviewError(tr("The selected directory could not be checked."));if(observer&&!observer->m_stopping)observer->setPreviewPending(false);});
    if(observer&&!m_stopping){Q_EMIT validationPendingChanged();if(observer&&!m_stopping)Q_EMIT canFinishChanged();}
    return admitted;
}
void OnboardingOptionsModel::useDefaultDataDir()
{
    if(m_stopping)return;
    const QPointer<OnboardingOptionsModel> observer{this};
    m_core_settings.setAdmissionReady(false);if(!observer||m_stopping)return;m_core_settings.invalidatePreviewContext();const auto revision=++m_path_revision;++m_preview_request_id;++m_storage_request_id;m_preview_pending=true;
    m_work->submit([]{return QmlDataDir::DefaultDataDirString();},[observer,revision](const QString& path){
        if(!observer||observer->m_stopping||revision!=observer->m_path_revision)return;
        observer->m_path_source=path;observer->m_data_dir_source=QmlOnboardingSettings::DataDirSource::UserSelection;
        if(!observer->m_custom_datadir_string.isEmpty()){observer->m_custom_datadir_string.clear();observer->m_custom_datadir_display.clear();Q_EMIT observer->customDataDirStringChanged({});}
        if(observer&&!observer->m_stopping)observer->setDataDir(path);
    },[observer,revision](std::exception_ptr){if(!observer||observer->m_stopping||revision!=observer->m_path_revision)return;
        observer->setPreviewError(tr("The default directory could not be checked."));if(observer&&!observer->m_stopping)observer->setPreviewPending(false);});
    if(observer&&!m_stopping){Q_EMIT validationPendingChanged();if(observer&&!m_stopping)Q_EMIT canFinishChanged();}
}
void OnboardingOptionsModel::setDataDir(const QString& canonical)
{
    if(m_stopping)return;
    const QPointer<OnboardingOptionsModel> observer{this};
    if(canonical!=m_data_dir){m_data_dir=canonical;Q_EMIT dataDirChanged(m_data_dir);}
    if(observer&&!m_stopping)refreshPreview();
}

void OnboardingOptionsModel::setPrune(bool prune)
{
    m_core_settings.changePrune(prune);
}

void OnboardingOptionsModel::setPruneSizeGB(int prune_size_gb)
{
    m_core_settings.changePruneSizeGB(prune_size_gb);
}

void OnboardingOptionsModel::setListen(bool listen)
{
    m_core_settings.changeListen(listen);
}

void OnboardingOptionsModel::setNatpmp(bool natpmp)
{
    m_core_settings.changeNatpmp(natpmp);
}

void OnboardingOptionsModel::setServer(bool server)
{
    m_core_settings.changeServer(server);
}

void OnboardingOptionsModel::setProxyEnabled(bool enabled)
{
    m_core_settings.changeProxyEnabled(enabled);
}

void OnboardingOptionsModel::setProxyAddress(const QString& address)
{
    requestProxyLocation(address);
}

void OnboardingOptionsModel::setTorEnabled(bool enabled)
{
    m_core_settings.changeTorEnabled(enabled);
}

void OnboardingOptionsModel::setTorAddress(const QString& address)
{
    requestTorLocation(address);
}

quint64 OnboardingOptionsModel::requestProxyLocation(const QString& location)
{
    return m_core_settings.changeProxyLocation(location);
}

quint64 OnboardingOptionsModel::requestTorLocation(const QString& location)
{
    return m_core_settings.changeTorLocation(location);
}

QString OnboardingOptionsModel::defaultProxyAddress() const
{
    return m_core_settings.defaultProxyAddress();
}

QString OnboardingOptionsModel::storageStatus() const
{
    return storageInfo().status;
}

int OnboardingOptionsModel::storageAvailableGB() const
{
    return storageInfo().available_gb;
}

QString OnboardingOptionsModel::storageAvailableText() const
{
    return storageInfo().available_text;
}

QString OnboardingOptionsModel::storageWarningText() const
{
    return storageInfo().warning_text;
}

int OnboardingOptionsModel::storageMinimumRequiredGB() const
{
    return storageInfo().minimum_required_gb;
}

int OnboardingOptionsModel::fullStorageRequiredGB() const
{
    return storageInfo().full_required_gb;
}

int OnboardingOptionsModel::prunedStorageRequiredGB() const
{
    return storageInfo().pruned_required_gb;
}

int OnboardingOptionsModel::selectedStorageRequiredGB() const
{
    return storageInfo().selected_required_gb;
}

bool OnboardingOptionsModel::storageEnoughForSelected() const
{
    return storageInfo().enough_for_selected;
}

bool OnboardingOptionsModel::storageEnoughForFull() const
{
    return storageInfo().enough_for_full;
}

void OnboardingOptionsModel::requestStorageCheck()
{
    const QPointer<OnboardingOptionsModel> observer{this};
    if(m_stopping)return;
    ++m_storage_request_id;
    m_storage_check_path = m_data_dir;
    m_storage_check_pending = true;
    emitStorageStatusChanged();
    if(!observer||m_stopping)return;
    if (!m_storage_check_in_flight) {
        startStorageCheck(m_storage_request_id, m_storage_check_path);
    }
}

void OnboardingOptionsModel::queueStorageCheck()
{
    if (m_stopping) return;
    m_storage_check_in_flight = true; // Coalesce newer edits until the queued follow-up starts.
    const QPointer<OnboardingOptionsModel> observer{this};
    presentation::ReliablePost(m_services.post)([observer] {
        if (observer && !observer->m_stopping) observer->startStorageCheck(observer->m_storage_request_id, observer->m_storage_check_path);
    });
}

void OnboardingOptionsModel::startStorageCheck(uint64_t request_id, const QString& path)
{
    m_storage_check_in_flight = true;
    m_work->submit( [path] { return QmlDataDir::CheckStorageSpace(path); },
        [this, request_id](const QmlDataDir::StorageSpaceResult& result) { if(!m_stopping)applyStorageCheckResult(request_id, result); },
        [this, request_id](std::exception_ptr) {
            if(m_stopping)return;
            QmlDataDir::StorageSpaceResult result;
            result.message = tr("Available storage could not be checked.");
            applyStorageCheckResult(request_id, result);
        });
}

void OnboardingOptionsModel::applyStorageCheckResult(uint64_t request_id, const QmlDataDir::StorageSpaceResult& result)
{
    const QPointer<OnboardingOptionsModel> observer{this};
    if(m_stopping)return;
    m_storage_check_in_flight = false;
    if (request_id != m_storage_request_id) {
        if (!m_preview_pending && !m_stopping) queueStorageCheck();
        return;
    }

    m_storage_check_pending = false;
    m_storage_result_valid = result.ok;
    m_storage_available_bytes = result.ok ? result.available_bytes : 0;
    m_storage_path_message = result.message;
    m_storage_error_text = result.ok ? QString{} : result.message;
    emitStorageStatusChanged();
    if(!observer||m_stopping)return;
}

void OnboardingOptionsModel::emitStorageStatusChanged()
{
    if(m_stopping)return;
    ++m_storage_projection_revision;m_storage_projection_pending=true;
    const QPointer<OnboardingOptionsModel> observer{this};
    if(!m_storage_projection_inflight)startStorageProjection();
    if(!observer||m_stopping)return;
    Q_EMIT storageStatusChanged();if(observer&&!m_stopping)Q_EMIT canFinishChanged();
}
void OnboardingOptionsModel::queueStorageProjection()
{
    if (m_stopping) return;
    m_storage_projection_inflight = true; // Coalesce newer edits until the queued follow-up starts.
    const QPointer<OnboardingOptionsModel> observer{this};
    presentation::ReliablePost(m_services.post)([observer] {
        if (observer && !observer->m_stopping) observer->startStorageProjection();
    });
}

void OnboardingOptionsModel::startStorageProjection()
{
    if(m_stopping)return;
    m_storage_projection_inflight=true;const auto revision=m_storage_projection_revision;const auto core_revision=m_core_settings.publishedUserRevision();
    const auto state=storageState();const auto status=m_core_settings.statuses().value(QStringLiteral("prune")).toMap();
    const bool touched=m_core_settings.isTouched(QStringLiteral("prune"))&&!m_prune_auto_recommended;
    const QPointer<OnboardingOptionsModel> observer{this};
    m_work->submit([state=QmlOnboardingStorage::State{state},status,touched]() mutable {
        const bool eligible=state.result_valid&&state.error_text.isEmpty()&&!state.existing_profile&&!touched
            &&status.value(QStringLiteral("canEdit"),true).toBool()&&status.value(QStringLiteral("source")).toString()==QStringLiteral("default");
        const bool recommend=eligible&&QmlOnboardingStorage::ShouldRecommendPrune(state);
        if(eligible){state.prune=recommend;state.prune_size_gb=DEFAULT_PRUNE_TARGET_GB;}
        return std::tuple{QmlOnboardingStorage::Evaluate(state),eligible,recommend};
    },[observer,revision,core_revision](const auto& result){
        if(!observer||observer->m_stopping)return;
        observer->m_storage_projection_inflight=false;
        if(revision!=observer->m_storage_projection_revision){observer->queueStorageProjection();return;}
        const auto& [info,eligible,recommend]=result;observer->m_storage_info=info;observer->m_storage_projection_pending=false;
        if(eligible){observer->m_prune_auto_recommended=recommend;observer->m_core_settings.changePruneRecommendation(recommend,DEFAULT_PRUNE_TARGET_GB,recommend,core_revision);}
        if(!observer||observer->m_stopping)return;
        Q_EMIT observer->storageStatusChanged();if(observer&&!observer->m_stopping)Q_EMIT observer->canFinishChanged();
    },[observer,revision](std::exception_ptr){
        if(!observer||observer->m_stopping)return;
        observer->m_storage_projection_inflight=false;
        if(revision!=observer->m_storage_projection_revision){observer->queueStorageProjection();return;}
        observer->m_storage_projection_pending=false;observer->m_storage_error_text=tr("Storage requirements could not be calculated.");
        observer->m_storage_info.status=QStringLiteral("error");Q_EMIT observer->storageStatusChanged();if(observer&&!observer->m_stopping)Q_EMIT observer->canFinishChanged();
    },{execution::Lane::Domain,1024,{},execution::WorkClass::Observation});
}

QmlOnboardingStorage::State OnboardingOptionsModel::storageState() const
{
    QmlOnboardingStorage::State state;
    state.check_pending = m_storage_check_pending;
    state.result_valid = m_storage_result_valid;
    state.available_bytes = m_storage_available_bytes;
    state.error_text = m_storage_error_text;
    state.assumed_blockchain_size_gb = m_assumed_blockchain_size;
    state.assumed_chainstate_size_gb = m_assumed_chainstate_size;
    state.prune = m_core_settings.values().prune;
    state.prune_size_gb = m_core_settings.values().prune_size_gb;
    state.existing_profile = m_profile.existing_profile;
    return state;
}

QmlCoreSettings::Values OnboardingOptionsModel::coreValues() const
{
    return m_core_settings.values();
}

void OnboardingOptionsModel::setPreviewError(const QString& error)
{
    const QPointer<OnboardingOptionsModel> observer{this};
    if(m_stopping)return;
    if (error == m_preview_error) return;
    m_preview_error = error;
    Q_EMIT previewErrorChanged();
    if(!observer||m_stopping)return;
    Q_EMIT canFinishChanged();
    if(!observer||m_stopping)return;
}

void OnboardingOptionsModel::refreshPreview()
{
    const QPointer<OnboardingOptionsModel> observer{this};
    if(m_stopping)return;
    ++m_preview_request_id;
    ++m_storage_request_id;
    setPreviewPending(true);if(!observer||m_stopping)return;
    if (!m_preview_in_flight) startPreview();
}

void OnboardingOptionsModel::queuePreview()
{
    if (m_stopping) return;
    m_preview_in_flight = true; // Coalesce newer edits until the queued follow-up starts.
    const QPointer<OnboardingOptionsModel> observer{this};
    presentation::ReliablePost(m_services.post)([observer] {
        if (observer && !observer->m_stopping) observer->startPreview();
    });
}

void OnboardingOptionsModel::startPreview()
{
    if (m_stopping) return;
    m_preview_in_flight = true;
    const auto request_id = m_preview_request_id;const auto context=m_core_settings.previewContext();
    m_work->submit( [argv = m_argv, ipc = m_can_listen_ipc,
        selection = QmlOnboardingSettings::DataDirSelection{m_data_dir, m_data_dir_source}] {
        return QmlOnboardingSettings::Preview(*argv, ipc, selection);
    }, [this, request_id,context](const QmlOnboardingSettings::PreviewResult& preview) {
        if(m_stopping)return;
        m_preview_in_flight = false;
        if (request_id != m_preview_request_id) {
            queuePreview();
            return;
        }
        const QPointer<OnboardingOptionsModel> observer{this};
        applyPreview(preview,request_id,context);
    }, [this, request_id](std::exception_ptr) {
            if(m_stopping)return;
        m_preview_in_flight = false;
        if (request_id != m_preview_request_id) {
            queuePreview();
            return;
        }
        const QString error = tr("The selected directory could not be checked.");
        const QPointer<OnboardingOptionsModel> observer{this};
        setPreviewError(error);if(!observer||m_stopping)return;
        setPreviewPending(false);if(observer&&!m_stopping)Q_EMIT dataDirSelectionFinished(false, error);
    });
}

void OnboardingOptionsModel::applyPreview(const QmlOnboardingSettings::PreviewResult& preview,uint64_t request_id,quint64 context)
{
    const QPointer<OnboardingOptionsModel> observer{this};
    if(m_stopping)return;
    if (!preview.ok) {
        setPreviewError(preview.error);if(!observer||m_stopping)return;setPreviewPending(false);
        if(observer&&!m_stopping)Q_EMIT dataDirSelectionFinished(false,preview.error);
        return;
    }

    const int old_assumed_blockchain_size = m_assumed_blockchain_size;
    const int old_assumed_chainstate_size = m_assumed_chainstate_size;
    const bool old_existing_profile = m_profile.existing_profile;
    m_assumed_blockchain_size = preview.assumed_blockchain_size;
    m_assumed_chainstate_size = preview.assumed_chainstate_size;
    m_profile = preview.profile;
    if (m_profile.existing_profile && m_prune_auto_recommended) {
        m_core_settings.changePruneRecommendation(preview.values.prune, preview.values.prune_size_gb, /*touched=*/false);
        if(!observer||m_stopping)return;
        m_prune_auto_recommended = false;
    }
    if (old_assumed_blockchain_size != m_assumed_blockchain_size || old_assumed_chainstate_size != m_assumed_chainstate_size) {
        Q_EMIT assumedSizesChanged();
    if(!observer||m_stopping)return;
        emitStorageStatusChanged();
    if(!observer||m_stopping)return;
    }
    if (old_existing_profile != m_profile.existing_profile) {
        emitStorageStatusChanged();
    if(!observer||m_stopping)return;
    }

    const auto revision=request_id;
    m_core_settings.applyPreviewValuesPreservingTouched(preview.values,QmlCoreSettings::StatusProjection{preview.core_setting_statuses,preview.core_setting_statuses_bytes},[observer,revision](bool accepted){
        if(!observer||observer->m_stopping||revision!=observer->m_preview_request_id)return;
        if(!accepted){observer->setPreviewError(tr("The settings preview could not be adopted."));if(observer&&!observer->m_stopping)observer->setPreviewPending(false);return;}
        observer->m_core_settings.setAdmissionReady(true);if(!observer||observer->m_stopping)return;
        observer->setPreviewError({});if(!observer||observer->m_stopping)return;observer->requestStorageCheck();if(!observer||observer->m_stopping)return;
        observer->setPreviewPending(false);if(observer&&!observer->m_stopping)Q_EMIT observer->dataDirSelectionFinished(true,{});
    },false,{},context);
}

QmlOnboardingSettings::ApplyRequest OnboardingOptionsModel::applyRequest() const
{
    return {{m_data_dir, m_data_dir_source}, m_core_settings.touchedSettings(), coreValues()};
}
