// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_ACTIVITYTIMELINEMODEL_H
#define BITCOIN_QML_WALLET_ACTIVITYTIMELINEMODEL_H
#include <QAbstractListModel>
#include <QVariantMap>
class TransactionHistoryModel;
class ReceiveRequestHistoryModel;

/** Read-only association by address, never invoice settlement or source mutation. */
class ActivityTimelineModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY recordsChanged)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY displayUnitChanged)
public:
    enum Role { KeyRole = Qt::UserRole + 1, TxidRole, KindRole, AddressRole, LabelRole,
                AmountRole, AmountDisplayRole, TimeRole, StatusRole, PendingRole,
                RequestedAmountRole, RequestIdsRole, MessageRole };
    ActivityTimelineModel(quint64 session_id, TransactionHistoryModel& transactions,
                          ReceiveRequestHistoryModel& requests, QObject* parent = nullptr);
    int count() const { return m_rows.size(); }
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : count(); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap details(const QString& key) const;
    const QVector<QVariantMap>& rows() const { return m_rows; }
    int displayUnit() const { return m_display_unit; }
    void setDisplayUnit(int unit);
Q_SIGNALS:
    void recordsChanged();
    void displayUnitChanged();
private:
    void rebuild();
    quint64 m_session_id;
    TransactionHistoryModel& m_transactions;
    ReceiveRequestHistoryModel& m_requests;
    QVector<QVariantMap> m_rows;
    int m_display_unit{0};
};
#endif // BITCOIN_QML_WALLET_ACTIVITYTIMELINEMODEL_H
