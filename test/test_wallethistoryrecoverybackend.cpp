// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallethistoryrecoverybackend.h>

#include <blockfilter.h>
#include <script/script.h>
#include <test/qt_test_registry.h>
#include <uint256.h>

#include <QtTest/QtTest>

#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using wallet_history::Outcome;
using wallet_history::Phase;

uint256 BlockHash(int height)
{
    return uint256{static_cast<uint8_t>(height + 1)};
}

struct FakeRecoveryNode {
    bool pruned{true};
    bool index_enabled{true};
    bool index_synced{true};
    int index_height{2};
    bool initial_download{false};
    bool validated{true};
    bool locked{false};
    bool empty_scripts{false};
    bool unspendable_script{false};
    bool rpc_unlock_required{false};
    bool missing_blocks{false};
    bool archival_peer{false};
    bool filter_read_ok{true};
    bool short_filter_range{false};
    bool change_anchor_after_scan{false};
    bool null_rescan_stop{false};
    bool null_mempool_rescan_stop{false};
    bool mempool_spend_known{false};
    bool expand_scripts_after_scan{false};
    bool expand_scripts_after_mempool_rescan{false};
    bool cancel_during_scan{false};
    bool extend_chain_during_rescan{false};
    bool cancel_during_filter_catchup{false};
    int chain_height{2};
    int rpc_calls{0};
    int filter_reads{0};
    std::vector<int> rescanned;
    std::vector<int> mempool_rescans;
    std::vector<std::pair<int, int>> filter_ranges;
    std::vector<std::string> wallet_uris;
    std::vector<std::string> requested_blocks;
    std::set<std::string> downloaded_blocks;
    std::shared_ptr<wallet_history::Control> control{std::make_shared<wallet_history::Control>()};

    UniValue rpc(const std::string& method, const UniValue& params, const std::string& uri)
    {
        ++rpc_calls;
        if (method == "getblockchaininfo") {
            UniValue result{UniValue::VOBJ};
            result.pushKV("pruned", pruned);
            result.pushKV("initialblockdownload", initial_download);
            result.pushKV("blocks", chain_height);
            result.pushKV("bestblockhash", BlockHash(chain_height).GetHex());
            return result;
        }
        if (method == "getindexinfo") {
            if (cancel_during_filter_catchup && chain_height > index_height) control->cancelled = true;
            UniValue result{UniValue::VOBJ};
            if (index_enabled) {
                UniValue index{UniValue::VOBJ};
                index.pushKV("synced", index_synced);
                index.pushKV("best_block_height", index_height);
                result.pushKV("basic block filter index", std::move(index));
            }
            return result;
        }
        if (method == "getchainstates") {
            UniValue state{UniValue::VOBJ};
            state.pushKV("validated", validated);
            state.pushKV("blocks", 2);
            state.pushKV("bestblockhash", BlockHash(2).GetHex());
            UniValue states{UniValue::VARR};
            states.push_back(std::move(state));
            UniValue result{UniValue::VOBJ};
            result.pushKV("chainstates", std::move(states));
            return result;
        }
        if (method == "getwalletinfo") {
            wallet_uris.push_back(uri);
            UniValue result{UniValue::VOBJ};
            result.pushKV("descriptors", true);
            result.pushKV("scanning", false);
            if (locked) result.pushKV("unlocked_until", 0);
            return result;
        }
        if (method == "getblockhash") {
            const int height{params[0].getInt<int>()};
            if (change_anchor_after_scan && filter_reads > 0 && height == 2) {
                return UniValue{uint256{uint8_t{99}}.GetHex()};
            }
            return UniValue{BlockHash(height).GetHex()};
        }
        if (method == "getblockheader") {
            int height{-1};
            for (int candidate{0}; candidate <= 2; ++candidate) {
                if (params[0].get_str() == BlockHash(candidate).GetHex()) height = candidate;
            }
            if (height < 0) throw std::runtime_error("Unknown test block hash");
            UniValue result{UniValue::VOBJ};
            result.pushKV("height", height);
            result.pushKV("confirmations", 3 - height);
            return result;
        }
        if (method == "getblock") {
            if (missing_blocks && !downloaded_blocks.contains(params[0].get_str())) {
                UniValue error{UniValue::VOBJ};
                error.pushKV("code", -1);
                error.pushKV("message", "Block not available (pruned data)");
                throw error;
            }
            return UniValue{"00"};
        }
        if (method == "rescanblockchain") {
            wallet_uris.push_back(uri);
            if (rpc_unlock_required) {
                UniValue error{UniValue::VOBJ};
                error.pushKV("code", -13);
                error.pushKV("message", "Please enter the wallet passphrase first");
                throw error;
            }
            const int height{params[0].getInt<int>()};
            const bool include_mempool{params.size() == 1};
            if (include_mempool) {
                mempool_rescans.push_back(height);
                mempool_spend_known = std::find(rescanned.begin(), rescanned.end(), 1) != rescanned.end();
            } else {
                if (params[1].getInt<int>() != height) throw std::runtime_error("Rescan must be limited to one block");
                if (extend_chain_during_rescan && chain_height == 2) {
                    chain_height = 3;
                    if (!cancel_during_filter_catchup) index_height = 3;
                }
                rescanned.push_back(height);
            }
            UniValue result{UniValue::VOBJ};
            result.pushKV("start_height", height);
            const bool incomplete{include_mempool ? null_mempool_rescan_stop : null_rescan_stop};
            result.pushKV("stop_height", incomplete ? UniValue{} : UniValue{include_mempool ? chain_height : height});
            return result;
        }
        if (method == "getpeerinfo") {
            UniValue peers{UniValue::VARR};
            if (archival_peer) {
                UniValue services{UniValue::VARR};
                services.push_back("NETWORK");
                UniValue peer{UniValue::VOBJ};
                peer.pushKV("id", 7);
                peer.pushKV("servicesnames", std::move(services));
                peers.push_back(std::move(peer));
            }
            return peers;
        }
        if (method == "getblockfrompeer") {
            if (!archival_peer || params[1].getInt<int>() != 7) throw std::runtime_error("Unknown test peer");
            const auto& hash{params[0].get_str()};
            requested_blocks.push_back(hash);
            downloaded_blocks.insert(hash);
            return UniValue{UniValue::VOBJ};
        }
        throw std::runtime_error("Unexpected RPC: " + method);
    }

