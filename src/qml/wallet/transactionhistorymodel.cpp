// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/transactionhistorymodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/bitcoinunits.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <QPointer>
#include <QTimer>
#include <algorithm>

TransactionHistoryModel::TransactionHistoryModel(QObject* parent) : QAbstractListModel(parent) {}

TransactionHistoryModel::TransactionHistoryModel(WalletSession& session, QObject* parent)
    : QAbstractListModel(parent), m_session{&session}
{
    connect(&session, &WalletSession::activityChanged, this, &TransactionHistoryModel::reload);
    connect(&session, &WalletSession::addressesChanged, this, &TransactionHistoryModel::reload);
    connect(&session, &WalletSession::invalidated, this, [this] {
        m_loading = false;
        m_reload_pending = false;
        Q_EMIT loadingChanged();
    });
    QTimer::singleShot(0, this, &TransactionHistoryModel::reload);
}

int TransactionHistoryModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QVariant TransactionHistoryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const auto& row{m_records[index.row()]};
    switch (role) {
    case KeyRole: return row.key();
    case TxidRole: return row.txid;
    case OutputRole: return row.output_index;
    case KindRole: return static_cast<int>(row.kind);
    case AddressRole: return row.address;
    case LabelRole: return row.label;
    case AmountRole: return QVariant::fromValue<qlonglong>(row.netAmount());
    case AmountDisplayRole: return QmlBitcoinUnits::format(QmlBitcoinUnits::fromDisplayUnit(m_display_unit), row.netAmount(), true);
    case TimeRole: return row.time;
    case StatusRole: return row.status();
    case ConfirmationsRole: return row.confirmations;
    }
    return {};
}

QHash<int, QByteArray> TransactionHistoryModel::roleNames() const
{
    return {{KeyRole, "rowKey"}, {TxidRole, "txid"}, {OutputRole, "outputIndex"}, {KindRole, "kind"},
            {AddressRole, "address"}, {LabelRole, "label"}, {AmountRole, "amountSat"},
            {AmountDisplayRole, "amountDisplay"}, {TimeRole, "timestamp"}, {StatusRole, "status"},
            {ConfirmationsRole, "confirmations"}};
}

void TransactionHistoryModel::setDisplayUnit(int unit)
{
    if (unit == m_display_unit) return;
    m_display_unit = unit;
    if (count()) Q_EMIT dataChanged(index(0), index(count() - 1), {AmountDisplayRole});
    Q_EMIT displayUnitChanged();
}

void TransactionHistoryModel::setRecords(QVector<TransactionRecord> records)
{
    std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
        return a.time != b.time ? a.time > b.time : a.key() < b.key();
    });
    beginResetModel();
    m_records = std::move(records);
    endResetModel();
    Q_EMIT recordsChanged();
}

QVariantMap TransactionHistoryModel::details(const QString& key) const
{
    for (const auto& row : m_records) {
        if (row.key() != key) continue;
        return {{QStringLiteral("rowKey"), row.key()}, {QStringLiteral("txid"), row.txid},
                {QStringLiteral("address"), row.address}, {QStringLiteral("label"), row.label},
                {QStringLiteral("message"), row.message}, {QStringLiteral("confirmations"), row.confirmations},
                {QStringLiteral("status"), row.status()}, {QStringLiteral("outputIndex"), row.output_index},
                {QStringLiteral("creditSat"), QVariant::fromValue<qlonglong>(row.credit)},
                {QStringLiteral("debitSat"), QVariant::fromValue<qlonglong>(row.debit)},
                {QStringLiteral("feeSat"), QVariant::fromValue<qlonglong>(row.fee)}};
    }
    return {};
}

void TransactionHistoryModel::reload()
{
    if (!m_session || !m_session->available()) return;
    if (m_loading) { m_reload_pending = true; return; }
    m_loading = true;
    Q_EMIT loadingChanged();
    const quint64 session_id{m_session->id()};
    auto rows{std::make_shared<QVector<TransactionRecord>>()};
    const QPointer<TransactionHistoryModel> self{this};
    const bool accepted{m_session->runRead([rows, session_id](interfaces::Wallet& wallet) {
        for (const auto& entry : wallet.getWalletTxs()) {
            interfaces::WalletTxStatus status{};
            std::vector<std::string> messages, requests;
            bool in_mempool{false};
            int blocks{0};
            const auto tx{wallet.getWalletTxDetails(entry.tx->GetHash(), status, messages, requests, in_mempool, blocks)};
            for (auto row : DecomposeWalletTransaction(session_id, tx, status)) {
                std::string label;
                if (!row.address.isEmpty() && wallet.getAddress(DecodeDestination(row.address.toStdString()), &label, nullptr)) {
                    row.label = QString::fromStdString(label);
                }
                rows->push_back(std::move(row));
            }
        }
        return WalletOperationResult{};
    }, [self, rows](WalletOperationResult result) {
        if (!self) return;
        self->m_loading = false;
        self->m_error = result.error;
        if (result.code == WalletOperationResult::Success) self->setRecords(std::move(*rows));
        Q_EMIT self->loadingChanged();
        Q_EMIT self->errorChanged();
        if (self->m_reload_pending) {
            self->m_reload_pending = false;
            self->reload();
        }
    })};
    if (!accepted) { m_loading = false; Q_EMIT loadingChanged(); }
}
