// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/nodemodel.h>

#include <interfaces/node.h>
#include <logging.h>
#include <net.h>
#include <net_processing.h>
#include <netbase.h>
#include <node/interface_ui.h>
#include <util/fs.h>
#include <validation.h>

#include <cassert>
#include <chrono>

#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QMetaObject>
#include <QRegularExpression>
#include <QString>
#include <QTextStream>
#include <QTimerEvent>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

NodeModel::NodeModel(interfaces::Node& node)
    : m_node{node}
{
    ConnectToBlockTipSignal();
    ConnectToNumConnectionsChangedSignal();
    ConnectToBannedListChangedSignal();
    ConnectToDebugLogSignal();
}

void NodeModel::setBlockTipHeight(int new_height)
{
    if (new_height != m_block_tip_height) {
        m_block_tip_height = new_height;
        Q_EMIT blockTipHeightChanged();
    }
}

void NodeModel::setNumOutboundPeers(int new_num)
{
    if (new_num != m_num_outbound_peers) {
        m_num_outbound_peers = new_num;
        Q_EMIT numOutboundPeersChanged();
    }
}

void NodeModel::setRemainingSyncTime(double new_progress)
{
    int currentTime = QDateTime::currentDateTime().toMSecsSinceEpoch();

    // keep a vector of samples of verification progress at height
    m_block_process_time.push_front(qMakePair(currentTime, new_progress));

    // show progress speed if we have more than one sample
    if (m_block_process_time.size() >= 2) {
        double progressDelta = 0;
        int timeDelta = 0;
        int remainingMSecs = 0;
        double remainingProgress = 1.0 - new_progress;
        for (int i = 1; i < m_block_process_time.size(); i++) {
            QPair<int, double> sample = m_block_process_time[i];

            // take first sample after 500 seconds or last available one
            if (sample.first < (currentTime - 500 * 1000) || i == m_block_process_time.size() - 1) {
                progressDelta = m_block_process_time[0].second - sample.second;
                timeDelta = m_block_process_time[0].first - sample.first;
                remainingMSecs = (progressDelta > 0) ? remainingProgress / progressDelta * timeDelta : -1;
                break;
            }
        }
        if (remainingMSecs > 0 && m_block_process_time.count() % 1000 == 0) {
            m_remaining_sync_time = remainingMSecs;

            Q_EMIT remainingSyncTimeChanged();
        }
        static const int MAX_SAMPLES = 5000;
        if (m_block_process_time.count() > MAX_SAMPLES) {
            m_block_process_time.remove(1, m_block_process_time.count() - 1);
        }
    }
}
void NodeModel::setVerificationProgress(double new_progress)
{
    if (new_progress != m_verification_progress) {
        setRemainingSyncTime(new_progress);

        m_verification_progress = new_progress;
        Q_EMIT verificationProgressChanged();
    }
}

void NodeModel::setPause(bool new_pause)
{
    if(m_pause != new_pause) {
        m_pause = new_pause;
        m_node.setNetworkActive(!new_pause);
        Q_EMIT pauseChanged(new_pause);
    }
}

void NodeModel::setErrorState(bool faulted)
{
    if (m_faulted != faulted) {
        m_faulted = faulted;
        Q_EMIT errorStateChanged(faulted);
    }
}

void NodeModel::startNodeInitializionThread()
{
    Q_EMIT requestedInitialize();
}

void NodeModel::requestShutdown()
{
    Q_EMIT requestedShutdown();
}

void NodeModel::initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info)
{
    if (!success) {
        setErrorState(true);
    } else {
        setBlockTipHeight(tip_info.block_height);
        setVerificationProgress(tip_info.verification_progress);
        Q_EMIT setTimeRatioListInitial();
        // Re-add the log file to the watcher: it may not have existed when
        // ConnectToDebugLogSignal() was called during construction (before
        // AppInitMain creates the file via StartLogging).
        const QString log_path = debugLogPath();
        if (!log_path.isEmpty()) m_log_watcher.addPath(log_path);
    }
    Q_EMIT nodeInitialized();
}

void NodeModel::startShutdownPolling()
{
    m_shutdown_polling_timer_id = startTimer(200ms);
}

void NodeModel::stopShutdownPolling()
{
    killTimer(m_shutdown_polling_timer_id);
}

void NodeModel::timerEvent(QTimerEvent* event)
{
    Q_UNUSED(event)
    if (m_node.shutdownRequested()) {
        stopShutdownPolling();
        Q_EMIT requestedShutdown();
    }
}

