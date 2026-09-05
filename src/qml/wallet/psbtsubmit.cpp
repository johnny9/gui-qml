// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/psbtmodel.h>
#include <qml/wallet/walletsession.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <node/types.h>
#include <QPointer>

bool PsbtModel::canSubmit() const
{
    const auto& snapshot = m_review.snapshot();
    return available() && !busy() && !m_submitted && !known() && m_error.isEmpty() && m_document && complete() &&
        m_inspection.fee && MoneyRange(*m_inspection.fee) && m_inspection.inputs_verified && snapshot && snapshot->revision == m_revision &&
        snapshot->session_id == m_session.id() && snapshot->session_generation == m_session.generation() &&
        snapshot->transaction->GetWitnessHash() == m_inspection.transaction->GetWitnessHash() && snapshot->fee == *m_inspection.fee;
}

void PsbtModel::submit()
{
    if (!canSubmit()) return;
    m_pending = true;
    const auto revision = m_revision;
    const auto snapshot = *m_review.snapshot();
    const QPointer<PsbtModel> self(this);
    const bool queued = m_session.runAction([document = *m_document, snapshot, node = &m_node](interfaces::Wallet& wallet) {
        // Re-analyze on the same original wallet worker. This only fills public
        // metadata/finalizes existing signatures; submission never signs again.
        const auto current = InspectPsbt(document.current, wallet, *node);
        if (!current.error.isEmpty() || current.known || !current.complete || !current.inputs_verified || !current.fee ||
            !MoneyRange(*current.fee) || !current.transaction || current.transaction->GetWitnessHash() != snapshot.transaction->GetWitnessHash() || *current.fee != snapshot.fee) {
            return WalletOperationResult::failure(WalletOperationResult::Unavailable, tr("The reviewed PSBT or its inputs changed. Import and review it again."));
        }
        const auto maximum_fee = wallet.getDefaultMaxTxFee();
        if (maximum_fee > 0 && *current.fee > maximum_fee)
            return WalletOperationResult::failure(WalletOperationResult::InvalidInput, tr("The transaction fee exceeds the wallet maximum."));
        std::string error;
        if (node->broadcastTransaction(current.transaction, maximum_fee, error) != node::TransactionError::OK)
            return WalletOperationResult::failure(WalletOperationResult::CoreError, tr("Core rejected the transaction: %1").arg(QString::fromStdString(error)));
        return WalletOperationResult{};
    }, [self, revision](WalletOperationResult result) {
        if (!self || revision != self->m_revision) return;
        self->m_pending = false;
        self->m_submitted = result.code == WalletOperationResult::Success;
        self->m_error = result.error;
        if (self->m_submitted) self->m_inspection.known = true;
        ++self->m_revision;
        self->m_review.clear(); // A retry requires a new, explicit review.
        Q_EMIT self->changed();
    });
    if (!queued) { m_pending = false; m_error = tr("Another wallet action is running."); }
    Q_EMIT changed();
}
