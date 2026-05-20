// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/gmocktestfixture.h>
#include <test/mocks/mocknode.h>
#include <test/qt_test_registry.h>

#include <qml/models/nodemodel.h>

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <validation.h>

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

namespace {
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Truly;

constexpr int ASYNC_TIMEOUT_MS{1'000};

std::unique_ptr<interfaces::Handler> MakeNoopHandler()
{
    return interfaces::MakeCleanupHandler([] {});
}

struct MempoolState {
    std::atomic<size_t> count{0};
    std::atomic<size_t> usage_bytes{0};
    std::atomic<size_t> max_bytes{0};
    std::atomic<int> max_usage_calls{0};
};

struct PeerCountState {
    std::atomic<size_t> total{0};
    std::atomic<size_t> inbound{0};
    std::atomic<size_t> outbound{0};
};

void InstallDefaultHandlers(NiceMock<MockNode>& node)
{
    ON_CALL(node, handleNotifyBlockTip(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::NotifyBlockTipFn) {
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleNotifyNumConnectionsChanged(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::NotifyNumConnectionsChangedFn) {
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleBannedListChanged(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::BannedListChangedFn) {
            return MakeNoopHandler();
        }));
}

void InstallPeerCountGetters(NiceMock<MockNode>& node, PeerCountState& peers)
{
    ON_CALL(node, getNodeCount(testing::_)).WillByDefault(Invoke([&peers](ConnectionDirection direction) {
        switch (direction) {
        case ConnectionDirection::Both:
            return peers.total.load();
        case ConnectionDirection::In:
            return peers.inbound.load();
        case ConnectionDirection::Out:
            return peers.outbound.load();
        case ConnectionDirection::None:
            return size_t{0};
        }
        return size_t{0};
    }));
}

void InstallMempoolGetters(NiceMock<MockNode>& node, MempoolState& mempool)
{
    ON_CALL(node, getMempoolSize()).WillByDefault(Invoke([&mempool] {
        return mempool.count.load();
    }));
    ON_CALL(node, getMempoolDynamicUsage()).WillByDefault(Invoke([&mempool] {
        return mempool.usage_bytes.load();
    }));
    ON_CALL(node, getMempoolMaxUsage()).WillByDefault(Invoke([&mempool] {
        ++mempool.max_usage_calls;
        return mempool.max_bytes.load();
    }));
}

void WaitForInitialMempoolRefresh(MempoolState& mempool)
{
    QTRY_VERIFY_WITH_TIMEOUT(mempool.max_usage_calls.load() >= 1, ASYNC_TIMEOUT_MS);
    QCoreApplication::processEvents();
}
} // namespace

class NodeModelTests : public GmockTestFixture
{
    Q_OBJECT

private Q_SLOTS:
    void refreshMempoolInfoUpdatesProperties();
    void activatingMempoolPollingEmitsSignalsAndRefreshesImmediately();
    void peerCountsInitializeAndRefreshFromDirectionalNodeCounts();
    void disconnectPeerReturnsNodeResult();
    void banPeerRejectsInvalidInputs();
    void banPeerReturnsFalseWithoutDisconnectWhenBackendFails();
    void banPeerDisconnectsAddressAfterSuccessfulBan();
    void nodeNotificationHandlersUpdateModelThroughQueuedSignals();
    void blockTipUpdatesQueuedAcrossThreadsRetainPayloadValues();
};

void NodeModelTests::refreshMempoolInfoUpdatesProperties()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    mempool.count = 12;
    mempool.usage_bytes = 12'500'000;
    mempool.max_bytes = 300'000'000;

    QSignalSpy mempool_info_spy{&model, &NodeModel::mempoolInfoChanged};
    model.refreshMempoolInfo();

    QTRY_COMPARE_WITH_TIMEOUT(mempool_info_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.mempoolTransactionCount(), 12);
    QCOMPARE(model.mempoolUsageMB(), 12.5);
    QCOMPARE(model.mempoolMaxUsageMB(), 300.0);
}

void NodeModelTests::activatingMempoolPollingEmitsSignalsAndRefreshesImmediately()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    mempool.count = 3;
    mempool.usage_bytes = 4'000'000;
    mempool.max_bytes = 500'000'000;

    QSignalSpy polling_active_spy{&model, &NodeModel::mempoolInfoPollingActiveChanged};
    QSignalSpy mempool_info_spy{&model, &NodeModel::mempoolInfoChanged};
    model.setMempoolInfoPollingActive(true);

