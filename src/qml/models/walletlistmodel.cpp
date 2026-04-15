// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletlistmodel.h>

#include <interfaces/node.h>

#include <QHash>

WalletListModel::WalletListModel(interfaces::Node& node, QObject *parent)
: QAbstractListModel(parent)
, m_node(node)
{
}

void WalletListModel::listWalletDir()
{
    QHash<QString, int> existing_rows;
    for (int i = 0; i < rowCount(); ++i) {
        QModelIndex index = this->index(i, 0);
        QString name = data(index, NameRole).toString();
        existing_rows.insert(name, i);
    }

    for (const auto& [path, format] : m_node.walletLoader().listWalletDir()) {
        QString qname = QString::fromStdString(path);
        QString qformat = QString::fromStdString(format);
        if (existing_rows.contains(qname)) {
            const int row = existing_rows.value(qname);
            const bool format_changed = m_items[row].format != qformat;
            m_items[row].format = qformat;
            m_items[row].from_wallet_dir = true;
            if (format_changed) {
                Q_EMIT dataChanged(index(row, 0), index(row, 0), {FormatRole});
            }
        } else {
            addItem({qname, qformat, true});
        }
    }
    m_wallet_dir_loaded = true;
    for (const QString& wallet_name : m_open_wallet_names) {
        if (rowForName(wallet_name) == -1) {
            addItem({wallet_name, QString(), false});
        }
    }
    Q_EMIT walletListChanged(rowCount() > 0);
}

void WalletListModel::setWalletLoadState(const QString& wallet_name, bool loaded)
{
    if (wallet_name.isEmpty()) {
        return;
    }

    const bool was_loaded = m_open_wallet_names.contains(wallet_name);
    if (loaded) {
        m_open_wallet_names.insert(wallet_name);
    } else {
        m_open_wallet_names.remove(wallet_name);
    }

    const int row = rowForName(wallet_name);
    if (row == -1) {
        if (loaded && m_wallet_dir_loaded) {
            addItem({wallet_name, QString(), false});
        }
        return;
    }

    if (!loaded && !m_items[row].from_wallet_dir) {
        beginRemoveRows(QModelIndex(), row, row);
        m_items.removeAt(row);
        endRemoveRows();
        Q_EMIT walletListChanged(rowCount() > 0);
        return;
    }

    if (was_loaded != loaded) {
        Q_EMIT dataChanged(index(row, 0), index(row, 0), {LoadStateRole});
    }
}

int WalletListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_items.size();
}

QVariant WalletListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    const auto &item = m_items[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return item.name;
    case FormatRole:
        return item.format;
    case LoadStateRole:
        return m_open_wallet_names.contains(item.name)
            ? static_cast<int>(LoadState::Open)
            : static_cast<int>(LoadState::Closed);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> WalletListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[FormatRole] = "format";
    roles[LoadStateRole] = "loadState";
    return roles;
}

void WalletListModel::addItem(const Item &item)
{
    beginInsertRows(QModelIndex(), rowCount(), rowCount());
    m_items.append(item);
    endInsertRows();
    Q_EMIT walletListChanged(true);
}

int WalletListModel::rowForName(const QString& name) const
{
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items[row].name == name) {
            return row;
        }
    }
    return -1;
}
