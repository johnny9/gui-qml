// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETLISTMODEL_H
#define BITCOIN_QML_WALLET_WALLETLISTMODEL_H

#include <QAbstractListModel>
#include <QString>
#include <vector>

class WalletListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    struct Row { QString name; QString format; bool loaded; QString display_name; };
    enum Role { NameRole = Qt::UserRole + 1, FormatRole, LoadedRole, DisplayNameRole };
    explicit WalletListModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
        const auto& row = m_rows.at(index.row());
        switch (role) {
        case NameRole: return row.name;
        case FormatRole: return row.format;
        case LoadedRole: return row.loaded;
        case DisplayNameRole: return row.display_name;
        default: return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{NameRole, "walletName"}, {FormatRole, "walletFormat"}, {LoadedRole, "loaded"}, {DisplayNameRole, "displayName"}};
    }
    void replace(std::vector<Row> rows) { beginResetModel(); m_rows = std::move(rows); endResetModel(); }
private:
    std::vector<Row> m_rows;
};

#endif // BITCOIN_QML_WALLET_WALLETLISTMODEL_H
