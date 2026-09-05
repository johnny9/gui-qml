// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletworkflowmodel.h>
#include <qml/wallet/walletmanager.h>
#include <QPointer>

bool WalletWorkflowModel::launch(const QString& name, WalletOperationExecutor::Work work)
{
    if (m_busy || m_manager.busy()) return fail(WalletOperationResult::Busy, tr("Another wallet operation is in progress."));
    m_result = {};
    m_busy = true;
    Q_EMIT statusChanged();
    QPointer<WalletWorkflowModel> self(this);
    if (!m_manager.beginOperation(name, std::move(work), [self](WalletOperationResult result) {
        if (!self) return;
        self->m_busy = false;
        result.wallet.reset();
        self->m_result = std::move(result);
        Q_EMIT self->statusChanged();
        if (self->m_result.code == WalletOperationResult::Success) Q_EMIT self->succeeded();
    })) {
        m_busy = false;
        return fail(WalletOperationResult::Unavailable, tr("Wallets are not available."));
    }
    return true;
}