    QCOMPARE(model.mempoolInfoPollingActive(), true);
    QCOMPARE(polling_active_spy.count(), 1);
    QCOMPARE(polling_active_spy.takeFirst().at(0).toBool(), true);
    QTRY_COMPARE_WITH_TIMEOUT(mempool_info_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.mempoolTransactionCount(), 3);
    QCOMPARE(model.mempoolUsageMB(), 4.0);
    QCOMPARE(model.mempoolMaxUsageMB(), 500.0);

    model.setMempoolInfoPollingActive(false);
    QCOMPARE(model.mempoolInfoPollingActive(), false);
    QCOMPARE(polling_active_spy.count(), 1);
    QCOMPARE(polling_active_spy.takeFirst().at(0).toBool(), false);
}

void NodeModelTests::peerCountsInitializeAndRefreshFromDirectionalNodeCounts()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    PeerCountState peers;
    interfaces::Node::NotifyNumConnectionsChangedFn connections_changed_fn;

    peers.total = 3;
    peers.inbound = 1;
    peers.outbound = 2;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    InstallPeerCountGetters(node, peers);
    ON_CALL(node, handleNotifyNumConnectionsChanged(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyNumConnectionsChangedFn fn) {
            connections_changed_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(connections_changed_fn);
    QCOMPARE(model.numPeers(), 3);
    QCOMPARE(model.numInboundPeers(), 1);
    QCOMPARE(model.numOutboundPeers(), 2);

    QSignalSpy peers_spy{&model, &NodeModel::numPeersChanged};
    QSignalSpy inbound_spy{&model, &NodeModel::numInboundPeersChanged};
    QSignalSpy outbound_spy{&model, &NodeModel::numOutboundPeersChanged};

    peers.total = 7;
    peers.inbound = 5;
    peers.outbound = 2;
    connections_changed_fn(99);

    QTRY_COMPARE_WITH_TIMEOUT(peers_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(inbound_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(outbound_spy.count(), 0);
    QCOMPARE(model.numPeers(), 7);
    QCOMPARE(model.numInboundPeers(), 5);
    QCOMPARE(model.numOutboundPeers(), 2);

    peers.total = 8;
    peers.inbound = 5;
    peers.outbound = 3;
    connections_changed_fn(1);

    QTRY_COMPARE_WITH_TIMEOUT(peers_spy.count(), 2, ASYNC_TIMEOUT_MS);
    QCOMPARE(inbound_spy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(outbound_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.numPeers(), 8);
    QCOMPARE(model.numInboundPeers(), 5);
    QCOMPARE(model.numOutboundPeers(), 3);
}

void NodeModelTests::disconnectPeerReturnsNodeResult()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    EXPECT_CALL(node, disconnectById(7)).Times(1).WillOnce(Return(true));
    QVERIFY(model.disconnectPeer(7));

    EXPECT_CALL(node, disconnectById(8)).Times(1).WillOnce(Return(false));
    QVERIFY(!model.disconnectPeer(8));
}

void NodeModelTests::banPeerRejectsInvalidInputs()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    EXPECT_CALL(node, ban(testing::_, testing::_)).Times(0);
    EXPECT_CALL(node, disconnectByAddress(testing::_)).Times(0);

    QVERIFY(!model.banPeer(QStringLiteral("not an address"), 3600));
    QVERIFY(!model.banPeer(QStringLiteral("127.0.0.1"), 0));
    QVERIFY(!model.banPeer(QStringLiteral("127.0.0.1"), -1));
}

void NodeModelTests::banPeerReturnsFalseWithoutDisconnectWhenBackendFails()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    EXPECT_CALL(node, ban(Truly([](const CNetAddr& value) {
        return value.ToStringAddr() == "127.0.0.1";
    }), 3600)).Times(1).WillOnce(Return(false));
    EXPECT_CALL(node, disconnectByAddress(testing::_)).Times(0);

    QVERIFY(!model.banPeer(QStringLiteral("127.0.0.1"), 3600));
}

void NodeModelTests::banPeerDisconnectsAddressAfterSuccessfulBan()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    auto is_loopback = [](const CNetAddr& value) {
        return value.ToStringAddr() == "127.0.0.1";
    };
    EXPECT_CALL(node, ban(Truly(is_loopback), 3600)).Times(1).WillOnce(Return(true));
    EXPECT_CALL(node, disconnectByAddress(Truly(is_loopback))).Times(1).WillOnce(Return(true));

    QVERIFY(model.banPeer(QStringLiteral("127.0.0.1"), 3600));
}

