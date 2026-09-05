// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETWORKFLOWMODEL_H
#define BITCOIN_QML_WALLET_WALLETWORKFLOWMODEL_H

#include <qml/wallet/walletoperationexecutor.h>

class WalletManager;

/** Per-form result state. Secrets never become properties or shared manager errors. */
class WalletWorkflowModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(int resultCode READ resultCode NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY statusChanged)
    Q_PROPERTY(QStringList companions READ companions NOTIFY statusChanged)
    Q_PROPERTY(QString backupPath READ backupPath NOTIFY statusChanged)
public:
    explicit WalletWorkflowModel(WalletManager& manager, QObject* parent = nullptr) : QObject(parent), m_manager(manager) {}
    bool busy() const { return m_busy; }
    int resultCode() const { return m_result.code; }
    QString error() const { return m_result.error; }
    QStringList warnings() const { return m_result.warnings; }
    QStringList companions() const { return m_result.companions; }
    QString backupPath() const { return m_result.backup_path; }
    Q_INVOKABLE void clear() { if (!m_busy) { m_result = {}; Q_EMIT statusChanged(); } }
Q_SIGNALS:
    void statusChanged();
    void succeeded();
protected:
    bool fail(WalletOperationResult::Code code, const QString& error)
    {
        m_result = WalletOperationResult::failure(code, error);
        Q_EMIT statusChanged();
        return false;
    }
    bool launch(const QString& name, WalletOperationExecutor::Work work);
    WalletManager& m_manager;
private:
    bool m_busy{false};
    WalletOperationResult m_result;
};

#endif // BITCOIN_QML_WALLET_WALLETWORKFLOWMODEL_H