    bool readFilters(int start, int end, std::vector<BlockFilter>& filters)
    {
        ++filter_reads;
        filter_ranges.emplace_back(start, end);
        if (cancel_during_scan) control->cancelled = true;
        if (!filter_read_ok) return false;
        filters.clear();
        for (int height{start}; height <= end; ++height) {
            const auto hash{BlockHash(height)};
            GCSFilter::ElementSet elements;
            if (height > 0) {
                elements.insert(GCSFilter::Element{static_cast<unsigned char>(
                    (expand_scripts_after_scan || expand_scripts_after_mempool_rescan) && height == 2 ? 0x52 : 0x51)});
            }
            const GCSFilter filter{
                {hash.GetUint64(0), hash.GetUint64(1), BASIC_FILTER_P, BASIC_FILTER_M}, elements};
            filters.emplace_back(BlockFilterType::BASIC, hash, filter.GetEncoded(), false);
        }
        if (short_filter_range && !filters.empty()) filters.pop_back();
        return true;
    }

    wallet_history::Result recover(const std::string& wallet_name = "test-wallet")
    {
        const wallet_history::Backend backend{
            [this](const std::string& method, const UniValue& params, const std::string& uri) {
                return rpc(method, params, uri);
            },
            [this](int start, int end, std::vector<BlockFilter>& filters) {
                return readFilters(start, end, filters);
            },
            [this] {
                if (empty_scripts) return std::vector<CScript>{};
                std::vector<CScript> scripts{CScript{} << (unspendable_script ? OP_RETURN : OP_TRUE)};
                if ((expand_scripts_after_scan && filter_reads > 0) ||
                    (expand_scripts_after_mempool_rescan && !mempool_rescans.empty())) {
                    scripts.push_back(CScript{} << OP_2);
                }
                return scripts;
            },
        };
        return wallet_history::Recover(backend, wallet_name, control);
    }
};
} // namespace

class WalletHistoryRecoveryBackendTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void requiresPruningAndFilterIndex();
    void waitsForRequiredState();
    void honorsWalletUnlockState();
    void rejectsScriptsExcludedFromBasicFilters();
    void retrievesMissingBlocksBeforeRescanning();
    void rejectsFailedOrIncompleteFilterReads();
    void cancelsBeforeRescanning();
    void rescansMatchesInOrderAndEscapesWalletName();
    void rejectsChangedChainAnchor();
    void rejectsIncompleteWalletRescan();
    void repeatsDiscoveryAfterScriptExpansion();
    void repeatsDiscoveryAfterMempoolScriptExpansion();
    void catchesUpNewSpendsBeforeCompletion();
    void cancelsWhileNewBlockFiltersCatchUp();
    void replaysMempoolAfterFundingRecovery();
    void rejectsIncompleteMempoolRescan();
};

void WalletHistoryRecoveryBackendTests::requiresPruningAndFilterIndex()
{
    FakeRecoveryNode unpruned;
    unpruned.pruned = false;
    QCOMPARE(unpruned.recover().outcome, Outcome::Disabled);
    QCOMPARE(unpruned.filter_reads, 0);

    FakeRecoveryNode without_filters;
    without_filters.index_enabled = false;
    QCOMPARE(without_filters.recover().outcome, Outcome::Disabled);
    QCOMPARE(without_filters.filter_reads, 0);
}

