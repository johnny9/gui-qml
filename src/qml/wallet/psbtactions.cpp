// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/psbtmodel.h>
#include <qml/wallet/walletpassphrase.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/walletunlockcontext.h>
#include <interfaces/wallet.h>
#include <QPointer>

namespace {
bool SignaturesChanged(const PartiallySignedTransaction& before, const PartiallySignedTransaction& after)
{
    for (size_t i = 0; i < before.inputs.size(); ++i) {
        const auto& a = before.inputs[i];
        const auto& b = after.inputs[i];
        if (a.partial_sigs != b.partial_sigs || a.final_script_sig != b.final_script_sig || a.final_script_witness.stack != b.final_script_witness.stack ||
            a.m_tap_key_sig != b.m_tap_key_sig || a.m_tap_script_sigs != b.m_tap_script_sigs) return true;
    }
    return false;
}
}

bool PsbtModel::importReview(TransactionReviewModel* review)
{
    if (!available() || busy() || !review || !review->snapshot()) return false;
    const auto snapshot = *review->snapshot();
    if (snapshot.session_id != m_session.id() || snapshot.session_generation != m_session.generation()) return false;
    // The ordinary send draft and its immutable review are never changed.
    CMutableTransaction transaction(*snapshot.transaction);
    for (auto& input : transaction.vin) {
        input.scriptSig.clear();
        input.scriptWitness.SetNull();
    }
    clear();
    m_pending = true;
    const auto revision = m_revision;
    struct Result { PsbtDocument document; PsbtInspection inspection; };
    const auto result = std::make_shared<Result>(Result{PsbtDocument{PartiallySignedTransaction(transaction), {}, true}, {}});
    const QPointer<PsbtModel> self(this);
    const bool queued = m_session.runRead([result, node = &m_node](interfaces::Wallet& wallet) {
        bool complete{false};
        if (wallet.fillPSBT({.sign = false, .finalize = false}, nullptr, result->document.current, complete))
            return WalletOperationResult::failure(WalletOperationResult::CoreError, tr("Core could not create the unsigned PSBT."));
        result->document.original = result->document.serialized();
        result->document.modified = false;
        result->inspection = InspectPsbt(result->document.current, wallet, *node);
        return WalletOperationResult{};
    }, [self, revision, result](WalletOperationResult operation) {
        if (!self || revision != self->m_revision) return;
        self->m_pending = false;
        if (operation.code != WalletOperationResult::Success) self->m_error = operation.error;
        else self->publish(std::move(result->document), std::move(result->inspection));
        Q_EMIT self->changed();
    });
    if (!queued) m_pending = false;
    Q_EMIT changed();
    return queued;
}

void PsbtModel::exportFile(const QString& path)
{
    if (path.isEmpty() || !available() || busy() || !m_document) return;
    m_pending = true;
    m_error.clear();
    const auto revision = m_revision;
    const QPointer<PsbtModel> self(this);
    const bool queued = m_session.runRead([document = *m_document, path](interfaces::Wallet&) {
        QString error;
        return WritePsbtDocument(document, path, error) ? WalletOperationResult{} : WalletOperationResult::failure(WalletOperationResult::CoreError, error);
    }, [self, revision](WalletOperationResult result) {
        if (!self || revision != self->m_revision) return;
        self->m_pending = false;
        self->m_error = result.error;
        Q_EMIT self->changed();
    });
    if (!queued) m_pending = false;
    Q_EMIT changed();
}

void PsbtModel::sign(const QString& passphrase)
{
    if ((!canSign() && !canUnlockForSigning()) || !m_document) return;
    ++m_revision;
    m_review.clear();
    m_pending = true;
    m_error.clear();
    struct Result { PsbtDocument document; PsbtInspection inspection; };
    const auto result = std::make_shared<Result>(Result{*m_document, {}});
    const auto revision = m_revision;
    const QPointer<PsbtModel> self(this);
    const bool queued = m_session.runAction([result, node = &m_node, secret = WalletPassphrase(passphrase)](interfaces::Wallet& wallet) {
        if (wallet.privateKeysDisabled() || wallet.hasExternalSigner())
            return WalletOperationResult::failure(WalletOperationResult::Unavailable, tr("No available local signing keys for this transaction."));
        WalletUnlockContext unlock(wallet, secret);
        if (!unlock.valid()) return WalletOperationResult::failure(WalletOperationResult::InvalidInput, unlock.error());
        result->inspection = InspectPsbt(result->document.current, wallet, *node);
        if (result->inspection.known || !result->inspection.can_sign)
            return WalletOperationResult::failure(WalletOperationResult::Unavailable, tr("No available local signing keys for this transaction."));
        bool complete{false};
        const auto before = result->document.current;
        if (wallet.fillPSBT({.sign = true}, nullptr, result->document.current, complete))
            return WalletOperationResult::failure(WalletOperationResult::CoreError, tr("Core could not add local signatures to this PSBT."));
        if (!SignaturesChanged(before, result->document.current)) {
            result->inspection.can_sign = result->inspection.needs_unlock = false;
            return WalletOperationResult::failure(WalletOperationResult::Unavailable, tr("Core added no new local signatures. The original PSBT is unchanged."));
        }
        result->document.modified = true;
        result->inspection = InspectPsbt(result->document.current, wallet, *node);
        return WalletOperationResult{};
    }, [self, revision, result](WalletOperationResult operation) {
        if (!self || revision != self->m_revision) return;
        self->m_pending = false;
        if (operation.code == WalletOperationResult::Success) self->publish(std::move(result->document), std::move(result->inspection));
        else {
            // No partial failure result is published. Keep the original document
            // and establish a fresh review revision for an explicit retry.
            self->publish(*self->m_document, operation.code == WalletOperationResult::Unavailable ? result->inspection : self->m_inspection);
            self->m_error = operation.error;
        }
        Q_EMIT self->changed();
    });
    if (!queued) { m_pending = false; m_error = tr("Another wallet action is running."); }
    Q_EMIT changed();
}
