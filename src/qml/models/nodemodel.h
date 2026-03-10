// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_NODEMODEL_H
#define BITCOIN_QML_MODELS_NODEMODEL_H

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <clientversion.h>

#include <memory>

#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

const char DEFAULT_PROXY_HOST[] = "127.0.0.1";
constexpr uint16_t DEFAULT_PROXY_PORT = 9050;

QT_BEGIN_NAMESPACE
class QTimerEvent;
QT_END_NAMESPACE

namespace interfaces {
class Node;
}

/** Model for Bitcoin network client. */
class NodeModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int blockTipHeight READ blockTipHeight NOTIFY blockTipHeightChanged)
    Q_PROPERTY(QString fullClientVersion READ fullClientVersion CONSTANT)
    Q_PROPERTY(int numOutboundPeers READ numOutboundPeers NOTIFY numOutboundPeersChanged)
    Q_PROPERTY(int maxNumOutboundPeers READ maxNumOutboundPeers CONSTANT)
    Q_PROPERTY(int remainingSyncTime READ remainingSyncTime NOTIFY remainingSyncTimeChanged)
    Q_PROPERTY(double verificationProgress READ verificationProgress NOTIFY verificationProgressChanged)
    Q_PROPERTY(bool pause READ pause WRITE setPause NOTIFY pauseChanged)
    Q_PROPERTY(bool faulted READ errorState WRITE setErrorState NOTIFY errorStateChanged)
    // CONSTANT assumes LogInstance().m_file_path is fixed after startup.
    Q_PROPERTY(QString debugLogPath READ debugLogPath CONSTANT)
    Q_PROPERTY(QString debugLogOpenError READ debugLogOpenError NOTIFY debugLogOpenErrorChanged)
    Q_PROPERTY(QString formattedDebugLog READ formattedDebugLog NOTIFY formattedDebugLogChanged)
    Q_PROPERTY(int debugLogLineCount READ debugLogLineCount NOTIFY formattedDebugLogChanged)
    Q_PROPERTY(bool debugLogHasMoreLines READ debugLogHasMoreLines NOTIFY debugLogHasMoreLinesChanged)
    Q_PROPERTY(int debugLogLoadLimit READ debugLogLoadLimit WRITE setDebugLogLoadLimit NOTIFY debugLogLoadLimitChanged)
    Q_PROPERTY(QString debugLogFilter READ debugLogFilter WRITE setDebugLogFilter NOTIFY formattedDebugLogChanged)
    Q_PROPERTY(QString debugLogLineNumColor WRITE setDebugLogLineNumColor)
    Q_PROPERTY(QString debugLogMessageColor WRITE setDebugLogMessageColor)
    Q_PROPERTY(QString debugLogTimestampColor WRITE setDebugLogTimestampColor)

