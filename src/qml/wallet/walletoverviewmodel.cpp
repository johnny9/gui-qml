// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletoverviewmodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/bitcoinunits.h>
#include <interfaces/wallet.h>

#include <QCryptographicHash>
#include <QSettings>

WalletOverviewModel::WalletOverviewModel(WalletSession& session, const QString& network, QObject* parent)
    : QObject(parent), m_session(session), m_network(network)
{
    connect(&session, &WalletSession::changed, this, &WalletOverviewModel::changed);
    connect(&session, &WalletSession::invalidated, this, &WalletOverviewModel::changed);
}

QString WalletOverviewModel::name() const { return m_session.name(); }
QString WalletOverviewModel::aliasKey() const
{
    return QStringLiteral("walletAliases/%1/%2").arg(m_network,
        QString::fromLatin1(QCryptographicHash::hash(name().toUtf8(), QCryptographicHash::Sha256).toHex()));
}
QString WalletOverviewModel::displayName() const
{
    const QString alias = QSettings{}.value(aliasKey()).toString();
    return alias.isEmpty() ? (name().isEmpty() ? tr("Default wallet") : name()) : alias;
}
QString WalletOverviewModel::balance() const
{
    return available() ? QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, m_session.wallet().getBalances().balance) : QString{};
}
bool WalletOverviewModel::available() const { return m_session.available(); }
bool WalletOverviewModel::encrypted() const { return available() && m_session.wallet().isCrypted(); }
bool WalletOverviewModel::locked() const { return available() && m_session.wallet().isLocked(); }
bool WalletOverviewModel::localSigning() const
{
    return available() && !m_session.wallet().privateKeysDisabled() && !m_session.wallet().hasExternalSigner();
}
bool WalletOverviewModel::canReceive() const { return available() && m_session.wallet().canGetAddresses(); }
QString WalletOverviewModel::keyScheme() const
{
    if (!available()) return {};
    if (m_session.wallet().hasExternalSigner()) return tr("External signer");
    if (m_session.wallet().privateKeysDisabled()) return tr("Watch-only descriptors");
    return tr("Local-key descriptors");
}
