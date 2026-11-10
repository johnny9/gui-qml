// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/backuppolicyoperation.h>

#include <QPointer>

#include <atomic>
#include <utility>

namespace backup {
struct OperationState {
    QString raw;
    Services services;
    Snapshot current;
    enum class Admission { Open, Writing, Retired };
    std::atomic<Admission> admission{Admission::Open};
    std::function<void(Snapshot)> publish;

    Decision advance(Event event, const Proposal& proposal = {}, WriteResult result = {})
    {
        auto decision = Advance(current, event, proposal, result);
        if (!decision.accepted) return decision;
        current = decision.snapshot;
        // GUI transport cannot own the outcome or interrupt an admitted write.
        try { publish(current); } catch (...) { }
        if (!Pending(current.phase) && services.outcome) services.outcome(current);
        return decision;
    }
    void consume(Event event)
    {
        try {
            if (admission.load() == Admission::Retired && Pending(current.phase)) {
                advance(Event::Retire);
                return;
            }
            auto decision = advance(event);
            if (!decision.accepted) return;
            if (decision.effect == Effect::Inspect) {
                auto proposal = services.inspect(RawUtf8(), services.wallet_name());
                if (admission.load() == Admission::Retired) { advance(Event::Retire); return; }
                advance(Event::Inspected, proposal);
            }
            if (current.phase == Phase::Ready) {
                auto expected = Admission::Open;
                if (!admission.compare_exchange_strong(expected, Admission::Writing)) { advance(Event::Retire); return; }
                decision = advance(Event::AdmitWrite);
                if (decision.effect == Effect::Write) {
                    const auto result = services.write(current.target, current.destination_approved);
                    advance(Event::Written, {}, result);
                }
            }
        } catch (...) {
            advance(Event::Fault, {}, {Error::Unknown});
        }
    }
    std::string RawUtf8() const
    {
        // Reject before allocating an unbounded encoded command.
        if (raw.size() > 32'768) return {};
        const auto bytes = raw.toUtf8();
        return {bytes.constData(), static_cast<size_t>(bytes.size())};
    }
};
} // namespace backup

BackupFileOperation::BackupFileOperation(QString raw, backup::Services services, QObject* parent)
    : QObject(parent), m_backend(std::make_shared<backup::OperationState>())
{
    static_assert(static_cast<int>(Phase::Retired) == static_cast<int>(backup::Phase::Retired));
    m_backend->raw = std::move(raw);
    m_backend->services = std::move(services);
    const QPointer<BackupFileOperation> observer{this};
    m_backend->publish = [post = m_backend->services.post, observer](backup::Snapshot projection) {
        post([observer, projection = std::move(projection)]() mutable {
            if (observer) observer->apply(std::move(projection));
        });
    };
}

BackupFileOperation::~BackupFileOperation()
{
    retire();
}

bool BackupFileOperation::start()
{
    if (m_started) return false;
    m_started = true;
    const auto& services = m_backend->services;
    if (!services.scheduler || !services.post || !services.wallet_name || !services.inspect || !services.write) {
        m_projection.phase = backup::Phase::Failed;
        m_projection.error = backup::Error::Transport;
        ++m_projection.sequence;
        Q_EMIT changed();
        return false;
    }
    return submit(backup::Event::Start);
}

bool BackupFileOperation::submit(backup::Event event)
{
    const auto state = m_backend;
    const auto admission = execution::Submit(*state->services.scheduler,
        [state, event] { state->consume(event); }, [] {});
    if (event == backup::Event::Start && admission) m_admitted = true;
    if (!admission && event == backup::Event::Start) {
        m_projection.phase = backup::Phase::Failed;
        m_projection.error = backup::Error::Transport;
        ++m_projection.sequence;
        Q_EMIT changed();
    }
    return bool(admission);
}

bool BackupFileOperation::approve()
{
    return m_started && m_backend->admission.load() != backup::OperationState::Admission::Retired && submit(backup::Event::Approve);
}

bool BackupFileOperation::refuse()
{
    return m_started && m_backend->admission.load() != backup::OperationState::Admission::Retired && submit(backup::Event::Refuse);
}

void BackupFileOperation::retire()
{
    const auto state = m_backend;
    auto expected = backup::OperationState::Admission::Open;
    if (!state->admission.compare_exchange_strong(expected, backup::OperationState::Admission::Retired)
        || !m_admitted) return;
    execution::Submit(*state->services.scheduler, [state] { state->consume(backup::Event::Retire); }, [] {});
}

void BackupFileOperation::apply(backup::Snapshot projection)
{
    if (projection.sequence <= m_projection.sequence) return;
    m_projection = std::move(projection);
    Q_EMIT changed();
}

QString BackupFileOperation::finalPath() const
{
    const auto& path = m_projection.target.path;
    return QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
}

QString BackupFileOperation::error() const
{
    switch (m_projection.error) {
    case backup::Error::None: return {};
    case backup::Error::InvalidPath: return tr("Choose a valid local backup destination.");
    case backup::Error::Inspection: return tr("The backup destination could not be inspected.");
    case backup::Error::UnsupportedTarget: return tr("Choose a regular file as the backup destination.");
    case backup::Error::Staging: return tr("A backup could not be created in that directory.");
    case backup::Error::CoreBackup: return tr("The wallet could not be backed up.");
    case backup::Error::Changed: return tr("The destination changed. Choose the backup location again.");
    case backup::Error::Install: return tr("The backup could not be installed. Check the destination before retrying.");
    case backup::Error::Unknown: return tr("The backup outcome could not be confirmed. Check the destination before retrying.");
    case backup::Error::Transport: return tr("The backup request could not be scheduled.");
    }
    return tr("The backup outcome could not be confirmed. Check the destination before retrying.");
}
