// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/addresslistmodel.h>
#include <qml/wallet/walletsession.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <wallet/types.h>
#include <QPointer>
#include <QTimer>

namespace {
QString ScriptType(const CTxDestination& destination)
{
    if (std::holds_alternative<PKHash>(destination)) return QStringLiteral("P2PKH");
    if (std::holds_alternative<ScriptHash>(destination)) return QStringLiteral("P2SH");
    if (std::holds_alternative<WitnessV0KeyHash>(destination)) return QStringLiteral("P2WPKH");
    if (std::holds_alternative<WitnessV0ScriptHash>(destination)) return QStringLiteral("P2WSH");
    if (std::holds_alternative<WitnessV1Taproot>(destination)) return QStringLiteral("P2TR");
    return QStringLiteral("Other");
}
}

AddressListModel::AddressListModel(WalletSession& session, QObject* parent)
    : QAbstractListModel(parent), m_session{session}
{
    connect(&session, &WalletSession::addressesChanged, this, &AddressListModel::reload);
    connect(&session, &WalletSession::activityChanged, this, &AddressListModel::reload);
    connect(&session, &WalletSession::actionBusyChanged, this, &AddressListModel::changed);
    connect(&session, &WalletSession::invalidated, this, [this] {
        m_loading = false;
        m_error = tr("The wallet is unavailable.");
        Q_EMIT changed();
    });
    QTimer::singleShot(0, this, &AddressListModel::reload);
}
bool AddressListModel::busy() const { return m_loading || m_session.actionBusy(); }
QHash<int, QByteArray> AddressListModel::roleNames() const
{
    return {{AddressRole, "address"}, {LabelRole, "label"}, {CategoryRole, "category"},
            {ScriptTypeRole, "scriptType"}, {UsedRole, "isUsed"}, {BalanceRole, "balanceSat"},
            {EditableRole, "canEditLabel"}, {SignableRole, "canSign"}, {RequestRole, "canCreateRequest"}};
}
QVariant AddressListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    return m_rows[index.row()].value(QString::fromUtf8(roleNames().value(role)));
}
QVariantMap AddressListModel::details(const QString& address) const
{
    for (const auto& row : m_rows) if (row.value("address").toString() == address) return row;
    return {};
}
void AddressListModel::reload()
{
    if (!m_session.available()) return;
    if (m_loading) { m_reload_pending = true; return; }
    m_loading = true;
    Q_EMIT changed();
    auto rows{std::make_shared<QVector<QVariantMap>>()};
    const QPointer<AddressListModel> self{this};
    if (!m_session.runRead([rows](interfaces::Wallet& wallet) {
        QMap<QString, QVariantMap> entries;
        for (const auto& address : wallet.getAddresses()) {
            const QString encoded{QString::fromStdString(EncodeDestination(address.dest))};
            if (encoded.isEmpty()) continue;
            entries.insert(encoded, {{"address", encoded}, {"label", QString::fromStdString(address.name)},
                {"category", address.is_mine ? QObject::tr("Receiving") : QObject::tr("Contact")},
                {"scriptType", ScriptType(address.dest)}, {"isUsed", false}, {"balanceSat", qlonglong{0}},
                {"canEditLabel", true}, {"canSign", address.is_mine && std::holds_alternative<PKHash>(address.dest) &&
                    !wallet.privateKeysDisabled() && !wallet.hasExternalSigner() && wallet.isSpendable(address.dest)},
                {"canCreateRequest", address.is_mine && address.purpose == wallet::AddressPurpose::RECEIVE}});
        }
        std::vector<COutPoint> outputs;
        for (const auto& tx : wallet.getWalletTxs()) {
            for (size_t index = 0; index < tx.tx->vout.size(); ++index) {
                if (index >= tx.txout_is_mine.size() || !tx.txout_is_mine[index]) continue;
                CTxDestination destination;
                if (!ExtractDestination(tx.tx->vout[index].scriptPubKey, destination)) continue;
                const QString address{QString::fromStdString(EncodeDestination(destination))};
                if (!entries.contains(address)) entries.insert(address, {{"address", address}, {"label", QString{}},
                    {"category", index < tx.txout_is_change.size() && tx.txout_is_change[index] ? QObject::tr("Change") : QObject::tr("Receiving")},
                    {"scriptType", ScriptType(destination)}, {"balanceSat", qlonglong{0}}, {"canEditLabel", false},
                    {"canSign", false}, {"canCreateRequest", false}});
                entries[address].insert("isUsed", true);
                outputs.emplace_back(tx.tx->GetHash(), index);
            }
        }
        for (const auto& coin : wallet.getCoins(outputs)) {
            if (coin.is_spent || coin.depth_in_main_chain < 0) continue;
            CTxDestination destination;
            if (!ExtractDestination(coin.txout.scriptPubKey, destination)) continue;
            auto entry{entries.find(QString::fromStdString(EncodeDestination(destination)))};
            if (entry != entries.end()) entry->insert("balanceSat", entry->value("balanceSat").toLongLong() + coin.txout.nValue);
        }
        for (const auto& entry : entries) rows->push_back(entry);
        return WalletOperationResult{};
    }, [self, rows](WalletOperationResult result) {
        if (!self) return;
        self->m_loading = false;
        self->m_read_error = result.error;
        if (result.code == WalletOperationResult::Success) {
            self->beginResetModel();
            self->m_rows = std::move(*rows);
            self->endResetModel();
        }
        Q_EMIT self->changed();
        if (self->m_reload_pending) { self->m_reload_pending = false; self->reload(); }
    })) { m_loading = false; Q_EMIT changed(); }
}
bool AddressListModel::setLabel(const QString& address, const QString& label)
{
    m_error.clear();
    if (!m_session.available() || !details(address).value("canEditLabel").toBool() || label.toUtf8().size() > 1024) {
        m_error = tr("This address label cannot be edited.");
        Q_EMIT changed();
        return false;
    }
    const QPointer<AddressListModel> self{this};
    const bool accepted{m_session.runAction([address, label](interfaces::Wallet& wallet) {
        const auto destination{DecodeDestination(address.toStdString())};
        if (!IsValidDestination(destination) || !wallet.getAddress(destination, nullptr, nullptr)) {
            return WalletOperationResult::failure(WalletOperationResult::Unavailable, QObject::tr("The address is no longer in this wallet."));
        }
        if (!wallet.setAddressBook(destination, label.toStdString(), std::nullopt)) {
            return WalletOperationResult::failure(WalletOperationResult::CoreError, QObject::tr("The label could not be saved."));
        }
        return WalletOperationResult{};
    }, [self](WalletOperationResult result) {
        if (!self) return;
        self->m_error = result.error;
        Q_EMIT self->changed();
    })};
    if (!accepted) { m_error = tr("The wallet is busy or unavailable."); Q_EMIT changed(); }
    return accepted;
}
