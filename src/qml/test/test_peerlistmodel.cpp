// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mocknode.h>
#include <qml/models/peerlistmodel.h>
#include <util/translation.h>

#include <utility>

const TranslateFn G_TRANSLATION_FUN{nullptr};

namespace {
interfaces::Node::NodesStats MakeStats(std::initializer_list<CNodeStats> node_stats)
{
    interfaces::Node::NodesStats stats;
    for (const auto& node_stat : node_stats) {
        stats.emplace_back(node_stat, true, CNodeStateStats{});
    }
    return stats;
}

CNodeStats MakeNodeStats(NodeId node_id, std::string address, bool inbound, ConnectionType connection_type, Network network)
{
    CNodeStats stats;
    stats.nodeid = node_id;
    stats.m_connected = std::chrono::seconds{1'000};
    stats.m_addr_name = std::move(address);
    stats.fInbound = inbound;
    stats.m_conn_type = connection_type;
    stats.m_network = network;
    stats.m_min_ping_time = std::chrono::microseconds{1'500};
    stats.nSendBytes = 1'200;
    stats.nRecvBytes = 900;
    stats.cleanSubVer = "/Satoshi:28.0.0/";
    return stats;
}
} // namespace

class PeerListModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void mapsRoleData();
    void refreshUpdatesRows();
    void startStopAutoRefresh();
};

void PeerListModelTests::mapsRoleData()
{
    using ::testing::_;
    using ::testing::DoAll;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::SetArgReferee;

    const auto stats{MakeStats({MakeNodeStats(7, "127.0.0.1:8333", false, ConnectionType::OUTBOUND_FULL_RELAY, NET_IPV4)})};
    NiceMock<MockNode> node;
    EXPECT_CALL(node, getNodesStats(_))
        .Times(1)
        .WillOnce(DoAll(SetArgReferee<0>(stats), Return(true)));

    PeerListModel model{node, nullptr};
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);

    const QModelIndex index = model.index(0, 0);
    QVERIFY(index.isValid());

    const auto roles = model.roleNames();
    QCOMPARE(roles.value(PeerListModel::NetNodeId), QByteArray{"nodeId"});
    QCOMPARE(roles.value(PeerListModel::Address), QByteArray{"address"});
    QCOMPARE(roles.value(PeerListModel::ConnectionType), QByteArray{"connectionType"});
    QCOMPARE(roles.value(PeerListModel::StatsRole), QByteArray{"stats"});

    QCOMPARE(model.data(index, PeerListModel::NetNodeId).toLongLong(), 7LL);
    QCOMPARE(model.data(index, PeerListModel::Address).toString(), QString{"127.0.0.1:8333"});
    QCOMPARE(model.data(index, PeerListModel::Direction).toString(), QString{"Outbound"});
    QCOMPARE(model.data(index, PeerListModel::ConnectionType).toString(), QString{"Full Relay"});
    QCOMPARE(model.data(index, PeerListModel::Network).toString(), QString{"IPv4"});
    QCOMPARE(model.data(index, PeerListModel::Ping).toString(), QString{"1 ms"});
    QCOMPARE(model.data(index, PeerListModel::Sent).toString(), QString{"1 kB"});
    QCOMPARE(model.data(index, PeerListModel::Received).toString(), QString{"900 B"});
    QCOMPARE(model.data(index, PeerListModel::Subversion).toString(), QString{"/Satoshi:28.0.0/"});
    QVERIFY(!model.data(index, PeerListModel::Age).toString().isEmpty());

    CNodeCombinedStats* stats_ptr = model.data(index, PeerListModel::StatsRole).value<CNodeCombinedStats*>();
    QVERIFY(stats_ptr != nullptr);
    QCOMPARE(stats_ptr->nodeStats.nodeid, 7);

    QCOMPARE(model.flags(QModelIndex{}), Qt::NoItemFlags);
    QVERIFY(model.flags(index).testFlag(Qt::ItemIsSelectable));
    QVERIFY(model.flags(index).testFlag(Qt::ItemIsEnabled));
}

void PeerListModelTests::refreshUpdatesRows()
{
    using ::testing::_;
    using ::testing::DoAll;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::SetArgReferee;

    const auto stats_initial{MakeStats({
        MakeNodeStats(1, "10.0.0.1:8333", true, ConnectionType::INBOUND, NET_IPV4),
        MakeNodeStats(2, "10.0.0.2:8333", false, ConnectionType::MANUAL, NET_IPV6),
    })};
    const auto stats_remove{MakeStats({
        MakeNodeStats(2, "10.0.0.2:8333", false, ConnectionType::MANUAL, NET_IPV6),
    })};
    const auto stats_insert{MakeStats({
        MakeNodeStats(2, "10.0.0.2:8333", false, ConnectionType::MANUAL, NET_IPV6),
        MakeNodeStats(3, "10.0.0.3:8333", false, ConnectionType::BLOCK_RELAY, NET_ONION),
    })};

    NiceMock<MockNode> node;
    EXPECT_CALL(node, getNodesStats(_))
        .Times(3)
        .WillOnce(DoAll(SetArgReferee<0>(stats_initial), Return(true)))
        .WillOnce(DoAll(SetArgReferee<0>(stats_remove), Return(true)))
        .WillOnce(DoAll(SetArgReferee<0>(stats_insert), Return(true)));

    PeerListModel model{node, nullptr};
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 1LL);
    QCOMPARE(model.data(model.index(1, 0), PeerListModel::NetNodeId).toLongLong(), 2LL);

    model.refresh();
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 2LL);

    model.refresh();
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 2LL);
    QCOMPARE(model.data(model.index(1, 0), PeerListModel::NetNodeId).toLongLong(), 3LL);
}

void PeerListModelTests::startStopAutoRefresh()
{
    using ::testing::_;
    using ::testing::AtLeast;
    using ::testing::Invoke;
    using ::testing::NiceMock;

    const auto stats{MakeStats({MakeNodeStats(1, "10.0.0.1:8333", true, ConnectionType::INBOUND, NET_IPV4)})};

    NiceMock<MockNode> node;
    int get_nodes_stats_calls{0};
    EXPECT_CALL(node, getNodesStats(_))
        .Times(AtLeast(2))
        .WillRepeatedly(Invoke([&](interfaces::Node::NodesStats& out_stats) {
            ++get_nodes_stats_calls;
            out_stats = stats;
            return true;
        }));

    PeerListModel model{node, nullptr};
    const int calls_after_ctor = get_nodes_stats_calls;
    QCOMPARE(calls_after_ctor, 1);

    model.startAutoRefresh();
    QTRY_VERIFY_WITH_TIMEOUT(get_nodes_stats_calls > calls_after_ctor, 1200);

    model.stopAutoRefresh();
    const int calls_after_stop = get_nodes_stats_calls;
    QTest::qWait(450);
    QCOMPARE(get_nodes_stats_calls, calls_after_stop);
}

int RunPeerListModelTests(int argc, char* argv[])
{
    PeerListModelTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#ifndef BITCOINQML_NO_TEST_MAIN
QTEST_MAIN(PeerListModelTests)
#endif
#include "test_peerlistmodel.moc"
