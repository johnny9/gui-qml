// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/activityexportoperation.h>
#include <qml/models/activitysnapshot.h>
#include <qml/retainedvalue.h>

#include <QUuid>

#include <mutex>
#include <utility>

namespace {
struct ExportSource {
    mutable std::mutex mutex;
    std::shared_ptr<activityexport::Request> retained;
    std::shared_ptr<activityexport::Request> hold() const {
        std::lock_guard lock{mutex}; return retained;
    }
    void clear() {
        std::shared_ptr<activityexport::Request> retired;
        { std::lock_guard lock{mutex}; retired = std::move(retained); }
        // Drop the reserved alias, never the large request itself on GUI.
        retired.reset();
    }
};
} // namespace

ActivityExportOperation::ActivityExportOperation(activityexport::Request request, activityexport::Files files,
    execution::SnapshotServices execution, std::function<activityexport::Result(const activityexport::Request&)> writer, QObject* parent)
    : QObject(parent), m_id(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      m_revision(request.snapshot && request.snapshot->snapshot ? QString::number(request.snapshot->snapshot->revision) : QString{}), m_unit(request.unit)
{
    if (!request.snapshot || !execution.scheduler || !execution.post || !files.inspect || !files.install || !writer) {
        m_error = tr("The original Activity export is unavailable."); return;
    }
    // Reserve before capturing the potentially large immutable frame. Aliases
    // may disappear on GUI; the original request is destroyed on Cleanup.
    auto retained = execution::RetainValues<activityexport::Request>(execution.scheduler,
        [&request] { return std::make_unique<activityexport::Request>(std::move(request)); });
    if (!retained) { m_error = tr("Activity export cleanup capacity is unavailable."); return; }
    const auto path = retained->path;
    auto source = std::make_shared<ExportSource>(); source->retained = std::move(retained);
    backup::Services services;
    services.scheduler = execution.scheduler; services.post = execution.post;
    services.available = files.available; services.wallet_name = [] { return std::string{"activity"}; };
    services.inspect = [inspect = files.inspect](const std::string& raw, const std::string&) {
        return inspect(QString::fromUtf8(raw.data(), static_cast<qsizetype>(raw.size())));
    };
    services.write = [source, files, writer](const backup::Proposal& target, bool approved) {
        const auto original_source = source->hold();
        if (!original_source) return backup::WriteResult{backup::Error::Transport};
        return files.install(target, approved, [original_source, writer](const std::string& stage) {
            auto original = *original_source;
            original.path = QString::fromUtf8(stage.data(), static_cast<qsizetype>(stage.size()));
            return writer(original).saved;
        });
    };
    services.outcome = [source, outcome = files.outcome, id = m_id, revision = m_revision, unit = m_unit](backup::Snapshot state) {
        source->clear(); // Native retirement does not wait for GUI delivery or projection destruction.
        if (outcome) outcome({id, revision, QString::fromUtf8(state.target.path.data(), static_cast<qsizetype>(state.target.path.size())), unit,
            state.phase, state.error, true, state.phase == backup::Phase::Succeeded, state.error >= backup::Error::None && state.error <= backup::Error::Transport && state.error != backup::Error::Unknown});
    };
    services.abandon = [source] { source->clear(); };
    m_policy = new BackupFileOperation(path, std::move(services), this);
    connect(m_policy, &BackupFileOperation::changed, this, &ActivityExportOperation::changed);
}
ActivityExportOperation::~ActivityExportOperation() { retire(); }
BackupFileOperation::Phase ActivityExportOperation::phase() const { return m_policy ? m_policy->phase() : BackupFileOperation::Phase::Failed; }
bool ActivityExportOperation::pending() const { return m_policy && m_policy->pending(); }
bool ActivityExportOperation::confirmationRequired() const { return m_policy && m_policy->confirmationRequired(); }
bool ActivityExportOperation::replacing() const { return m_policy && m_policy->replacing(); }
bool ActivityExportOperation::cancelled() const { return m_policy && (m_policy->phase() == BackupFileOperation::Phase::Declined || m_policy->phase() == BackupFileOperation::Phase::Retired); }
ActivityExportOperation::Error ActivityExportOperation::errorCode() const
{
    if (!m_policy) return Rejected;
    switch (m_policy->errorCode()) {
    case backup::Error::None: return NoError;
    case backup::Error::InvalidPath: return InvalidPath;
    case backup::Error::Inspection: return Inspection;
    case backup::Error::UnsupportedTarget: return UnsupportedTarget;
    case backup::Error::Staging: return Staging;
    case backup::Error::CoreBackup: return Write;
    case backup::Error::Changed: return Changed;
    case backup::Error::Install: return Install;
    case backup::Error::Unknown: return OutcomeUnknown;
    case backup::Error::Transport: return Rejected;
    }
    return OutcomeUnknown;
}
bool ActivityExportOperation::resultPresent() const { return !pending() && !confirmationRequired(); }
bool ActivityExportOperation::saved() const { return m_policy && m_policy->succeeded(); }
bool ActivityExportOperation::outcomeKnown() const { return !m_policy || errorCode() != OutcomeUnknown; }
QString ActivityExportOperation::finalPath() const { return m_policy ? m_policy->finalPath() : QString{}; }
QString ActivityExportOperation::error() const {
    if (!m_policy) return m_error;
    switch (errorCode()) {
    case NoError: return {};
    case InvalidPath: return tr("Choose a local CSV file.");
    case Inspection: return tr("The export destination could not be inspected.");
    case UnsupportedTarget: return tr("Choose a regular file as the export destination.");
    case Staging: case Write: return tr("The Activity CSV could not be written.");
    case Changed: return tr("The destination changed after confirmation. Choose it again.");
    case Install: return tr("The Activity CSV could not be installed at its destination.");
    case OutcomeUnknown: return tr("The export outcome is unknown. Check the original destination.");
    case Rejected: return tr("The Activity export could not be scheduled.");
    }
    return {};
}
ActivityExportResult ActivityExportOperation::result() const
{
    return {m_id, m_revision, finalPath(), error(), m_unit, resultPresent(), saved(), cancelled(), outcomeKnown()};
}
bool ActivityExportOperation::start() { return m_policy && m_policy->start(); }
bool ActivityExportOperation::approve() { return m_policy && m_policy->approve(); }
bool ActivityExportOperation::refuse() { return m_policy && m_policy->refuse(); }
void ActivityExportOperation::retire() { if (m_policy) m_policy->retire(); }