public:
    explicit NodeModel(interfaces::Node& node);

    int blockTipHeight() const { return m_block_tip_height; }
    void setBlockTipHeight(int new_height);
    QString fullClientVersion() const { return QString::fromStdString(FormatFullVersion()); }
    int numOutboundPeers() const { return m_num_outbound_peers; }
    void setNumOutboundPeers(int new_num);
    int maxNumOutboundPeers() const { return m_max_num_outbound_peers; }
    int remainingSyncTime() const { return m_remaining_sync_time; }
    void setRemainingSyncTime(double new_progress);
    double verificationProgress() const { return m_verification_progress; }
    void setVerificationProgress(double new_progress);
    bool pause() const { return m_pause; }
    void setPause(bool new_pause);
    bool errorState() const { return m_faulted; }
    void setErrorState(bool new_error);

    Q_INVOKABLE float getTotalBytesReceived() const { return (float)m_node.getTotalBytesRecv(); }
    Q_INVOKABLE float getTotalBytesSent() const { return (float)m_node.getTotalBytesSent(); }

    Q_INVOKABLE void startNodeInitializionThread();
    Q_INVOKABLE void requestShutdown();

    void startShutdownPolling();
    void stopShutdownPolling();

    QString debugLogPath() const;
    Q_INVOKABLE bool openDebugLogFile();
    QString debugLogOpenError() const { return m_debug_log_open_error; }
    Q_INVOKABLE QVariantList debugLogLines(int max_lines = 10000);

    QString formattedDebugLog() const { return m_formatted_debug_log; }
    int debugLogLineCount() const { return m_debug_log_line_count; }
    bool debugLogHasMoreLines() const { return m_has_more_lines; }
    int debugLogLoadLimit() const { return m_debug_log_load_limit; }
    QString debugLogFilter() const { return m_debug_log_filter; }
    void setDebugLogLoadLimit(int limit);
    void setDebugLogFilter(const QString& filter);
    void setDebugLogLineNumColor(const QString& color);
    void setDebugLogMessageColor(const QString& color);
    void setDebugLogTimestampColor(const QString& color);
    Q_INVOKABLE void refreshDebugLog(bool full_load = false);
    Q_INVOKABLE void updateDebugLogTimestamps();

    Q_INVOKABLE bool validateProxyAddress(QString addr_port);
    Q_INVOKABLE QString defaultProxyAddress();
    Q_INVOKABLE bool disconnectPeer(int nodeId);
    Q_INVOKABLE bool banPeer(const QString& rawAddress, int64_t banDuration);

public Q_SLOTS:
    void initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info);

Q_SIGNALS:
    void blockTipHeightChanged();
    void numOutboundPeersChanged();
    void remainingSyncTimeChanged();
    void requestedInitialize();
    void requestedShutdown();
    void verificationProgressChanged();
    void pauseChanged(bool new_pause);
    void errorStateChanged(bool new_error_state);

    void setTimeRatioList(int new_time);
    void setTimeRatioListInitial();
    void nodeInitialized();
    void bannedListChanged();
    void debugLogChanged();
    void debugLogOpenErrorChanged();
    void formattedDebugLogChanged();
    void debugLogHasMoreLinesChanged();
    void debugLogLoadLimitChanged();
    void newDebugLogLines(int count);

protected:
    void timerEvent(QTimerEvent* event) override;

private:
    // Properties that are exposed to QML.
    int m_block_tip_height{0};
    int m_num_outbound_peers{0};
    static constexpr int m_max_num_outbound_peers{MAX_OUTBOUND_FULL_RELAY_CONNECTIONS + MAX_BLOCK_RELAY_ONLY_CONNECTIONS};
    int m_remaining_sync_time{0};
    double m_verification_progress{0.0};
    bool m_pause{false};
    bool m_faulted{false};
    QString m_debug_log_open_error;
    QFileSystemWatcher m_log_watcher;

    QList<QVariantMap> m_all_lines;
    QString m_debug_log_filter;
    int m_debug_log_load_limit{1000};
    bool m_has_more_lines{false};
    int m_debug_log_line_count{0};
    QString m_debug_log_line_num_color;
    QString m_debug_log_message_color;
    QString m_debug_log_timestamp_color;
    QString m_formatted_debug_log;

    int m_shutdown_polling_timer_id{0};

    QVector<QPair<int, double>> m_block_process_time;

    interfaces::Node& m_node;
    std::unique_ptr<interfaces::Handler> m_handler_notify_block_tip;
    std::unique_ptr<interfaces::Handler> m_handler_notify_num_peers_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_banned_list_changed;

    void ConnectToBlockTipSignal();
    void ConnectToNumConnectionsChangedSignal();
    void ConnectToBannedListChangedSignal();
    void ConnectToDebugLogSignal();
    void buildFormattedDebugLog();
    QString relativeTimeLabel(qint64 timestamp_ms, qint64 now_ms) const;
};

#endif // BITCOIN_QML_MODELS_NODEMODEL_H
