// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_TRANSACTIONHISTORYMODEL_H
#define BITCOIN_QML_WALLET_TRANSACTIONHISTORYMODEL_H
#include <qml/wallet/transactionrecord.h>
#include <QAbstractListModel>
#include <QVariantMap>
class WalletSession;

class TransactionHistoryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY recordsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY displayUnitChanged)
public:
    enum Role { KeyRole = Qt::UserRole + 1, TxidRole, OutputRole, KindRole, AddressRole, LabelRole,
                AmountRole, AmountDisplayRole, TimeRole, StatusRole, ConfirmationsRole };
    explicit TransactionHistoryModel(WalletSession& session, QObject* parent = nullptr);
    explicit TransactionHistoryModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_records.size(); }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    int displayUnit() const { return m_display_unit; }
    void setDisplayUnit(int unit);
    const QVector<TransactionRecord>& records() const { return m_records; }
    void setRecords(QVector<TransactionRecord> records);
    Q_INVOKABLE QVariantMap details(const QString& key) const;
    Q_INVOKABLE QString keyForTransaction(const QString& txid) const;
    Q_INVOKABLE void reload();
Q_SIGNALS:
    void recordsChanged();
    void loadingChanged();
    void errorChanged();
    void displayUnitChanged();
private:
    WalletSession* m_session{nullptr};
    QVector<TransactionRecord> m_records;
    int m_display_unit{0};
    bool m_loading{false}, m_reload_pending{false};
    QString m_error;
};
#endif // BITCOIN_QML_WALLET_TRANSACTIONHISTORYMODEL_H
