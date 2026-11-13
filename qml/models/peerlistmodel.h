// Copyright (c) 2011-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PEERLISTMODEL_H
#define BITCOIN_QML_MODELS_PEERLISTMODEL_H

#include <qml/models/peerrecord.h>
#include <qml/nativelistmodel.h>
#include <util/time.h>
#include <map>
#include <set>

#include <QModelIndex>
#include <QStringList>
#include <QVariant>

namespace interfaces {
class Node;
}

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

class PeerListModel : public NativeListModel
{
    Q_OBJECT

public:
    explicit PeerListModel(interfaces::Node& node, QObject* parent, bool backend_ready = true,
                           execution::SnapshotServices services = {},
                           std::function<NodeClock::time_point()> clock = [] { return NodeClock::now(); });
    void backendInitialized();
    ~PeerListModel();

    Q_INVOKABLE
    void startAutoRefresh();
    Q_INVOKABLE
    void stopAutoRefresh();
    void beginShutdown();
    void closeAdmission() override;
    int rowForId(qint64 id) const { return indexForKey(QString::number(id)); }
    void watchDetails(qint64 id, PeerPresentationObserver* details);

    enum Role {
        StatsRole = Qt::UserRole,
        NetNodeId = Qt::UserRole + 1,
        Age,
        Address,
        Direction,
        ConnectionType,
        Network,
        Ping,
        Sent,
        Received,
        Subversion,
        Transport,
    };

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

public Q_SLOTS:
    void refresh();

protected:
    void rowUpdated(const QString& key, const NativeRowHandle& row) override;
    void rowRemoved(const QString& key) override;
private:
    void notifyDetails(qint64 id, bool removed, PeerPresentationObserver* after = nullptr);
    struct DetailRegistry;
    std::shared_ptr<DetailRegistry> m_details;
    QTimer* m_timer{nullptr};
    bool m_backend_ready;
    bool m_auto_refresh_requested{false};
};
#endif // BITCOIN_QML_MODELS_PEERLISTMODEL_H
