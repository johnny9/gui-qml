// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BACKUPPOLICYOPERATION_H
#define BITCOIN_QML_MODELS_BACKUPPOLICYOPERATION_H

#include <qml/execution.h>
#include <qml/models/backuppolicy.h>
#include <qml/models/backuppolicyio.h>

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace backup {
struct Services {
    std::shared_ptr<execution::Scheduler> scheduler;
    std::function<void(std::function<void()>)> post;
    std::function<std::string()> wallet_name;
    std::function<Proposal(const std::string&, const std::string&)> inspect;
    std::function<WriteResult(const Proposal&, bool)> write;
    std::shared_ptr<InstallCoordinator> coordinator;
    // Consumed on the backend lane, even if the GUI observer has disappeared.
    std::function<void(Snapshot)> outcome;
};
struct OperationState;
} // namespace backup

/** One immutable request identity; projections never own wallet or file handles. */
class BackupFileOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool confirmationRequired READ confirmationRequired NOTIFY changed)
    Q_PROPERTY(bool replacing READ replacing NOTIFY changed)
    Q_PROPERTY(QString finalPath READ finalPath NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    enum class Phase { Queued, Inspecting, AwaitingConfirmation, Ready, Writing, Succeeded, Failed, Declined, Retired };
    Q_ENUM(Phase)
    BackupFileOperation(QString raw, backup::Services services, QObject* parent = nullptr);
    ~BackupFileOperation() override;
    bool start();
    Phase phase() const { return static_cast<Phase>(m_projection.phase); }
    bool pending() const { return backup::Pending(m_projection.phase); }
    bool confirmationRequired() const { return m_projection.phase == backup::Phase::AwaitingConfirmation; }
    bool replacing() const { return m_projection.target.exists; }
    QString finalPath() const;
    QString error() const;
    Q_INVOKABLE bool approve();
    Q_INVOKABLE bool refuse();
    void retire();
Q_SIGNALS:
    void changed();
private:
    bool submit(backup::Event event);
    void apply(backup::Snapshot projection);
    std::shared_ptr<backup::OperationState> m_backend;
    backup::Snapshot m_projection;
    bool m_started{false};
    bool m_admitted{false};
};

#endif // BITCOIN_QML_MODELS_BACKUPPOLICYOPERATION_H
