// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PEERDETAILSMODEL_H
#define BITCOIN_QML_MODELS_PEERDETAILSMODEL_H

#include <qml/peerstatsutil.h>

#include <QObject>
#include <qt/peertablemodel.h>
#include <util/time.h>

class PeerDetailsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int nodeId READ nodeId NOTIFY dataChanged)
    Q_PROPERTY(QString address READ address NOTIFY dataChanged)
    Q_PROPERTY(QString addressLocal READ addressLocal NOTIFY dataChanged)
    Q_PROPERTY(QString type READ type NOTIFY dataChanged)
    Q_PROPERTY(QString version READ version NOTIFY dataChanged)
    Q_PROPERTY(QString userAgent READ userAgent NOTIFY dataChanged)
    Q_PROPERTY(QString services READ services NOTIFY dataChanged)
    Q_PROPERTY(bool transactionRelay READ transactionRelay NOTIFY dataChanged)
    Q_PROPERTY(bool addressRelay READ addressRelay NOTIFY dataChanged)
    Q_PROPERTY(QString startingHeight READ startingHeight NOTIFY dataChanged)
    Q_PROPERTY(QString syncedHeaders READ syncedHeaders NOTIFY dataChanged)
    Q_PROPERTY(QString syncedBlocks READ syncedBlocks NOTIFY dataChanged)
    Q_PROPERTY(QString direction READ direction NOTIFY dataChanged)
    Q_PROPERTY(QString connectionDuration READ connectionDuration NOTIFY dataChanged)
    Q_PROPERTY(QString lastSend READ lastSend NOTIFY dataChanged)
    Q_PROPERTY(QString lastReceived READ lastReceived NOTIFY dataChanged)
    Q_PROPERTY(QString bytesSent READ bytesSent NOTIFY dataChanged)
    Q_PROPERTY(QString bytesReceived READ bytesReceived NOTIFY dataChanged)
    Q_PROPERTY(QString pingTime READ pingTime NOTIFY dataChanged)
    Q_PROPERTY(QString pingWait READ pingWait NOTIFY dataChanged)
    Q_PROPERTY(QString pingMin READ pingMin NOTIFY dataChanged)
    Q_PROPERTY(QString timeOffset READ timeOffset NOTIFY dataChanged)
    Q_PROPERTY(QString mappedAS READ mappedAS NOTIFY dataChanged)
    Q_PROPERTY(QString permission READ permission NOTIFY dataChanged)

public:
    explicit PeerDetailsModel(CNodeCombinedStats* nodeStats, PeerTableModel* model);

    int nodeId() const { return m_combinedStats->nodeStats.nodeid; }
    QString address() const { return QString::fromStdString(m_combinedStats->nodeStats.m_addr_name); }
    QString addressLocal() const { return QString::fromStdString(m_combinedStats->nodeStats.addrLocal); }
    QString type() const { return PeerStatsUtil::ConnectionTypeToQString(m_combinedStats->nodeStats.m_conn_type, /*prepend_direction=*/true); }
    QString version() const { return QString::number(m_combinedStats->nodeStats.nVersion); }
    QString userAgent() const { return QString::fromStdString(m_combinedStats->nodeStats.cleanSubVer); }
    QString services() const { return PeerStatsUtil::FormatServicesStr(m_combinedStats->nodeStateStats.their_services); }
    bool transactionRelay() const { return m_combinedStats->nodeStateStats.m_relay_txs; }
    bool addressRelay() const { return m_combinedStats->nodeStateStats.m_addr_relay_enabled; }
    QString startingHeight() const { return QString::number(m_combinedStats->nodeStateStats.m_starting_height); }
    QString syncedHeaders() const { return QString::number(m_combinedStats->nodeStateStats.nSyncHeight); }
    QString syncedBlocks() const { return QString::number(m_combinedStats->nodeStateStats.nCommonHeight); }
    QString direction() const { return QString::fromStdString(m_combinedStats->nodeStats.fInbound ? "Inbound" : "Outbound"); }
    QString connectionDuration() const { return PeerStatsUtil::FormatDurationStr(GetTime<std::chrono::seconds>() - m_combinedStats->nodeStats.m_connected); }
    QString lastSend() const { return PeerStatsUtil::FormatDurationStr(GetTime<std::chrono::seconds>() - m_combinedStats->nodeStats.m_last_send); }
    QString lastReceived() const { return PeerStatsUtil::FormatDurationStr(GetTime<std::chrono::seconds>() - m_combinedStats->nodeStats.m_last_recv); }
    QString bytesSent() const { return PeerStatsUtil::FormatBytes(m_combinedStats->nodeStats.nSendBytes); }
    QString bytesReceived() const { return PeerStatsUtil::FormatBytes(m_combinedStats->nodeStats.nRecvBytes); }
    QString pingTime() const { return PeerStatsUtil::FormatPingTime(m_combinedStats->nodeStats.m_last_ping_time); }
    QString pingMin() const { return PeerStatsUtil::FormatPingTime(m_combinedStats->nodeStats.m_min_ping_time); }
    QString pingWait() const { return PeerStatsUtil::FormatPingTime(m_combinedStats->nodeStateStats.m_ping_wait); }
    QString timeOffset() const { return PeerStatsUtil::FormatTimeOffset(Ticks<std::chrono::seconds>(m_combinedStats->nodeStateStats.time_offset)); }
    QString mappedAS() const { return m_combinedStats->nodeStats.m_mapped_as != 0 ? QString::number(m_combinedStats->nodeStats.m_mapped_as) : tr("N/A"); }
    QString permission() const {
        if (m_combinedStats->nodeStats.m_permission_flags == NetPermissionFlags::None) {
            return tr("N/A");
        }
        QStringList permissions;
        for (const auto& permission : NetPermissions::ToStrings(m_combinedStats->nodeStats.m_permission_flags)) {
            permissions.append(QString::fromStdString(permission));
        }
        return permissions.join(" & ");
    }

Q_SIGNALS:
    void dataChanged();
    void disconnected();

private Q_SLOTS:
    void onModelRowsRemoved(const QModelIndex& parent, int first, int last);
    void onModelDataChanged(const QModelIndex& top_left, const QModelIndex& bottom_right);

private:
    int m_row;
    CNodeCombinedStats* m_combinedStats;
    PeerTableModel* m_model;
    bool m_disconnected;
};

#endif // BITCOIN_QML_MODELS_PEERDETAILSMODEL_H
