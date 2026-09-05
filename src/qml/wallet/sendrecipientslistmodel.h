// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_SENDRECIPIENTSLISTMODEL_H
#define BITCOIN_QML_WALLET_SENDRECIPIENTSLISTMODEL_H

#include <qml/wallet/senddraftsnapshot.h>
#include <policy/feerate.h>
#include <QAbstractListModel>

class SendRecipientsListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY edited)
    Q_PROPERTY(bool canAdd READ canAdd NOTIFY edited)
    Q_PROPERTY(bool valid READ valid NOTIFY edited)
    Q_PROPERTY(QString error READ error NOTIFY edited)
public:
    enum Role { AddressRole = Qt::UserRole + 1, AmountRole, LabelRole, SubtractFeeRole, MaximumRole, ErrorRole };
    explicit SendRecipientsListModel(CFeeRate dust_relay_fee, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    bool canAdd() const { return rowCount() < 25; }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool valid() const { return error().isEmpty(); }
    QString error() const;
    std::vector<SendRecipientValue> snapshot() const;
    Q_INVOKABLE void add();
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void setAddress(int row, const QString& address);
    Q_INVOKABLE void setAmount(int row, const QString& amount);
    Q_INVOKABLE void setLabel(int row, const QString& label);
    Q_INVOKABLE void setSubtractFee(int row, bool subtract);
    Q_INVOKABLE void setMaximum(int row, bool maximum);
    void clear();
Q_SIGNALS:
    void edited();
private:
    struct Entry {
        QString address;
        QString amount;
        QString label;
        bool subtract_fee{false};
        bool maximum{false};
    };
    QString entryError(int row) const;
    void changed(int row);
    bool hasRow(int row) const { return row >= 0 && row < int(m_entries.size()); }
    std::vector<Entry> m_entries{Entry{}};
    const CFeeRate m_dust_relay_fee;
};

#endif // BITCOIN_QML_WALLET_SENDRECIPIENTSLISTMODEL_H
