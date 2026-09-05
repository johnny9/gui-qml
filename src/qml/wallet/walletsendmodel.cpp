// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletsendmodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/sendpreview.h>
#include <interfaces/wallet.h>

WalletSendModel::WalletSendModel(WalletSession& session, CFeeRate dust_relay_fee, QObject* parent)
    : QObject(parent), m_session(session), m_recipients(dust_relay_fee, this),
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
      }, this)
{
    connect(&m_recipients, &SendRecipientsListModel::edited, this, &WalletSendModel::draftEdited);
    connect(&session, &WalletSession::invalidated, this, &WalletSendModel::draftEdited);
    connect(&session, &WalletSession::changed, this, &WalletSendModel::changed);
    connect(&session, &WalletSession::activityChanged, this, &WalletSendModel::draftEdited);
    connect(&m_fees, &FeeSelectionModel::policyChanged, this, &WalletSendModel::draftEdited);
    connect(&m_fees, &FeeSelectionModel::previewChanged, this, &WalletSendModel::changed);
}

bool WalletSendModel::available() const
{
    return m_session.available() && !m_session.wallet().privateKeysDisabled() && !m_session.wallet().hasExternalSigner();
}

QString WalletSendModel::error() const
{
    if (!available()) return tr("This wallet is not available for local signing.");
    return m_recipients.valid() ? m_fees.error() : m_recipients.error();
}

SendDraftSnapshot WalletSendModel::snapshot() const
{
    return {m_session.id(), m_session.generation(), m_revision, m_recipients.snapshot(), m_fees.policy(),
            m_session.available() ? m_session.wallet().getDefaultAddressType() : OutputType::UNKNOWN};
}

void WalletSendModel::draftEdited()
{
    ++m_revision;
    if (available()) m_fees.setDraft(snapshot());
    else m_fees.invalidate();
    Q_EMIT changed();
}

void WalletSendModel::discard() { m_recipients.clear(); }