void NodeModel::ConnectToBlockTipSignal()
{
    assert(!m_handler_notify_block_tip);

    m_handler_notify_block_tip = m_node.handleNotifyBlockTip(
        [this](SynchronizationState state, interfaces::BlockTip tip, double verification_progress) {
            QMetaObject::invokeMethod(this, [&, this] {
                setBlockTipHeight(tip.block_height);
                setVerificationProgress(verification_progress);

                Q_EMIT setTimeRatioList(tip.block_time);
            });
        });
}

void NodeModel::ConnectToNumConnectionsChangedSignal()
{
    assert(!m_handler_notify_num_peers_changed);

    m_handler_notify_num_peers_changed = m_node.handleNotifyNumConnectionsChanged(
        [this](int new_num_connections) {
            setNumOutboundPeers(new_num_connections);
        });
}

bool NodeModel::validateProxyAddress(QString address_port)
{
    uint16_t port{0};
    std::string addr_port{address_port.toStdString()};
    std::string hostname;
    // First, attempt to split the input address into hostname and port components.
    // We call SplitHostPort to validate that a port is provided in addr_port.
    // If either splitting fails or port is zero (not specified), return false.
    if (!SplitHostPort(addr_port, port, hostname) || !port) return false;

    // Create a service endpoint (CService) from the address and port.
    // If port is missing in addr_port, DEFAULT_PROXY_PORT is used as the fallback.
    CService serv(LookupNumeric(addr_port, DEFAULT_PROXY_PORT));

    // Construct the Proxy with the service endpoint and return if it's valid
    Proxy addrProxy = Proxy(serv, true);
    return addrProxy.IsValid();
}

QString NodeModel::defaultProxyAddress()
{
    return QString::fromStdString(std::string(DEFAULT_PROXY_HOST) + ":" + util::ToString(DEFAULT_PROXY_PORT));
}

QString NodeModel::debugLogPath() const
{
    return QString::fromStdString(LogInstance().m_file_path.utf8string());
}

static QString debugLogErrorString(const fs::path& log_path)
{
    const QString path_str = QString::fromStdString(log_path.utf8string());
    return !fs::exists(log_path)
        ? NodeModel::tr("Debug log file not found: %1").arg(path_str)
        : NodeModel::tr("Could not open debug log file: %1").arg(path_str);
}

bool NodeModel::openDebugLogFile()
{
    const fs::path log_path = LogInstance().m_file_path;
    if (!fs::exists(log_path)) {
        const QString err = debugLogErrorString(log_path);
        if (m_debug_log_open_error != err) {
            m_debug_log_open_error = err;
            Q_EMIT debugLogOpenErrorChanged();
        }
        return false;
    }
    const bool ok = QDesktopServices::openUrl(
        QUrl::fromLocalFile(QString::fromStdString(log_path.utf8string())));
    if (!ok) {
        m_debug_log_open_error = tr(
            "Could not open debug log file. "
            "No application is associated with this file type.");
        Q_EMIT debugLogOpenErrorChanged();
        return false;
    }
    if (!m_debug_log_open_error.isEmpty()) {
        m_debug_log_open_error.clear();
        Q_EMIT debugLogOpenErrorChanged();
    }
    return true;
}

QVariantList NodeModel::debugLogLines(int max_lines)
{
    const fs::path log_path = LogInstance().m_file_path;
    QFile file(QString::fromStdString(log_path.utf8string()));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString err = debugLogErrorString(log_path);
        if (m_debug_log_open_error != err) {
            m_debug_log_open_error = err;
            Q_EMIT debugLogOpenErrorChanged();
        }
        return {};
    }
    if (!m_debug_log_open_error.isEmpty()) {
        m_debug_log_open_error.clear();
        Q_EMIT debugLogOpenErrorChanged();
    }

    // Seek near the end of the file to avoid reading the entire file into
    // memory. debug.log on an active mainnet node can exceed 100 MB.
    // Conservative estimate: 500 bytes per log line.
    const qint64 seek_pos = std::max(qint64{0},
        file.size() - static_cast<qint64>(max_lines) * 500);
    if (seek_pos > 0) {
        file.seek(seek_pos);
    }

    QTextStream in(&file);
    if (seek_pos > 0) {
        in.readLine(); // discard the potentially partial first line
    }

    QStringList lines;
    lines.reserve(max_lines);
    while (!in.atEnd()) {
        lines.append(in.readLine());
    }
    if (lines.size() > max_lines) {
        lines = lines.mid(lines.size() - max_lines);
    }

    static const QRegularExpression ts_rx(
        QStringLiteral(R"(^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z)\s*(.*)$)"));

    QVariantList result;
    result.reserve(lines.size());
    for (const QString& raw : lines) {
        QVariantMap entry;
        entry[QStringLiteral("raw")] = raw;
        const QRegularExpressionMatch m = ts_rx.match(raw);
        if (m.hasMatch()) {
            const QDateTime dt = QDateTime::fromString(m.captured(1), Qt::ISODateWithMs);
            entry[QStringLiteral("timestamp")] = dt.isValid() ? dt.toMSecsSinceEpoch() : qint64{-1};
            entry[QStringLiteral("message")] = m.captured(2);
        } else {
            entry[QStringLiteral("timestamp")] = qint64{-1};
            entry[QStringLiteral("message")] = raw;
        }
        result.append(entry);
    }
    return result;
}

