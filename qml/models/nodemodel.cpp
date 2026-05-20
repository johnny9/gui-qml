// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/nodemodel.h>

#include <common/args.h>
#include <interfaces/node.h>
#include <net.h>
#include <net_processing.h>
#include <netbase.h>
#include <node/interface_ui.h>
#include <util/threadnames.h>
#include <validation.h>

#include <cassert>
#include <chrono>

#include <QDateTime>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QTimerEvent>

using namespace std::chrono_literals;

static constexpr int MEMPOOL_INFO_POLLING_INTERVAL_MS{3000};

NodeModel::NodeModel(interfaces::Node& node)
    : m_node{node}
{
    m_mempool_information_available = !gArgs.GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY);
    initializeMempoolInfoPolling();
    refreshPeerCounts();
    ConnectToBlockTipSignal();
    ConnectToNumConnectionsChangedSignal();
    ConnectToBannedListChangedSignal();
    refreshMempoolInfo();
}

NodeModel::~NodeModel()
{
    setMempoolInfoPollingActive(false);
    if (m_mempool_info_thread) {
        m_mempool_info_thread->quit();
        m_mempool_info_thread->wait();
    }
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

void NodeModel::setNumPeers(int new_num)
{
    if (new_num != m_num_peers) {
        m_num_peers = new_num;
        Q_EMIT numPeersChanged();
    }
}

void NodeModel::setNumInboundPeers(int new_num)
{
    if (new_num != m_num_inbound_peers) {
        m_num_inbound_peers = new_num;
        Q_EMIT numInboundPeersChanged();
    }
}

void NodeModel::refreshPeerCounts()
{
    setNumPeers(static_cast<int>(m_node.getNodeCount(ConnectionDirection::Both)));
    setNumInboundPeers(static_cast<int>(m_node.getNodeCount(ConnectionDirection::In)));
    setNumOutboundPeers(static_cast<int>(m_node.getNodeCount(ConnectionDirection::Out)));
}

void NodeModel::refreshMempoolInfo()
{
    requestMempoolInfoRefresh();
}

void NodeModel::setMempoolInfoPollingActive(bool active)
{
    if (m_mempool_info_polling_active == active) {
        return;
    }
    m_mempool_info_polling_active = active;
    Q_EMIT mempoolInfoPollingActiveChanged(active);

    QTimer* timer = m_mempool_info_timer;
    if (!timer) {
        return;
    }

    QMetaObject::invokeMethod(timer, [timer, active] {
        if (active) {
            timer->start();
        } else {
            timer->stop();
        }
    }, Qt::QueuedConnection);

    if (active) {
        refreshMempoolInfo();
    }
}

void NodeModel::initializeMempoolInfoPolling()
{
    m_mempool_info_worker = new QObject;
    m_mempool_info_thread = new QThread(this);
    m_mempool_info_timer = new QTimer;
    m_mempool_info_timer->setInterval(MEMPOOL_INFO_POLLING_INTERVAL_MS);

    m_mempool_info_worker->moveToThread(m_mempool_info_thread);
    m_mempool_info_timer->moveToThread(m_mempool_info_thread);

    connect(m_mempool_info_timer, &QTimer::timeout, m_mempool_info_worker, [this] {
        fetchMempoolInfo();
    });
    connect(m_mempool_info_thread, &QThread::finished, m_mempool_info_timer, &QObject::deleteLater);
    connect(m_mempool_info_thread, &QThread::finished, m_mempool_info_worker, &QObject::deleteLater);

    m_mempool_info_thread->start();
    QTimer::singleShot(0, m_mempool_info_worker, [] {
        util::ThreadRename("qml-mempool");
    });
}

void NodeModel::requestMempoolInfoRefresh()
{
    if (!m_mempool_info_worker) {
        return;
    }

    QTimer::singleShot(0, m_mempool_info_worker, [this] {
        fetchMempoolInfo();
    });
}

void NodeModel::fetchMempoolInfo()
{
    const MempoolInfo info{
        static_cast<int>(m_node.getMempoolSize()),
        m_node.getMempoolDynamicUsage() / 1'000'000.0,
        m_node.getMempoolMaxUsage() / 1'000'000.0,
    };

    QMetaObject::invokeMethod(this, [this, info] {
        applyMempoolInfo(info);
    }, Qt::QueuedConnection);
}

void NodeModel::applyMempoolInfo(const MempoolInfo& info)
{
    if (info.transaction_count != m_mempool_transaction_count ||
        info.usage_mb != m_mempool_usage_mb ||
        info.max_usage_mb != m_mempool_max_usage_mb) {
        m_mempool_transaction_count = info.transaction_count;
        m_mempool_usage_mb = info.usage_mb;
        m_mempool_max_usage_mb = info.max_usage_mb;
        Q_EMIT mempoolInfoChanged();
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
    if (m_initialization_requested) {
        return;
    }
    m_initialization_requested = true;
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
        refreshMempoolInfo();
        Q_EMIT setTimeRatioListInitial();
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
        [this]([[maybe_unused]] SynchronizationState state, interfaces::BlockTip tip, double verification_progress) {
            QMetaObject::invokeMethod(this, [this, block_height = tip.block_height, block_time = tip.block_time, verification_progress] {
                setBlockTipHeight(block_height);
                setVerificationProgress(verification_progress);

                Q_EMIT setTimeRatioList(block_time);
            }, Qt::QueuedConnection);
        });
}

void NodeModel::ConnectToNumConnectionsChangedSignal()
{
    assert(!m_handler_notify_num_peers_changed);

    m_handler_notify_num_peers_changed = m_node.handleNotifyNumConnectionsChanged(
        [this]([[maybe_unused]] int new_num_connections) {
            QMetaObject::invokeMethod(this, [this] {
                refreshPeerCounts();
            }, Qt::QueuedConnection);
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

bool NodeModel::disconnectPeer(int nodeId)
{
    return m_node.disconnectById(nodeId);
}

bool NodeModel::banPeer(const QString& rawAddress, int64_t banDuration)
{
    if (banDuration <= 0) return false;
    auto addr = LookupHost(rawAddress.toStdString(), /*fAllowLookup=*/false);
    if (!addr) return false;
    bool result = m_node.ban(*addr, banDuration);
    if (result) {
        m_node.disconnectByAddress(*addr);
    }
    return result;
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
