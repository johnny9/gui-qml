// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/sendrecipientslistmodel.h>
#include <key_io.h>
#include <policy/policy.h>
#include <script/solver.h>
#include <util/moneystr.h>

#include <set>

SendRecipientsListModel::SendRecipientsListModel(CFeeRate dust_relay_fee, QObject* parent)
    : QAbstractListModel(parent), m_dust_relay_fee(dust_relay_fee) {}

int SendRecipientsListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant SendRecipientsListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !hasRow(index.row())) return {};
    const auto& entry = m_entries[index.row()];
    switch (role) {
    case AddressRole: return entry.address;
    case AmountRole: return entry.amount;
    case LabelRole: return entry.label;
    case SubtractFeeRole: return entry.subtract_fee;
    case MaximumRole: return entry.maximum;
    case ErrorRole: return entryError(index.row());
    default: return {};
    }
}

QHash<int, QByteArray> SendRecipientsListModel::roleNames() const
{
    return {{AddressRole, "recipientAddress"}, {AmountRole, "recipientAmount"}, {LabelRole, "recipientLabel"},
            {SubtractFeeRole, "subtractFee"}, {MaximumRole, "maximum"}, {ErrorRole, "fieldError"}};
}

QString SendRecipientsListModel::entryError(int row) const
{
    const auto& entry = m_entries[row];
    const auto destination = DecodeDestination(entry.address.toStdString());
    if (!IsValidDestination(destination)) return tr("Enter a valid address for this network.");
    if (entry.maximum) return {};
    const auto amount = ParseMoney(entry.amount.toStdString());
    if (!amount || *amount <= 0 || !MoneyRange(*amount)) return tr("Enter a positive BTC amount with at most eight decimal places.");
    if (IsDust(CTxOut(*amount, GetScriptForDestination(destination)), m_dust_relay_fee)) return tr("The amount is below the dust threshold.");
    return {};
}

QString SendRecipientsListModel::error() const
{
    if (m_entries.empty()) return tr("Add a recipient.");
    std::set<std::string> destinations;
    CAmount total{0};
    int maximum_count{0};
    for (int row = 0; row < rowCount(); ++row) {
        const QString field_error = entryError(row);
        if (!field_error.isEmpty()) return field_error;
        const auto& entry = m_entries[row];
        if (!destinations.insert(EncodeDestination(DecodeDestination(entry.address.toStdString()))).second) return tr("Duplicate recipient address.");
        if (entry.maximum) {
            if (++maximum_count > 1) return tr("Only one recipient can use the remaining balance.");
        } else {
            const CAmount amount = *ParseMoney(entry.amount.toStdString());
            if (amount > MAX_MONEY - total) return tr("The total amount is out of range.");
            total += amount;
        }
    }
    return {};
}

std::vector<SendRecipientValue> SendRecipientsListModel::snapshot() const
{
    if (!valid()) return {};
    std::vector<SendRecipientValue> values;
    for (const auto& entry : m_entries) {
        values.push_back({QString::fromStdString(EncodeDestination(DecodeDestination(entry.address.toStdString()))),
                          entry.label, entry.maximum ? 0 : *ParseMoney(entry.amount.toStdString()),
                          entry.subtract_fee || entry.maximum, entry.maximum});
    }
    return values;
}

void SendRecipientsListModel::changed(int row)
{
    Q_EMIT dataChanged(index(row), index(row));
    Q_EMIT edited();
}

void SendRecipientsListModel::add()
{
    // Preserve the source form's recipient bound in the model as well.
    if (!canAdd()) return;
    beginInsertRows({}, rowCount(), rowCount());
    m_entries.emplace_back();
    endInsertRows();
    Q_EMIT edited();
}

void SendRecipientsListModel::remove(int row)
{
    if (!hasRow(row)) return;
    if (rowCount() == 1) { clear(); return; }
    beginRemoveRows({}, row, row);
    m_entries.erase(m_entries.begin() + row);
    endRemoveRows();
    Q_EMIT edited();
}

void SendRecipientsListModel::clear()
{
    beginResetModel();
    m_entries.assign(1, Entry{});
    endResetModel();
    Q_EMIT edited();
}

void SendRecipientsListModel::setAddress(int row, const QString& address)
{
    if (!hasRow(row) || m_entries[row].address == address.trimmed()) return;
    m_entries[row].address = address.trimmed();
    changed(row);
}

void SendRecipientsListModel::setAmount(int row, const QString& amount)
{
    if (!hasRow(row) || m_entries[row].amount == amount) return;
    m_entries[row].amount = amount;
    changed(row);
}

void SendRecipientsListModel::setLabel(int row, const QString& label)
{
    if (!hasRow(row) || m_entries[row].label == label) return;
    m_entries[row].label = label;
    changed(row);
}

void SendRecipientsListModel::setSubtractFee(int row, bool subtract)
{
    if (!hasRow(row) || m_entries[row].subtract_fee == subtract) return;
    m_entries[row].subtract_fee = subtract;
    changed(row);
}

void SendRecipientsListModel::setMaximum(int row, bool maximum)
{
    if (!hasRow(row) || m_entries[row].maximum == maximum) return;
    m_entries[row].maximum = maximum;
    changed(row);
}
