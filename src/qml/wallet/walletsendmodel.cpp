// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletsendmodel.h>
#include <qml/wallet/walletsession.h>
#include <interfaces/wallet.h>

WalletSendModel::WalletSendModel(WalletSession& session, CFeeRate dust_relay_fee, QObject* parent)
    : QObject(parent), m_session(session), m_recipients(dust_relay_fee, this)
{
    connect(&m_recipients, &SendRecipientsListModel::edited, this, &WalletSendModel::draftEdited);
    connect(&session, &WalletSession::invalidated, this, &WalletSendModel::draftEdited);
    connect(&session, &WalletSession::changed, this, &WalletSendModel::changed);
}

bool WalletSendModel::available() const
{
    return m_session.available() && !m_session.wallet().privateKeysDisabled() && !m_session.wallet().hasExternalSigner();
}

QString WalletSendModel::error() const
{
    return available() ? m_recipients.error() : tr("This wallet is not available for local signing.");
}

SendDraftSnapshot WalletSendModel::snapshot() const
{
    return {m_session.id(), m_session.generation(), m_revision, m_recipients.snapshot()};
}

void WalletSendModel::draftEdited()
{
    ++m_revision;
    Q_EMIT changed();
}

void WalletSendModel::discard() { m_recipients.clear(); }
