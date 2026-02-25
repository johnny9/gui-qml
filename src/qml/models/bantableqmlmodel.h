// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BANTABLEQMLMODEL_H
#define BITCOIN_QML_MODELS_BANTABLEQMLMODEL_H

#include <bitcoin/src/qt/bantablemodel.h>

#include <QAbstractListModel>
#include <QList>

namespace interfaces { class Node; }

class BanTableQmlModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum class BanRoles {
        AddressRole = Qt::UserRole,
        BanUntilRole
    };

    explicit BanTableQmlModel(interfaces::Node& node, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    int count() const { return m_ban_list.size(); }

    Q_INVOKABLE void unbanAt(int row);

public Q_SLOTS:
    void refresh();

Q_SIGNALS:
    void countChanged();

private:
    interfaces::Node& m_node;
    QList<CCombinedBan> m_ban_list;
};

#endif // BITCOIN_QML_MODELS_BANTABLEQMLMODEL_H
