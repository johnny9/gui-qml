// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/addresslistmodel.h>

#include <addresstype.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <qml/bitcoinunits.h>
#include <qml/models/bitcoinaddress.h>
#include <qml/models/walletqmlmodel.h>
#include <wallet/types.h>

#include <QVariantMap>
#include <algorithm>
#include <set>
#include <utility>

using wallet::AddressPurpose;

namespace {
QString CategoryName(AddressListModel::Category category)
{
    switch (category) {
    case AddressListModel::SingleUse: return QStringLiteral("single-use");
    case AddressListModel::Change: return QStringLiteral("change");
    }
    return {};
}

QString ScriptTypeName(const CTxDestination& destination)
{
    if (std::get_if<PKHash>(&destination)) return QStringLiteral("P2PKH");
    if (std::get_if<ScriptHash>(&destination)) return QStringLiteral("P2SH");
    if (std::get_if<WitnessV0KeyHash>(&destination)) return QStringLiteral("P2WPKH");
    if (std::get_if<WitnessV0ScriptHash>(&destination)) return QStringLiteral("P2WSH");
    if (std::get_if<WitnessV1Taproot>(&destination)) return QStringLiteral("P2TR");
    if (std::get_if<PayToAnchor>(&destination)) return QStringLiteral("P2A");
    if (std::get_if<WitnessUnknown>(&destination)) return QStringLiteral("Witness");
    if (std::get_if<PubKeyDestination>(&destination)) return QStringLiteral("P2PK");
    return {};
}

QString DisplayAmount(CAmount amount, int display_unit)
{
    const QmlBitcoinUnits::Unit unit{QmlBitcoinUnits::fromDisplayUnit(display_unit)};
    QString value{QmlBitcoinUnits::format(unit, amount)};
    if (value.contains(QLatin1Char('.'))) {
        while (value.endsWith(QLatin1Char('0'))) value.chop(1);
        if (value.endsWith(QLatin1Char('.'))) value.chop(1);
    }

    const QString label{QmlBitcoinUnits::displayLabel(unit, amount)};
    if (unit == QmlBitcoinUnits::Unit::BTC) {
        return label + QStringLiteral(" ") + value;
    }
    return value + QStringLiteral(" ") + label;
}
} // namespace

AddressListModel::AddressListModel(WalletQmlModel* parent)
    : QAbstractListModel(parent)
    , m_wallet_model(parent)
{
    if (m_wallet_model) {
        connect(m_wallet_model, &WalletQmlModel::walletStateChanged, this, &AddressListModel::refresh);
        connect(m_wallet_model, &WalletQmlModel::receiveOperationFinished, this, [this](const QString& operation, bool success) {
            if (operation != QStringLiteral("label") || m_pending_label_address.isEmpty()) return;
            const auto address = std::exchange(m_pending_label_address, {});
            const auto label = std::exchange(m_pending_label, {});
            Q_EMIT labelChangePendingChanged();
            Q_EMIT labelChangeFinished(address, label, success, m_wallet_model->receiveOperationError());
        });
    }
    refresh();
}

int AddressListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(m_entries.size());
}

int AddressListModel::count() const
{
    return static_cast<int>(m_entries.size());
}

QVariant AddressListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_entries.size())) {
        return {};
    }

    const AddressEntry& entry{m_entries.at(index.row())};
    switch (role) {
    case AddressRole:
        return entry.address;
    case FormattedAddressRole:
        return BitcoinAddress::formattedAddress(entry.address);
    case EllipsesAddressRole:
        return BitcoinAddress::ellipsesAddress(entry.address);
    case LabelRole:
        return entry.label;
    case CategoryRole:
        return CategoryName(entry.category);
    case UsedRole:
        return entry.used;
    case CurrentBalanceRole:
        return QmlBitcoinUnits::format(QmlBitcoinUnits::fromDisplayUnit(m_display_unit), entry.current_balance);
    case DisplayAmountRole:
        return DisplayAmount(entry.current_balance, m_display_unit);
    case HasAmountRole:
        return entry.current_balance != 0;
    case ScriptTypeRole:
        return entry.script_type;
    case CanEditLabelRole:
        return entry.can_edit_label;
    case CanCreatePaymentRequestRole:
        return entry.can_create_payment_request;
    }
    return {};
}

QHash<int, QByteArray> AddressListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[AddressRole] = "address";
    roles[FormattedAddressRole] = "formattedAddress";
    roles[EllipsesAddressRole] = "ellipsesAddress";
    roles[LabelRole] = "label";
    roles[CategoryRole] = "category";
    roles[UsedRole] = "isUsed";
    roles[CurrentBalanceRole] = "currentBalance";
    roles[DisplayAmountRole] = "displayAmount";
    roles[HasAmountRole] = "hasAmount";
    roles[ScriptTypeRole] = "scriptType";
    roles[CanEditLabelRole] = "canEditLabel";
    roles[CanCreatePaymentRequestRole] = "canCreatePaymentRequest";
    return roles;
}

