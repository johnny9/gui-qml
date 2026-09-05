// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletsendmodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/sendpreview.h>
#include <qml/wallet/sendprepare.h>
#include <qml/wallet/sendsubmit.h>
#include <qml/wallet/walletpassphrase.h>
#include <interfaces/wallet.h>
#include <QPointer>

WalletSendModel::WalletSendModel(WalletSession& session, CFeeRate dust_relay_fee, QObject* parent)
    : QObject(parent), m_session(session), m_recipients(dust_relay_fee, this), m_coins(session, this),
      m_fees([&session](SendDraftSnapshot draft, quint64 request_id, FeeSelectionModel::Complete complete) {
          auto preview = std::make_shared<FeePreview>(FeePreview{draft.session_id, draft.session_generation, draft.revision, request_id, {}, false, false, {}});
          const bool accepted = session.runRead([preview, draft](interfaces::Wallet& backend) {
              *preview = EstimateSendFee(backend, draft, preview->request_id);
              return WalletOperationResult{};
          }, [preview, complete](WalletOperationResult result) {
              if (result.code != WalletOperationResult::Success) preview->error = result.error;
              complete(*preview);
          });
          if (!accepted) {
              preview->error = tr("The wallet is unavailable.");
              complete(*preview);
          }
      }, this), m_review(this)
{
    connect(&m_recipients, &SendRecipientsListModel::edited, this, &WalletSendModel::draftEdited);
    connect(&session, &WalletSession::invalidated, this, &WalletSendModel::draftEdited);
    connect(&session, &WalletSession::changed, this, [this] {
        if (!m_preparing && !m_submitting && m_prepared) invalidateReview();
        Q_EMIT changed();
    });
    connect(&session, &WalletSession::actionBusyChanged, this, [this] {
        if (m_session.actionBusy() && !m_preparing && !m_submitting) invalidateReview();
        Q_EMIT changed();
    });
    connect(&session, &WalletSession::activityChanged, this, &WalletSendModel::draftEdited);
    connect(&m_fees, &FeeSelectionModel::policyChanged, this, &WalletSendModel::draftEdited);
    connect(&m_fees, &FeeSelectionModel::previewChanged, this, &WalletSendModel::changed);
    connect(&m_coins, &CoinSelectionModel::inputChanged, this, &WalletSendModel::draftEdited);
    connect(&m_review, &TransactionReviewModel::changed, this, &WalletSendModel::changed);
}

bool WalletSendModel::available() const
{
    return m_session.available() && !m_session.wallet().privateKeysDisabled() && !m_session.wallet().hasExternalSigner();
}

QString WalletSendModel::error() const
{
    if (!available()) return tr("This wallet is not available for local signing.");
    if (!m_error.isEmpty()) return m_error;
    if (!m_submission_status.isEmpty()) return {};
    return m_recipients.valid() ? m_fees.error() : m_recipients.error();
}

SendDraftSnapshot WalletSendModel::snapshot() const
{
    return {m_session.id(), m_session.generation(), m_revision, m_recipients.snapshot(), m_fees.policy(),
            m_session.available() ? m_session.wallet().getDefaultAddressType() : OutputType::UNKNOWN,
            m_coins.selected(), m_coins.manual()};
}

void WalletSendModel::draftEdited()
{
    ++m_revision;
    m_error.clear();
    invalidateReview();
    if (available()) m_fees.setDraft(snapshot());
    else m_fees.invalidate();
    Q_EMIT changed();
}

void WalletSendModel::discard() { m_coins.clear(); m_recipients.clear(); }

bool WalletSendModel::busy() const { return m_session.available() && (m_preparing || m_submitting || m_session.actionBusy()); }
bool WalletSendModel::canPrepare() const
{
    return available() && !busy() && m_recipients.valid() && m_fees.valid() && (!m_coins.manual() || m_coins.selectedCount() > 0);
}
bool WalletSendModel::needsPassphrase() const { return available() && m_session.wallet().isCrypted() && m_session.wallet().isLocked(); }

void WalletSendModel::invalidateReview()
{
    ++m_prepare_id;
    m_prepared.reset();
    m_review.clear();
    Q_EMIT changed();
}