bool NodeModel::disconnectPeer(int nodeId)
{
    return m_node.disconnectById(nodeId);
}

bool NodeModel::banPeer(const QString& rawAddress, int64_t banDuration)
{
    auto addr = LookupHost(rawAddress.toStdString(), /*fAllowLookup=*/false);
    if (!addr) return false;
    bool result = m_node.ban(*addr, banDuration);
    if (result) {
        m_node.disconnectByAddress(*addr);
    }
    return result;
}

void NodeModel::ConnectToDebugLogSignal()
{
    const QString path = debugLogPath();
    if (path.isEmpty()) return;
    m_log_watcher.addPath(path);
    connect(&m_log_watcher, &QFileSystemWatcher::fileChanged,
            this, [this](const QString& path) {
                m_log_watcher.addPath(path);
                Q_EMIT debugLogChanged();
            });
}

void NodeModel::setDebugLogFilter(const QString& filter)
{
    if (m_debug_log_filter == filter) return;
    m_debug_log_filter = filter;
    buildFormattedDebugLog();
}

void NodeModel::setDebugLogLoadLimit(int limit)
{
    if (m_debug_log_load_limit == limit) return;
    m_debug_log_load_limit = limit;
    Q_EMIT debugLogLoadLimitChanged();
}

void NodeModel::setDebugLogLineNumColor(const QString& color)
{
    if (m_debug_log_line_num_color == color) return;
    m_debug_log_line_num_color = color;
    buildFormattedDebugLog();
}

void NodeModel::setDebugLogMessageColor(const QString& color)
{
    if (m_debug_log_message_color == color) return;
    m_debug_log_message_color = color;
    buildFormattedDebugLog();
}

void NodeModel::setDebugLogTimestampColor(const QString& color)
{
    if (m_debug_log_timestamp_color == color) return;
    m_debug_log_timestamp_color = color;
    buildFormattedDebugLog();
}

void NodeModel::refreshDebugLog(bool full_load)
{
    const QString prev_top_raw = m_all_lines.isEmpty()
        ? QString{}
        : m_all_lines.first()[QStringLiteral("raw")].toString();

    if (full_load || m_all_lines.isEmpty()) {
        // Full (re-)load: initial load or Load More — use doubling loop to ensure
        // we get load_limit actual non-blank lines.
        const int load_limit = m_debug_log_load_limit;
        int fetch_size = load_limit;
        QVariantList filtered;
        while (true) {
            const QVariantList raw_lines = debugLogLines(fetch_size);
            filtered.clear();
            for (const QVariant& v : raw_lines) {
                if (!v.toMap()[QStringLiteral("raw")].toString().trimmed().isEmpty())
                    filtered.append(v);
            }
            if (filtered.size() > load_limit || raw_lines.size() < fetch_size)
                break;
            fetch_size *= 2;
        }
        const bool new_has_more = filtered.size() > load_limit;
        const int start = new_has_more ? filtered.size() - load_limit : 0;
        m_all_lines.clear();
        m_all_lines.reserve(load_limit);
        for (int i = filtered.size() - 1; i >= start; i--)
            m_all_lines.append(filtered[i].toMap());
        if (m_has_more_lines != new_has_more) {
            m_has_more_lines = new_has_more;
            Q_EMIT debugLogHasMoreLinesChanged();
        }
    } else {
        // Auto-refresh: fetch recent lines, prepend only lines newer than the
        // previous top entry. Never drops existing loaded lines.
        const QVariantList raw_lines2 = debugLogLines(m_debug_log_load_limit);
        QVariantList filtered2;
        for (const QVariant& v : raw_lines2) {
            if (!v.toMap()[QStringLiteral("raw")].toString().trimmed().isEmpty())
                filtered2.append(v);
        }
        if (!prev_top_raw.isEmpty()) {
            QList<QVariantMap> new_lines;
            bool found_prev = false;
            for (int j = filtered2.size() - 1; j >= 0; j--) {
                if (filtered2[j].toMap()[QStringLiteral("raw")].toString() == prev_top_raw) {
                    found_prev = true;
                    break;
                }
                new_lines.append(filtered2[j].toMap());
            }
            if (found_prev && !new_lines.isEmpty()) {
                m_all_lines = new_lines + m_all_lines;
            } else if (!found_prev) {
                // Log rotated or window missed — fall back to full reset.
                const bool new_has_more = filtered2.size() > m_debug_log_load_limit;
                const int start = new_has_more ? filtered2.size() - m_debug_log_load_limit : 0;
                m_all_lines.clear();
                m_all_lines.reserve(m_debug_log_load_limit);
                for (int i = filtered2.size() - 1; i >= start; i--)
                    m_all_lines.append(filtered2[i].toMap());
                if (m_has_more_lines != new_has_more) {
                    m_has_more_lines = new_has_more;
                    Q_EMIT debugLogHasMoreLinesChanged();
                }
            }
        }
    }

    // Count how many new lines appeared at the front (for pill notification).
    if (!prev_top_raw.isEmpty()) {
        for (int k = 0; k < m_all_lines.size(); k++) {
            if (m_all_lines[k][QStringLiteral("raw")].toString() == prev_top_raw) {
                if (k > 0) Q_EMIT newDebugLogLines(k);
                break;
            }
        }
    }

    buildFormattedDebugLog();
}