AddressListModel::Category AddressListModel::category() const
{
    return m_category;
}

void AddressListModel::setCategory(Category category)
{
    if (m_category == category) return;
    m_category = category;
    Q_EMIT categoryChanged();
    rebuild();
}

QVariantList AddressListModel::categoryOptions() const
{
    return {
        QVariantMap{{QStringLiteral("value"), static_cast<int>(SingleUse)}, {QStringLiteral("text"), tr("Receive")}},
        QVariantMap{{QStringLiteral("value"), static_cast<int>(Change)}, {QStringLiteral("text"), tr("Change")}},
    };
}

bool AddressListModel::showUsed() const
{
    return m_show_used;
}

void AddressListModel::setShowUsed(bool show_used)
{
    if (m_show_used == show_used) return;
    m_show_used = show_used;
    Q_EMIT showUsedChanged();
    rebuild();
}

void AddressListModel::refresh()
{
    rebuild();
}

void AddressListModel::setDisplayUnit(int unit)
{
    if (unit == m_display_unit) return;
    m_display_unit = unit;
    if (!m_entries.empty()) {
        Q_EMIT dataChanged(index(0), index(rowCount() - 1), {CurrentBalanceRole, DisplayAmountRole});
    }
}

bool AddressListModel::setAddressLabel(const QString& address, const QString& label)
{
    if (!m_wallet_model || address.isEmpty() || labelChangePending()) return false;
    if (!m_wallet_model->setAddressLabel(address, label)) return false;
    m_pending_label_address = address;
    m_pending_label = label;
    Q_EMIT labelChangePendingChanged();
    return true;
}

QString AddressListModel::addressAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_entries.size())) return {};
    return m_entries.at(row).address;
}

void AddressListModel::rebuild()
{
    auto entries = collectEntries();
    const int old_count = count();
    std::set<QString> addresses;
    for (const auto& entry : entries) addresses.insert(entry.address);
    for (int row = old_count - 1; row >= 0; --row) {
        if (addresses.contains(m_entries[row].address)) continue;
        beginRemoveRows({}, row, row);
        m_entries.erase(m_entries.begin() + row);
        endRemoveRows();
    }
    for (int row = 0; row < static_cast<int>(entries.size()); ++row) {
        auto existing = std::find_if(m_entries.begin() + row, m_entries.end(), [&](const AddressEntry& entry) {
            return entry.address == entries[row].address;
        });
        if (existing == m_entries.end()) {
            beginInsertRows({}, row, row);
            m_entries.insert(m_entries.begin() + row, std::move(entries[row]));
            endInsertRows();
            continue;
        }
        const int source = existing - m_entries.begin();
        if (source != row) {
            beginMoveRows({}, source, source, {}, row);
            std::rotate(m_entries.begin() + row, existing, existing + 1);
            endMoveRows();
        }
        if (m_entries[row] != entries[row]) {
            m_entries[row] = std::move(entries[row]);
            Q_EMIT dataChanged(index(row), index(row));
        }
    }
    if (old_count != count()) Q_EMIT countChanged();
}

std::vector<AddressListModel::AddressEntry> AddressListModel::collectEntries() const
{
    std::vector<AddressEntry> entries;
    if (!m_wallet_model) return entries;

    const auto balances{m_wallet_model->addressBalances()};
    const auto used_addresses{m_wallet_model->usedAddresses()};

    if (m_category == Change) {
        const auto change_addresses{m_wallet_model->changeAddresses()};
        entries.reserve(change_addresses.size());
        for (const QString& address : change_addresses) {
            AddressEntry entry;
            entry.address = address;
            entry.label = m_wallet_model->getAddressLabel(address);
            entry.category = Change;
            entry.used = used_addresses.count(address) > 0;
            entry.script_type = ScriptTypeName(DecodeDestination(address.toStdString()));
            const auto balance_it{balances.find(address)};
            entry.current_balance = balance_it == balances.end() ? 0 : balance_it->second;
            entry.can_edit_label = false;
            entry.can_create_payment_request = false;
            entries.push_back(entry);
        }
        return entries;
    }

    for (const interfaces::WalletAddress& wallet_address : m_wallet_model->getAddresses()) {
        if (wallet_address.purpose != AddressPurpose::RECEIVE || !wallet_address.is_mine) {
            continue;
        }

        const QString address{QString::fromStdString(EncodeDestination(wallet_address.dest))};
        if (address.isEmpty()) continue;

        const bool used{used_addresses.count(address) > 0};
        if (used && !m_show_used) {
            continue;
        }

        AddressEntry entry;
        entry.address = address;
        entry.label = QString::fromStdString(wallet_address.name);
        entry.category = SingleUse;
        entry.used = used;
        entry.script_type = ScriptTypeName(wallet_address.dest);
        const auto balance_it{balances.find(address)};
        entry.current_balance = balance_it == balances.end() ? 0 : balance_it->second;
        entry.can_edit_label = true;
        entry.can_create_payment_request = true;
        entries.push_back(entry);
    }

    return entries;
}
