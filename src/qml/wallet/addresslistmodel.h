// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_ADDRESSLISTMODEL_H
#define BITCOIN_QML_WALLET_ADDRESSLISTMODEL_H
#include <QAbstractListModel>
#include <QVariantMap>
class WalletSession;

class AddressListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    enum Role { AddressRole = Qt::UserRole + 1, LabelRole, CategoryRole, ScriptTypeRole,
                UsedRole, BalanceRole, EditableRole, SignableRole, RequestRole };
    explicit AddressListModel(WalletSession& session, QObject* parent = nullptr);
    int count() const { return m_rows.size(); }
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : count(); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool busy() const;
    QString error() const { return m_error.isEmpty() ? m_read_error : m_error; }
    Q_INVOKABLE QVariantMap details(const QString& address) const;
    Q_INVOKABLE void reload();
    Q_INVOKABLE bool setLabel(const QString& address, const QString& label);
Q_SIGNALS:
    void changed();
private:
    WalletSession& m_session;
    QVector<QVariantMap> m_rows;
    QString m_error, m_read_error;
    bool m_loading{false}, m_reload_pending{false};
};
#endif // BITCOIN_QML_WALLET_ADDRESSLISTMODEL_H