void WalletHistoryRecoveryBackendTests::waitsForRequiredState()
{
    for (int scenario{0}; scenario < 6; ++scenario) {
        FakeRecoveryNode node;
        switch (scenario) {
        case 0: node.index_synced = false; break;
        case 1: node.index_height = 1; break;
        case 2: node.initial_download = true; break;
        case 3: node.validated = false; break;
        case 4: node.locked = true; break;
        case 5: node.empty_scripts = true; break;
        }
        const auto result{node.recover()};
        QCOMPARE(result.outcome, Outcome::Retry);
        QCOMPARE(node.filter_reads, 0);
        QVERIFY(node.rescanned.empty());
        QVERIFY(node.control->snapshot().phase != Phase::Complete);
    }
}

void WalletHistoryRecoveryBackendTests::honorsWalletUnlockState()
{
    FakeRecoveryNode locked;
    locked.locked = true;
    QCOMPARE(locked.recover().outcome, Outcome::Retry);
    QCOMPARE(locked.control->snapshot().phase, Phase::WaitingForWallet);
    QVERIFY(locked.control->snapshot().needs_unlock);
    QVERIFY(locked.rescanned.empty());

    FakeRecoveryNode unlocked_by_interface;
    unlocked_by_interface.locked = true;
    unlocked_by_interface.control->wallet_unlocked = true;
    QCOMPARE(unlocked_by_interface.recover().outcome, Outcome::Complete);
    QCOMPARE(unlocked_by_interface.rescanned, (std::vector<int>{1, 2}));

    FakeRecoveryNode relocked;
    relocked.locked = true;
    relocked.control->wallet_unlocked = true;
    relocked.rpc_unlock_required = true;
    QCOMPARE(relocked.recover().outcome, Outcome::Retry);
    QVERIFY(relocked.control->snapshot().needs_unlock);
    QVERIFY(relocked.rescanned.empty());
}

void WalletHistoryRecoveryBackendTests::rejectsScriptsExcludedFromBasicFilters()
{
    FakeRecoveryNode node;
    node.unspendable_script = true;
    QCOMPARE(node.recover().outcome, Outcome::Failed);
    QCOMPARE(node.filter_reads, 0);
    QVERIFY(node.rescanned.empty());
}

void WalletHistoryRecoveryBackendTests::retrievesMissingBlocksBeforeRescanning()
{
    FakeRecoveryNode no_peer;
    no_peer.missing_blocks = true;
    QCOMPARE(no_peer.recover().outcome, Outcome::Failed);
    QVERIFY(no_peer.rescanned.empty());
    QVERIFY(no_peer.requested_blocks.empty());

    FakeRecoveryNode with_peer;
    with_peer.missing_blocks = true;
    with_peer.archival_peer = true;
    QCOMPARE(with_peer.recover().outcome, Outcome::Complete);
    QCOMPARE(with_peer.requested_blocks, (std::vector<std::string>{BlockHash(1).GetHex(), BlockHash(2).GetHex()}));
    QCOMPARE(with_peer.rescanned, (std::vector<int>{1, 2}));
}

void WalletHistoryRecoveryBackendTests::rejectsFailedOrIncompleteFilterReads()
{
    for (bool short_range : {false, true}) {
        FakeRecoveryNode node;
        node.filter_read_ok = short_range;
        node.short_filter_range = short_range;
        QCOMPARE(node.recover().outcome, Outcome::Failed);
        QVERIFY(node.rescanned.empty());
        QCOMPARE(node.control->snapshot().phase, Phase::Failed);
    }
}

void WalletHistoryRecoveryBackendTests::cancelsBeforeRescanning()
{
    FakeRecoveryNode before;
    before.control->cancelled = true;
    QCOMPARE(before.recover().outcome, Outcome::Cancelled);
    QCOMPARE(before.rpc_calls, 0);

    FakeRecoveryNode during;
    during.cancel_during_scan = true;
    QCOMPARE(during.recover().outcome, Outcome::Cancelled);
    QVERIFY(during.rescanned.empty());
    QCOMPARE(during.control->snapshot().phase, Phase::Cancelled);
}

void WalletHistoryRecoveryBackendTests::rescansMatchesInOrderAndEscapesWalletName()
{
    FakeRecoveryNode node;
    const auto result{node.recover("wallet /#%")};
    QVERIFY2(result.outcome == Outcome::Complete, result.error.c_str());
    QCOMPARE(node.filter_reads, 1);
    QCOMPARE(node.rescanned, (std::vector<int>{1, 2}));
    QVERIFY(!node.wallet_uris.empty());
    for (const auto& uri : node.wallet_uris) {
        QCOMPARE(uri, std::string{"/wallet/wallet%20%2F%23%25"});
    }
    QCOMPARE(node.control->snapshot().phase, Phase::Complete);
}

