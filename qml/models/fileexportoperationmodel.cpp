// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/fileexportoperationmodel.h>
#include <qml/guipresentation.h>

#include <QPointer>
#include <QUuid>

namespace {
using Scope = std::shared_ptr<execution::Scheduler>;
FileExportError::Code FileError(backup::Error error)
{
    switch (error) {
    case backup::Error::None: return FileExportError::None;
    case backup::Error::InvalidPath: return FileExportError::InvalidPath;
    case backup::Error::Inspection: return FileExportError::Inspection;
    case backup::Error::UnsupportedTarget: return FileExportError::UnsupportedTarget;
    case backup::Error::Staging: case backup::Error::CoreBackup: return FileExportError::Encoding;
    case backup::Error::Changed: return FileExportError::Changed;
    case backup::Error::Install: return FileExportError::Install;
    case backup::Error::Unknown: return FileExportError::Unknown;
    case backup::Error::Transport: return FileExportError::Rejected;
    }
    return FileExportError::Unknown;
}
}
struct FileExportOperationModel::Cell {
    std::mutex mutex;
    Phase phase{Checking};
    FileExportError::Code error{FileExportError::None};
    requestshare::Error source_error{requestshare::Error::None};
    FileExportResult result;
    backup::Proposal target;
    std::shared_ptr<requestshare::Grant> grant;
    std::atomic_bool cancelled{false};
    bool pending{true};
    std::function<void()> notify;
};
struct FileExportOperationModel::Work : std::enable_shared_from_this<Work> {
    const int64_t id;
    requestshare::Services services;
    const std::shared_ptr<requestshare::View> view;
    const uint64_t view_token;
    const uint64_t input_revision;
    const std::shared_ptr<Cell> cell;
    backup::Snapshot policy;
    QImage image;
    std::optional<requestshare::Source> expected;
    Work(int64_t request_id, requestshare::Services ports, std::shared_ptr<requestshare::View> interest, std::shared_ptr<Cell> result)
        : id(request_id), services(std::move(ports)), view(std::move(interest)), view_token(view->token.load()),
          input_revision(view->input_epoch ? view->input_epoch->value.load() : 0), cell(std::move(result)) {}
    void notice() { cell->notify(); }
    void finish(FileExportError::Code error, requestshare::Error source_error = requestshare::Error::None,
                bool written = false, bool unknown = false)
    {
        {
            std::lock_guard lock{cell->mutex};
            if (cell->result.present) return;
            const bool admitted = cell->grant && cell->grant->admitted();
            cell->pending = false;
            cell->error = error; cell->source_error = source_error;
            cell->result.present = true; cell->result.admitted = admitted;
            cell->result.written = written; cell->result.outcomeKnown = !unknown;
            cell->phase = unknown ? OutcomeUnknown : written ? Completed : error == FileExportError::Cancelled ? Cancelled : Failed;
        }
        notice();
    }
    bool stopped()
    {
        { std::lock_guard lock{cell->mutex}; if (cell->result.present) return true; }
        if (!cell->cancelled.load()) return false;
        finish(FileExportError::Cancelled); return true;
    }
    template<typename Effect, typename Done>
    bool step(Scope scope, execution::Lane lane, size_t bytes, Effect effect, Done done, bool mutation = false)
    {
        if (!scope) { finish(FileExportError::Rejected); return false; }
        const auto self = shared_from_this();
        const auto admission = execution::Submit(*scope, std::move(effect),
            [self, scope, done = std::move(done)](auto result) mutable { done(std::move(result), std::move(scope)); },
            [self, scope, mutation](std::exception_ptr failure) {
                // A thrown native writer can have installed a file. Only an
                // adapter's known receipt may classify it as not written.
                try { std::rethrow_exception(failure); }
                catch (const execution::NotStarted&) { self->finish(FileExportError::Rejected); return; }
                catch (...) { }
                self->finish(mutation ? FileExportError::Unknown : FileExportError::Rejected, requestshare::Error::None, false, mutation);
            }, {lane, bytes});
        if (!admission) { finish(FileExportError::Rejected); return false; }
        return true;
    }
    void begin(Scope scope)
    {
        if (stopped()) return;
        if (!view->active(view_token, input_revision)) { finish(FileExportError::Source, requestshare::Error::Inactive); return; }
        if (expected && (expected->error != requestshare::Error::None || !expected->current || !expected->current())) {
            finish(FileExportError::Source, expected->error == requestshare::Error::None ? requestshare::Error::Changed : expected->error); return;
        }
        const auto self = shared_from_this();
        if (!services.read || !services.files || !services.files->inspect || !services.files->write || !services.render) { finish(FileExportError::Rejected); return; }
        const auto admission = services.read(id, [self, scope](requestshare::Source source) mutable {
            if (self->stopped()) return;
            if (!source.opening || source.request_id != self->id || source.record_revision == 0) {
                self->finish(FileExportError::Source, requestshare::Error::Changed); return;
            }
            if (self->expected && (self->expected->opening != source.opening || self->expected->request_id != source.request_id ||
                self->expected->record_revision != source.record_revision || self->expected->payload != source.payload)) {
                self->finish(FileExportError::Source, requestshare::Error::Changed); return;
            }
            if (source.error != requestshare::Error::None || source.payload.isEmpty() || !source.current) {
                self->finish(FileExportError::Source, source.error == requestshare::Error::None ? requestshare::Error::Missing : source.error); return;
            }
            if (source.payload.size() > 16'384) { self->finish(FileExportError::Source, requestshare::Error::Oversize); return; }
            const auto grant = std::make_shared<requestshare::Grant>(source, self->view, self->view_token, self->input_revision);
            {
                std::lock_guard lock{self->cell->mutex};
                self->cell->grant = grant;
                self->cell->phase = Preparing;
                self->cell->result.requestId = QString::number(source.request_id);
                self->cell->result.payloadRevision = QString::number(source.record_revision);
            }
            self->notice();
            self->step(std::move(scope), execution::Lane::Domain, 2'500'000,
                [render = self->services.render, payload = std::move(source.payload)] { return render(payload); },
                [self](QImage image, Scope original) {
                    if (self->stopped()) return;
                    if (image.isNull() || image.width() <= 1 || image.width() != image.height() || image.width() > 736) {
                        self->finish(FileExportError::Source, requestshare::Error::QrEncoding); return;
                    }
                    self->image = std::move(image);
                    // No effect/capability waits for a chooser or GUI turn.
                    original.reset();
                    { std::lock_guard lock{self->cell->mutex}; self->cell->phase = Choosing; self->cell->pending = false; }
                    self->notice();
                });
        });
        if (!admission) finish(FileExportError::Rejected);
    }
    void inspect(QUrl destination, Scope scope)
    {
        if (stopped()) return;
        policy = backup::Advance(policy, backup::Event::Start).snapshot;
        const auto self = shared_from_this();
        step(std::move(scope), execution::Lane::Core, 65'536,
            [inspect = services.files->inspect, destination = std::move(destination)] { return inspect(destination); },
            [self](backup::Proposal proposal, Scope original) {
                if (self->stopped()) return;
                const auto decision = backup::Advance(self->policy, backup::Event::Inspected, proposal);
                self->policy = decision.snapshot;
                if (self->policy.phase == backup::Phase::Failed) { self->finish(FileError(self->policy.error)); return; }
                {
                    std::lock_guard lock{self->cell->mutex};
                    self->cell->target = proposal;
                    self->cell->result.finalPath = QString::fromUtf8(proposal.path.data(), static_cast<qsizetype>(proposal.path.size()));
                }
                if (self->policy.phase == backup::Phase::AwaitingConfirmation) {
                    original.reset();
                    { std::lock_guard lock{self->cell->mutex}; self->cell->phase = Confirming; self->cell->pending = false; }
                    self->notice();
                } else self->write(std::move(original));
            });
    }
    void write(Scope scope)
    {
        if (stopped()) return;
        const auto decision = backup::Advance(policy, backup::Event::AdmitWrite);
        if (!decision.accepted) { finish(FileExportError::Rejected); return; }
        policy = decision.snapshot;
        std::shared_ptr<requestshare::Grant> grant;
        { std::lock_guard lock{cell->mutex}; cell->phase = Writing; grant = cell->grant; }
        notice();
        const auto self = shared_from_this();
        step(std::move(scope), execution::Lane::Core, 2'500'000,
            [writer = services.files->write, target = policy.target, approved = policy.destination_approved, pixels = image, grant] {
                return writer(target, approved, pixels, grant);
            }, [self, grant](backup::WriteResult result, Scope) {
                self->policy = backup::Advance(self->policy, backup::Event::Written, {}, result).snapshot;
                if (result.error == backup::Error::None) {
                    if (!grant || !grant->admitted()) { self->finish(FileExportError::Unknown, requestshare::Error::None, false, true); return; }
                    self->finish(FileExportError::None, requestshare::Error::None, true); return;
                }
                if (self->cell->cancelled.load() && (!grant || !grant->admitted())) { self->finish(FileExportError::Cancelled); return; }
                if (grant && !grant->admitted() && !grant->current() && result.error == backup::Error::Changed) {
                    self->finish(FileExportError::Source, requestshare::Error::Changed); return;
                }
                self->finish(FileError(result.error), requestshare::Error::None, false, result.error == backup::Error::Unknown);
            }, true);
    }
};

FileExportOperationModel::FileExportOperationModel(int64_t id, requestshare::Services services, std::shared_ptr<requestshare::View> view,
    std::optional<requestshare::Source> expected, QObject* parent)
    : QObject(parent), m_id(QUuid::createUuid().toString(QUuid::WithoutBraces)), m_cell(std::make_shared<Cell>()),
      m_work(std::make_shared<Work>(id, std::move(services), std::move(view), m_cell))
{
    m_work->expected = std::move(expected);
    m_cell->result.operationId = m_id;
    const QPointer<FileExportOperationModel> observer{this};
    const auto post = presentation::ReliablePost(m_work->services.execution.post);
    m_cell->notify = [observer, post] { post([observer] { if (observer) Q_EMIT observer->changed(); }); };
    auto scope = m_work->services.execution.scheduler ? m_work->services.execution.scheduler->openActionScope(512) : nullptr;
    const auto work = m_work;
    if (scope) work->step(scope, execution::Lane::Domain, 1024, [] { return true; }, [work](bool, Scope original) { work->begin(std::move(original)); });
    else work->finish(FileExportError::Rejected);
}
FileExportOperationModel::~FileExportOperationModel() { cancel(); }
FileExportOperationModel::Phase FileExportOperationModel::phase() const { std::lock_guard lock{m_cell->mutex}; return m_cell->phase; }
bool FileExportOperationModel::busy() const { const auto value = phase(); return value != Choosing && value != Confirming && value < Completed; }
bool FileExportOperationModel::canCancel() const { return phase() < Completed; }
bool FileExportOperationModel::replacing() const { std::lock_guard lock{m_cell->mutex}; return m_cell->target.exists; }
QString FileExportOperationModel::finalPath() const { std::lock_guard lock{m_cell->mutex}; return m_cell->result.finalPath; }
FileExportError::Code FileExportOperationModel::error() const { std::lock_guard lock{m_cell->mutex}; return m_cell->error; }
FileExportResult FileExportOperationModel::result() const { std::lock_guard lock{m_cell->mutex}; return m_cell->result; }
QString FileExportOperationModel::errorText() const
{
    FileExportError::Code error;
    requestshare::Error source;
    { std::lock_guard lock{m_cell->mutex}; error = m_cell->error; source = m_cell->source_error; }
    switch (error) {
    case FileExportError::None: return {};
    case FileExportError::Source: return requestshare::ErrorText(source);
    case FileExportError::Rejected: return tr("The image export could not be scheduled.");
    case FileExportError::InvalidPath: return tr("Choose a valid local PNG destination.");
    case FileExportError::Inspection: return tr("The image destination could not be inspected.");
    case FileExportError::UnsupportedTarget: return tr("Choose a regular file as the image destination.");
    case FileExportError::Encoding: return tr("The QR image could not be written in that directory.");
    case FileExportError::Changed: return tr("The destination changed. Choose the image location again.");
    case FileExportError::Install: return tr("The image could not be installed. Check the destination before retrying.");
    case FileExportError::Unknown: return requestshare::ErrorText(requestshare::Error::Unknown);
    case FileExportError::Cancelled: return tr("The image export was cancelled.");
    case FileExportError::Chooser: return tr("The file chooser could not be opened.");
    }
    return {};
}
bool FileExportOperationModel::choose(const QUrl& destination)
{
    if (destination.toString().size() > 32'768) { Q_EMIT commandRejected(QStringLiteral("choose")); return false; }
    {
        std::lock_guard lock{m_cell->mutex};
        if (m_cell->phase != Choosing || m_cell->pending) return false;
        m_cell->pending = true; m_cell->phase = Inspecting;
    }
    auto scope = m_work->services.execution.scheduler ? m_work->services.execution.scheduler->openActionScope(512) : nullptr;
    const auto work = m_work;
    const bool admitted = work->step(scope, execution::Lane::Domain, 65'536, [destination] { return destination; },
        [work](QUrl target, Scope original) { work->inspect(std::move(target), std::move(original)); });
    Q_EMIT changed(); return admitted;
}
bool FileExportOperationModel::approve()
{
    {
        std::lock_guard lock{m_cell->mutex};
        if (m_cell->phase != Confirming || m_cell->pending) return false;
        m_cell->pending = true; m_cell->phase = Writing;
    }
    auto scope = m_work->services.execution.scheduler ? m_work->services.execution.scheduler->openActionScope(512) : nullptr;
    const auto work = m_work;
    const bool admitted = work->step(scope, execution::Lane::Domain, 1024, [] { return true; },
        [work](bool, Scope original) {
            const auto decision = backup::Advance(work->policy, backup::Event::Approve);
            if (!decision.accepted) { work->finish(FileExportError::Rejected); return; }
            work->policy = decision.snapshot; work->write(std::move(original));
        });
    Q_EMIT changed(); return admitted;
}
void FileExportOperationModel::cancel()
{
    bool waiting{false};
    {
        std::lock_guard lock{m_cell->mutex};
        if (m_cell->result.present || m_cell->cancelled.exchange(true)) return;
        if (m_cell->grant) m_cell->grant->revoke();
        waiting = m_cell->pending;
        m_cell->phase = Cancelling;
    }
    if (!waiting) m_work->finish(FileExportError::Cancelled);
    else Q_EMIT changed();
}
void FileExportOperationModel::chooserFailed()
{
    if (phase() != Choosing) return;
    m_work->finish(FileExportError::Chooser);
}