void WalletSendModel::editDraft()
{
    if (busy()) return;
    m_submitted_id.clear();
    m_submission_status.clear();
    invalidateReview();
}

bool WalletSendModel::prepare(const QString& passphrase)
{
    if (!canPrepare()) return false;
    invalidateReview();
    m_error.clear();
    m_submitted_id.clear();
    m_submission_status.clear();
    m_preparing = true;
    const quint64 request_id = m_prepare_id;
    const auto draft = snapshot();
    const auto result = std::make_shared<PreparedSendResult>();
    const QPointer<WalletSendModel> self(this);
    const bool accepted = m_session.runAction([draft, secret = WalletPassphrase(passphrase), result](interfaces::Wallet& wallet) {
        *result = PrepareWalletSend(wallet, draft, secret);
        return WalletOperationResult{};
    }, [self, draft, request_id, result](WalletOperationResult operation) {
        if (!self) return;
        self->m_preparing = false;
        if (request_id == self->m_prepare_id && draft.revision == self->m_revision) {
            self->m_error = operation.code == WalletOperationResult::Success ? result->error : operation.error;
            if (result->review && operation.code == WalletOperationResult::Success) {
                self->m_prepared = Prepared{draft, *result->review};
                self->m_review.setSnapshot(*result->review);
            }
        }
        Q_EMIT self->changed();
    });
    if (!accepted) {
        m_preparing = false;
        m_error = tr("Another wallet action is in progress.");
    }
    Q_EMIT changed();
    return accepted;
}

bool WalletSendModel::canSubmit() const
{
    return available() && !busy() && m_prepared && m_review.hasReview() &&
        m_prepared->inputs.session_id == m_session.id() &&
        m_prepared->inputs.session_generation == m_session.generation() &&
        m_prepared->inputs.revision == m_revision;
}

bool WalletSendModel::submit()
{
    if (!canSubmit()) return false;
    const auto prepared = *m_prepared;
    m_submitting = true;
    invalidateReview(); // Consume the handle before another click can reach it.
    m_error.clear();
    m_submission_status = tr("Submitting transaction…");
    const auto result = std::make_shared<SendSubmissionResult>();
    const QPointer<WalletSendModel> self(this);
    const bool accepted = m_session.runAction([prepared, result](interfaces::Wallet& wallet) {
        try {
            *result = SubmitWalletSend(wallet, prepared.review, prepared.inputs.recipients);
        } catch (...) {
            // A failure after recording is not evidence of non-submission.
            // Never encourage a duplicate payment on an uncertain outcome.
            result->status_known = false;
            result->transaction_id = QString::fromStdString(prepared.review.transaction->GetHash().ToString());
            result->recorded = bool(wallet.getWalletTx(prepared.review.transaction->GetHash()).tx);
            result->error = tr("Submission status is unavailable. Check wallet activity before trying again.");
        }
        return WalletOperationResult{};
    }, [self, result](WalletOperationResult operation) {
        if (!self) return;
        self->m_submitting = false;
        if (result->recorded) {
            self->discard();
            self->m_submitted_id = result->transaction_id;
        }
        self->m_error = operation.code == WalletOperationResult::Success ? result->error :
            tr("Submission could not finish. Check wallet activity before trying again.");
        if (!result->status_known || operation.code != WalletOperationResult::Success) {
            self->m_submission_status = tr("Submission status is unavailable. Check wallet activity before creating another payment.");
        } else if (result->accepted) {
            self->m_submission_status = result->confirmed ? tr("The transaction is already confirmed.") :
                tr("Submitted to the local node. Awaiting confirmation.");
            Q_EMIT self->submitted();
        } else if (result->recorded) {
            self->m_submission_status = tr("Recorded in the wallet; not currently in the node mempool. Check activity before creating another payment.");
        } else {
            self->m_submission_status = tr("Transaction was not submitted. Check the error and prepare again.");
        }
        Q_EMIT self->changed();
    });
    if (!accepted) {
        m_submitting = false;
        m_submission_status.clear();
        m_error = tr("Another wallet action is in progress. Prepare again.");
    }
    Q_EMIT changed();
    return accepted;
}