void WalletHistoryRecoveryBackendTests::rejectsChangedChainAnchor()
{
    FakeRecoveryNode node;
    node.change_anchor_after_scan = true;
    const auto result{node.recover()};
    QVERIFY(result.outcome != Outcome::Complete);
    QVERIFY(node.rescanned.empty());
    QVERIFY(node.control->snapshot().phase != Phase::Complete);
}

void WalletHistoryRecoveryBackendTests::rejectsIncompleteWalletRescan()
{
    FakeRecoveryNode node;
    node.null_rescan_stop = true;
    QCOMPARE(node.recover().outcome, Outcome::Failed);
    QCOMPARE(node.rescanned, (std::vector<int>{1}));
    QCOMPARE(node.control->snapshot().phase, Phase::Failed);
}

void WalletHistoryRecoveryBackendTests::repeatsDiscoveryAfterScriptExpansion()
{
    FakeRecoveryNode node;
    node.expand_scripts_after_scan = true;
    const auto result{node.recover()};
    QVERIFY2(result.outcome == Outcome::Complete, result.error.c_str());
    QCOMPARE(node.filter_reads, 2);
    QVERIFY(std::find(node.rescanned.begin(), node.rescanned.end(), 2) != node.rescanned.end());
    QCOMPARE(node.control->snapshot().phase, Phase::Complete);
}

void WalletHistoryRecoveryBackendTests::repeatsDiscoveryAfterMempoolScriptExpansion()
{
    FakeRecoveryNode node;
    node.expand_scripts_after_mempool_rescan = true;
    const auto result{node.recover()};
    QVERIFY2(result.outcome == Outcome::Complete, result.error.c_str());
    QCOMPARE(node.filter_ranges, (std::vector<std::pair<int, int>>{{0, 2}, {0, 2}}));
    QCOMPARE(node.rescanned, (std::vector<int>{1, 1, 2}));
    QCOMPARE(node.mempool_rescans, (std::vector<int>{2, 2}));
    QCOMPARE(node.control->snapshot().phase, Phase::Complete);
}

void WalletHistoryRecoveryBackendTests::catchesUpNewSpendsBeforeCompletion()
{
    FakeRecoveryNode node;
    node.extend_chain_during_rescan = true;
    const auto result{node.recover()};
    QVERIFY2(result.outcome == Outcome::Complete, result.error.c_str());
    QCOMPARE(node.rescanned, (std::vector<int>{1, 2, 3}));
    QCOMPARE(node.filter_ranges, (std::vector<std::pair<int, int>>{{0, 2}, {3, 3}}));
    QCOMPARE(node.mempool_rescans, (std::vector<int>{3}));
    QCOMPARE(node.control->snapshot().phase, Phase::Complete);
}

void WalletHistoryRecoveryBackendTests::cancelsWhileNewBlockFiltersCatchUp()
{
    FakeRecoveryNode node;
    node.extend_chain_during_rescan = true;
    node.cancel_during_filter_catchup = true;
    const auto result{node.recover()};
    QCOMPARE(result.outcome, Outcome::Cancelled);
    QCOMPARE(node.rescanned, (std::vector<int>{1, 2}));
    QCOMPARE(node.control->snapshot().phase, Phase::Cancelled);
}

void WalletHistoryRecoveryBackendTests::replaysMempoolAfterFundingRecovery()
{
    FakeRecoveryNode node;
    QVERIFY(!node.mempool_spend_known);
    const auto result{node.recover()};
    QVERIFY2(result.outcome == Outcome::Complete, result.error.c_str());
    QCOMPARE(node.mempool_rescans, (std::vector<int>{2}));
    QVERIFY(node.mempool_spend_known);
    QCOMPARE(node.control->snapshot().phase, Phase::Complete);
}

void WalletHistoryRecoveryBackendTests::rejectsIncompleteMempoolRescan()
{
    FakeRecoveryNode node;
    node.null_mempool_rescan_stop = true;
    const auto result{node.recover()};
    QCOMPARE(result.outcome, Outcome::Failed);
    QCOMPARE(node.rescanned, (std::vector<int>{1, 2}));
    QCOMPARE(node.mempool_rescans, (std::vector<int>{2}));
    QCOMPARE(node.control->snapshot().phase, Phase::Failed);
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(WalletHistoryRecoveryBackendTests)
#else
QTEST_MAIN(WalletHistoryRecoveryBackendTests)
#endif
#include "test_wallethistoryrecoverybackend.moc"
