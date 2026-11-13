// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BUMPTRANSACTIONMODEL_H
#define BITCOIN_QML_MODELS_BUMPTRANSACTIONMODEL_H

#include <qml/walletbump.h>
#include <qml/walletopening.h>
#include <qml/snapshotquery.h>
#include <QObject>
#include <QString>

class BumpTransactionModel : public QObject
{
    Q_OBJECT
public:
    enum State { Idle, Preparing, NeedsConfirmation, Committing, Succeeded, Failed, Discarding, Uncertain, Closed };
    Q_ENUM(State)
    enum ActionType { SpeedUp };
    Q_ENUM(ActionType)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(quint64 attemptId READ attemptId NOTIFY stateChanged)
    Q_PROPERTY(quint64 commitId READ commitId NOTIFY stateChanged)
    Q_PROPERTY(ActionType actionType READ actionType NOTIFY actionTypeChanged)
    Q_PROPERTY(QString oldFee READ oldFee NOTIFY resultChanged)
    Q_PROPERTY(QString newFee READ newFee NOTIFY resultChanged)
    Q_PROPERTY(QString feeIncrease READ feeIncrease NOTIFY resultChanged)
    Q_PROPERTY(QString oldTxid READ oldTxid NOTIFY resultChanged)
    Q_PROPERTY(QString newTxid READ newTxid NOTIFY resultChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY resultChanged)
    Q_PROPERTY(QString unlockErrorText READ unlockErrorText NOTIFY resultChanged)
    Q_PROPERTY(bool needsUnlock READ needsUnlock NOTIFY needsUnlockChanged)

    explicit BumpTransactionModel(interfaces::Wallet* wallet, QObject* parent = nullptr);
    void setOpening(std::shared_ptr<walletopening::Session> opening, execution::SnapshotServices services);
    void closeAdmission();
    void retryPublications();
    void detachWallet() { closeAdmission(); m_wallet = nullptr; }
    State state() const { return m_state; }
    bool busy() const { return m_preparing || m_committing; }
    quint64 attemptId() const { return m_attempt_id; }
    quint64 commitId() const { return m_commit_id; }
    ActionType actionType() const { return SpeedUp; }
    QString oldFee() const { return m_old_fee; }
    QString newFee() const { return m_new_fee; }
    QString feeIncrease() const { return m_increase; }
    QString oldTxid() const { return m_old_txid; }
    QString newTxid() const { return m_new_txid; }
    QString errorText() const { return m_error; }
    QString unlockErrorText() const { return m_unlock_error; }
    bool needsUnlock() const { return m_needs_unlock; }
    std::optional<walletbump::Commit> commitResult() const;
    std::shared_ptr<const execution::OwnedCommand<walletbump::Commit>> commitOperation() const { return m_commit_operation; }
    Q_INVOKABLE quint64 requestPrepareFeeBump(const QString& txid, unsigned int target_blocks);
    Q_INVOKABLE quint64 requestConfirmFeeBump();
    Q_INVOKABLE quint64 requestConfirmFeeBumpWithPassphrase(const QString& passphrase);
    // Compatibility methods return admission, never completion.
    void prepareFeeBump(const QString& txid, unsigned int target_blocks) { requestPrepareFeeBump(txid, target_blocks); }
    bool confirmFeeBump() { return requestConfirmFeeBump() != 0; }
    bool confirmFeeBumpWithPassphrase(const QString& passphrase) { return requestConfirmFeeBumpWithPassphrase(passphrase) != 0; }
    Q_INVOKABLE void reset();
    void setSecurityStateChangedFn(std::function<void()> fn);

Q_SIGNALS:
    void stateChanged();
    void actionTypeChanged();
    void resultChanged();
    void needsUnlockChanged();

private:
    quint64 confirm(std::optional<SecureString> passphrase, secretinput::Captured captured = {});
    void publishPreparation(quint64 attempt, quint64 revision);
    void publishCommit(quint64 attempt, quint64 commit);
    bool publish();
    void clearPresentation();
    interfaces::Wallet* m_wallet{nullptr}; // GUI association only, never transported.
    std::shared_ptr<walletopening::Session> m_opening;
    execution::SnapshotServices m_services;
    std::shared_ptr<const walletbump::Prepared> m_review;
    std::shared_ptr<execution::OwnedCommand<walletbump::Preparation>> m_prepare_operation;
    std::shared_ptr<execution::OwnedCommand<walletbump::Commit>> m_commit_operation;
    std::function<void()> m_security_state_changed;
    State m_state{Idle};
    QString m_old_fee, m_new_fee, m_increase, m_old_txid, m_new_txid, m_error, m_unlock_error;
    bool m_needs_unlock{false}, m_closed{false}, m_preparing{false}, m_committing{false};
    quint64 m_attempt_id{0}, m_commit_id{0}, m_view_revision{0}, m_preparation_revision{0};
};

#endif // BITCOIN_QML_MODELS_BUMPTRANSACTIONMODEL_H