void NodeModel::updateDebugLogTimestamps()
{
    buildFormattedDebugLog();
}

void NodeModel::buildFormattedDebugLog()
{
    // Apply search filter.
    QList<QVariantMap> lines;
    if (m_debug_log_filter.isEmpty()) {
        lines = m_all_lines;
    } else {
        const QString filter = m_debug_log_filter.toLower();
        for (const QVariantMap& entry : m_all_lines) {
            if (entry[QStringLiteral("raw")].toString().toLower().contains(filter))
                lines.append(entry);
        }
    }

    m_debug_log_line_count = lines.size();

    if (lines.isEmpty()) {
        m_formatted_debug_log.clear();
        Q_EMIT formattedDebugLogChanged();
        return;
    }

    const int num_width = std::max(2, static_cast<int>(QString::number(lines.size()).length()));
    const int num_col_px = num_width * 7 + 8;
    constexpr int ts_col_px = 80;
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();

    QString html;
    html.reserve(lines.size() * 200);
    html += QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\" width=\"100%\">");
    for (int i = 0; i < lines.size(); i++) {
        const QString num = QString::number(i + 1);
        const QString msg = lines[i][QStringLiteral("message")].toString().toHtmlEscaped();
        const qint64 ts = lines[i][QStringLiteral("timestamp")].toLongLong();
        const QString ts_label = ts >= 0 ? relativeTimeLabel(ts, now_ms) : QString{};
        html += QStringLiteral("<tr>")
              + QStringLiteral("<td width=\"") + QString::number(num_col_px)
              + QStringLiteral("\" align=\"right\" style=\"color:") + m_debug_log_line_num_color
              + QStringLiteral("; font-family:monospace; font-size:11px; vertical-align:top; padding-right:8px\">")
              + num + QStringLiteral("</td>")
              + QStringLiteral("<td style=\"color:") + m_debug_log_message_color
              + QStringLiteral("; font-family:monospace; font-size:11px\">")
              + msg + QStringLiteral("</td>")
              + QStringLiteral("<td width=\"") + QString::number(ts_col_px)
              + QStringLiteral("\" align=\"right\" style=\"color:") + m_debug_log_timestamp_color
              + QStringLiteral("; font-size:10px; vertical-align:top; padding-left:8px\">")
              + ts_label + QStringLiteral("</td>")
              + QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table>");

    m_formatted_debug_log = html;
    Q_EMIT formattedDebugLogChanged();
}

QString NodeModel::relativeTimeLabel(qint64 timestamp_ms, qint64 now_ms) const
{
    const qint64 diff = (now_ms - timestamp_ms) / 1000;
    if (diff < 60)    return tr("just now");
    if (diff < 3600)  return tr("%1 min ago").arg(diff / 60);
    if (diff < 86400) return tr("%1 hr ago").arg(diff / 3600);
    return tr("%1 d ago").arg(diff / 86400);
}

void NodeModel::ConnectToBannedListChangedSignal()
{
    assert(!m_handler_notify_banned_list_changed);
    m_handler_notify_banned_list_changed = m_node.handleBannedListChanged([this]() {
        QMetaObject::invokeMethod(this, [this] {
            Q_EMIT bannedListChanged();
        });
    });
}