void NodeModelTests::nodeNotificationHandlersUpdateModelThroughQueuedSignals()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    PeerCountState peers;
    interfaces::Node::NotifyBlockTipFn block_tip_fn;
    interfaces::Node::NotifyNumConnectionsChangedFn connections_changed_fn;
    interfaces::Node::BannedListChangedFn banned_list_changed_fn;

    InstallMempoolGetters(node, mempool);
    InstallPeerCountGetters(node, peers);
    ON_CALL(node, handleNotifyBlockTip(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyBlockTipFn fn) {
            block_tip_fn = std::move(fn);
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleNotifyNumConnectionsChanged(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyNumConnectionsChangedFn fn) {
            connections_changed_fn = std::move(fn);
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleBannedListChanged(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::BannedListChangedFn fn) {
            banned_list_changed_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(block_tip_fn);
    QVERIFY(connections_changed_fn);
    QVERIFY(banned_list_changed_fn);

    QSignalSpy block_tip_height_spy{&model, &NodeModel::blockTipHeightChanged};
    QSignalSpy verification_progress_spy{&model, &NodeModel::verificationProgressChanged};
    QSignalSpy time_ratio_spy{&model, &NodeModel::setTimeRatioList};
    QSignalSpy peers_spy{&model, &NodeModel::numPeersChanged};
    QSignalSpy inbound_peers_spy{&model, &NodeModel::numInboundPeersChanged};
    QSignalSpy outbound_peers_spy{&model, &NodeModel::numOutboundPeersChanged};
    QSignalSpy banned_list_spy{&model, &NodeModel::bannedListChanged};

    block_tip_fn(SynchronizationState::POST_INIT, interfaces::BlockTip{144, 1'700'000'000, uint256{}}, 0.42);
    peers.total = 9;
    peers.inbound = 2;
    peers.outbound = 7;
    connections_changed_fn(7);
    banned_list_changed_fn();

    QTRY_COMPARE_WITH_TIMEOUT(block_tip_height_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(verification_progress_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(time_ratio_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(peers_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(inbound_peers_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(outbound_peers_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(banned_list_spy.count(), 1, ASYNC_TIMEOUT_MS);

    QCOMPARE(model.blockTipHeight(), 144);
    QCOMPARE(model.verificationProgress(), 0.42);
    QCOMPARE(time_ratio_spy.takeFirst().at(0).toInt(), 1'700'000'000);
    QCOMPARE(model.numPeers(), 9);
    QCOMPARE(model.numInboundPeers(), 2);
    QCOMPARE(model.numOutboundPeers(), 7);
}

void NodeModelTests::blockTipUpdatesQueuedAcrossThreadsRetainPayloadValues()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::NotifyBlockTipFn block_tip_fn;

    InstallMempoolGetters(node, mempool);
    ON_CALL(node, handleNotifyBlockTip(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyBlockTipFn fn) {
            block_tip_fn = std::move(fn);
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleNotifyNumConnectionsChanged(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::NotifyNumConnectionsChangedFn) {
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleBannedListChanged(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::BannedListChangedFn) {
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(block_tip_fn);

    std::vector<double> seen_progress;
    std::vector<int> seen_heights;
    std::vector<int> seen_times;

    QObject::connect(&model, &NodeModel::verificationProgressChanged, &model, [&] {
        seen_progress.push_back(model.verificationProgress());
    });
    QObject::connect(&model, &NodeModel::blockTipHeightChanged, &model, [&] {
        seen_heights.push_back(model.blockTipHeight());
    });
    QObject::connect(&model, &NodeModel::setTimeRatioList, &model, [&](int block_time) {
        seen_times.push_back(block_time);
    });

    std::thread worker([&] {
        block_tip_fn(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{123, 1'700'000'001, uint256{}}, 0.51);
        block_tip_fn(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{456, 1'700'000'099, uint256{}}, 0.75);
    });
    worker.join();

    QTRY_COMPARE_WITH_TIMEOUT(seen_progress.size(), size_t{2}, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(seen_heights.size(), size_t{2}, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(seen_times.size(), size_t{2}, ASYNC_TIMEOUT_MS);

    QVERIFY(qFuzzyCompare(seen_progress.at(0), 0.51));
    QVERIFY(qFuzzyCompare(seen_progress.at(1), 0.75));
    QCOMPARE(seen_heights.at(0), 123);
    QCOMPARE(seen_heights.at(1), 456);
    QCOMPARE(seen_times.at(0), 1'700'000'001);
    QCOMPARE(seen_times.at(1), 1'700'000'099);
    QCOMPARE(model.blockTipHeight(), 456);
    QVERIFY(qFuzzyCompare(model.verificationProgress(), 0.75));
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(NodeModelTests)
#else
QTEST_MAIN(NodeModelTests)
#endif
#include "test_nodemodel.moc"
