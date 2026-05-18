// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletlistmodel.h>

#include <interfaces/node.h>

#include <QSettings>

namespace {
QString WalletDisplayName(const QString& path)
{
    const QString trimmed_path = path.trimmed();
    if (trimmed_path.isEmpty()) {
        return {};
    }

    QSettings settings;
    const QString display_name = settings.value(QStringLiteral("walletDisplayNames/%1").arg(trimmed_path)).toString().trimmed();
    return display_name.isEmpty() ? trimmed_path : display_name;
}
} // namespace

#include <algorithm>

WalletListModel::WalletListModel(interfaces::Node& node, QObject *parent)
: QAbstractListModel(parent)
, m_node(node)
{
}

void WalletListModel::listWalletDir()
{
    QList<Item> updated_items;
    for (const auto& [path, format] : m_node.walletLoader().listWalletDir()) {
        updated_items.append({
            QString::fromStdString(path),
            QString::fromStdString(format),
            true,
        });
    }

    for (const QString& wallet_name : m_open_wallet_names) {
        const bool has_wallet = std::any_of(updated_items.cbegin(), updated_items.cend(), [&](const Item& item) {
            return item.name == wallet_name;
        });
        if (!has_wallet) {
            updated_items.append({wallet_name, QString(), false});
        }
    }

    sortItems(updated_items);
    applyUpdatedItems(std::move(updated_items));
    m_wallet_dir_loaded = true;
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

    QList<Item> updated_items{m_items};
    if (loaded && m_wallet_dir_loaded && rowForName(wallet_name) == -1) {
        updated_items.append({wallet_name, QString(), false});
    } else if (!loaded) {
        updated_items.erase(std::remove_if(updated_items.begin(), updated_items.end(), [&](const Item& item) {
            return item.name == wallet_name && !item.from_wallet_dir;
        }), updated_items.end());
    }

    const bool wallet_count_changed{updated_items.size() != m_items.size()};
    sortItems(updated_items);
    if (applyUpdatedItems(std::move(updated_items))) {
        if (wallet_count_changed) {
            Q_EMIT walletListChanged(rowCount() > 0);
        }
        return;
    }

    if (was_loaded != loaded) {
        updateLoadStateForAllRows();
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
    case DisplayNameRole:
        return WalletDisplayName(item.name);
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
    roles[DisplayNameRole] = "displayName";
    roles[LoadStateRole] = "loadState";
    return roles;
}

bool WalletListModel::itemLess(const Item& a, const Item& b) const
{
    const bool a_open{m_open_wallet_names.contains(a.name)};
    const bool b_open{m_open_wallet_names.contains(b.name)};
    if (a_open != b_open) return a_open;

    const int name_compare = QString::compare(a.name, b.name, Qt::CaseInsensitive);
    if (name_compare != 0) return name_compare < 0;

    const int case_compare = QString::compare(a.name, b.name, Qt::CaseSensitive);
    if (case_compare != 0) return case_compare < 0;

    const int format_compare = QString::compare(a.format, b.format, Qt::CaseInsensitive);
    if (format_compare != 0) return format_compare < 0;

    return QString::compare(a.format, b.format, Qt::CaseSensitive) < 0;
}

void WalletListModel::sortItems(QList<Item>& items) const
{
    std::stable_sort(items.begin(), items.end(), [this](const Item& a, const Item& b) {
        return itemLess(a, b);
    });
}

bool WalletListModel::applyUpdatedItems(QList<Item>&& updated_items)
{
    bool unchanged{m_items.size() == updated_items.size()};
    for (qsizetype i = 0; unchanged && i < m_items.size(); ++i) {
        unchanged = m_items[i].name == updated_items[i].name &&
            m_items[i].format == updated_items[i].format &&
            m_items[i].from_wallet_dir == updated_items[i].from_wallet_dir;
    }
    if (unchanged) {
        return false;
    }

    beginResetModel();
    m_items = std::move(updated_items);
    endResetModel();
    return true;
}

void WalletListModel::updateLoadStateForAllRows()
{
    if (m_items.isEmpty()) {
        return;
    }

    const QModelIndex first = index(0, 0);
    const QModelIndex last = index(rowCount() - 1, 0);
    Q_EMIT dataChanged(first, last, {LoadStateRole});
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

void WalletListModel::refreshDisplayNames()
{
    if (m_items.isEmpty()) {
        return;
    }

    const QModelIndex first = index(0, 0);
    const QModelIndex last = index(rowCount() - 1, 0);
    Q_EMIT dataChanged(first, last, {Qt::DisplayRole, DisplayNameRole});
}
