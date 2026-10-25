// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallethistoryrecoverybackend.h>

#include <blockfilter.h>
#include <chain.h>
#include <index/blockfilterindex.h>
#include <interfaces/node.h>
#include <node/context.h>
#include <rpc/protocol.h>
#include <rpc/util.h>
#include <script/script.h>
#include <script/signingprovider.h>
#include <sync.h>
#include <validation.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <utility>

namespace wallet_history {
namespace {
constexpr int FILTER_BATCH_SIZE{256};
constexpr int DESCRIPTOR_BATCH_SIZE{64};
constexpr int MAX_DESCRIPTOR_PASSES{8};
constexpr std::size_t MAX_DOWNLOAD_PEERS{4};
constexpr auto DOWNLOAD_TIMEOUT{std::chrono::seconds{15}};
constexpr auto DOWNLOAD_POLL_INTERVAL{std::chrono::milliseconds{100}};
constexpr auto FILTER_SYNC_TIMEOUT{std::chrono::seconds{30}};

struct Cancelled {};

class RpcFailure : public std::runtime_error
{
public:
    const int code;
    RpcFailure(int code_in, const std::string& message) : std::runtime_error(message), code{code_in} {}
};

template <typename... Args>
UniValue Params(Args&&... args)
{
    UniValue params{UniValue::VARR};
    (params.push_back(UniValue{std::forward<Args>(args)}), ...);
    return params;
}

UniValue Call(const Backend& backend, const std::string& command, const UniValue& params = UniValue{UniValue::VARR}, const std::string& uri = {})
{
    try {
        return backend.rpc(command, params, uri);
    } catch (const UniValue& error) {
        throw RpcFailure{error["code"].getInt<int>(), error["message"].get_str()};
    }
}

void CheckCancelled(const Control& control)
{
    if (control.cancelled.load()) throw Cancelled{};
}

std::string WalletUri(const std::string& name)
{
    constexpr char HEX[]{"0123456789ABCDEF"};
    std::string uri{"/wallet/"};
    for (const unsigned char character : name) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.' || character == '~') {
            uri += character;
        } else {
            uri += '%';
            uri += HEX[character >> 4];
            uri += HEX[character & 15];
        }
    }
    return uri;
}

Result Finish(const std::shared_ptr<Control>& control, Outcome outcome, Phase phase, std::string error = {}, bool needs_unlock = false)
{
    auto progress = control->snapshot();
    progress.phase = phase;
    progress.detail = error;
    progress.needs_unlock = needs_unlock;
    if (phase == Phase::Complete) {
        progress.current = 1;
        progress.total = 1;
    }
    control->update(std::move(progress));
    return {outcome, std::move(error)};
}

std::vector<UniValue> ReadDescriptors(const Backend& backend, const std::string& uri)
{
    const auto wallet_descriptors{Call(backend, "listdescriptors", Params(false), uri)};
    std::vector<UniValue> descriptors;
    for (const auto& entry : wallet_descriptors["descriptors"].getValues()) {
        UniValue descriptor{UniValue::VOBJ};
        descriptor.pushKV("desc", entry["desc"].get_str());
        if (entry.exists("range")) descriptor.pushKV("range", entry["range"]);
        descriptors.push_back(std::move(descriptor));
    }
    std::sort(descriptors.begin(), descriptors.end(), [](const auto& first, const auto& second) {
        return first.write() < second.write();
    });
    return descriptors;
}

bool SameDescriptors(const std::vector<UniValue>& first, const std::vector<UniValue>& second)
{
    return first.size() == second.size() && std::equal(first.begin(), first.end(), second.begin(), [](const auto& a, const auto& b) {
        return a.write() == b.write();
    });
}

GCSFilter::ElementSet DescriptorScripts(const std::vector<UniValue>& descriptors, const Control& control)
{
    GCSFilter::ElementSet scripts;
    for (const auto& descriptor : descriptors) {
        CheckCancelled(control);
        auto add_scripts = [&](const UniValue& scan_object) {
            FlatSigningProvider provider;
            for (const auto& script : EvalDescriptorStringOrObject(scan_object, provider)) {
                if (script.empty() || script.front() == OP_RETURN) {
                    throw std::runtime_error{"Compact block filters do not cover an unspendable wallet descriptor."};
                }
                scripts.emplace(script.begin(), script.end());
            }
        };
        if (!descriptor.exists("range")) {
            add_scripts(descriptor);
            continue;
        }
        const auto [begin, end]{ParseDescriptorRange(descriptor["range"])};
        for (int64_t offset{begin}; offset <= end; offset += DESCRIPTOR_BATCH_SIZE) {
            CheckCancelled(control);
            UniValue scan_object{UniValue::VOBJ};
            scan_object.pushKV("desc", descriptor["desc"]);
            scan_object.pushKV("range", Params(offset, std::min(end, offset + DESCRIPTOR_BATCH_SIZE - 1)));
            add_scripts(scan_object);
        }
    }
    return scripts;
}

void CheckAnchor(const Backend& backend, int height, const std::string& hash, const Control& control)
{
    CheckCancelled(control);
    if (Call(backend, "getblockhash", Params(height)).get_str() != hash) {
        throw std::runtime_error{"The blockchain changed during recovery. Retry wallet history recovery."};
    }
}

void WaitForFilters(const Backend& backend, int height, const std::string& hash, const std::shared_ptr<Control>& control)
{
    const auto deadline{std::chrono::steady_clock::now() + FILTER_SYNC_TIMEOUT};
    while (true) {
        CheckAnchor(backend, height, hash, *control);
        const auto indexes{Call(backend, "getindexinfo")};
        if (!indexes.exists("basic block filter index")) {
            throw std::runtime_error{"The compact block filter index is no longer available. Wallet history recovery is incomplete."};
        }
        const auto& index{indexes["basic block filter index"]};
        const int indexed_height{index["best_block_height"].getInt<int>()};
        if (index["synced"].get_bool() && indexed_height >= height) return;
        control->update({Phase::WaitingForFilters, indexed_height, height, "Waiting for compact filters for new blocks."});
        CheckCancelled(*control);
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error{"Compact block filters did not catch up with new blocks. Retry wallet history recovery."};
        }
        std::this_thread::sleep_for(DOWNLOAD_POLL_INTERVAL);
    }
}

bool BlockAvailable(const Backend& backend, const std::string& hash)
{
    try {
        Call(backend, "getblock", Params(hash, 0));
        return true;
    } catch (const RpcFailure& error) {
        if (error.code == RPC_MISC_ERROR && std::string{error.what()}.find("pruned") != std::string::npos) return false;
        throw;
    }
}

void FetchBlock(const Backend& backend, const std::string& hash, const Control& control)
{
    CheckCancelled(control);
    if (BlockAvailable(backend, hash)) return;

    std::vector<int64_t> peers;
    const auto peer_info{Call(backend, "getpeerinfo")};
    for (const auto& peer : peer_info.getValues()) {
        const auto& services{peer["servicesnames"].getValues()};
        if (std::any_of(services.begin(), services.end(), [](const UniValue& service) { return service.get_str() == "NETWORK"; })) {
            peers.push_back(peer["id"].getInt<int64_t>());
            if (peers.size() == MAX_DOWNLOAD_PEERS) break;
        }
    }
    if (peers.empty()) {
        throw std::runtime_error{"No connected peer can supply historical blocks. Connect to the network and retry wallet history recovery."};
    }

    for (const auto peer : peers) {
        CheckCancelled(control);
        try {
            Call(backend, "getblockfrompeer", Params(hash, peer));
        } catch (const RpcFailure&) {
            if (BlockAvailable(backend, hash)) return;
            continue;
        }
        const auto deadline{std::chrono::steady_clock::now() + DOWNLOAD_TIMEOUT};
        while (std::chrono::steady_clock::now() < deadline) {
            CheckCancelled(control);
            if (BlockAvailable(backend, hash)) return;
            std::this_thread::sleep_for(DOWNLOAD_POLL_INTERVAL);
        }
    }
    throw std::runtime_error{"Historical block download did not complete. Check the connection and retry wallet history recovery."};
}

Result Run(const Backend& backend, const std::string& wallet_name, const std::shared_ptr<Control>& control)
{
    CheckCancelled(*control);
    control->update({Phase::Checking});
    const std::string uri{WalletUri(wallet_name)};
    const auto chain{Call(backend, "getblockchaininfo")};
    if (!chain["pruned"].get_bool()) return Finish(control, Outcome::Disabled, Phase::Disabled);
    const auto indexes{Call(backend, "getindexinfo")};
    if (!indexes.exists("basic block filter index")) return Finish(control, Outcome::Disabled, Phase::Disabled);

    const auto wallet_info{Call(backend, "getwalletinfo", Params(), uri)};
    if (!wallet_info["descriptors"].get_bool()) {
        return Finish(control, Outcome::Disabled, Phase::Disabled);
    }
    if (chain["initialblockdownload"].get_bool()) {
        return Finish(control, Outcome::Retry, Phase::WaitingForNode, "Waiting for the node to finish synchronizing.");
    }
    const auto chainstates{Call(backend, "getchainstates")};
    const auto& states{chainstates["chainstates"].getValues()};
    if (states.empty() || !states.back()["validated"].get_bool()) {
        return Finish(control, Outcome::Retry, Phase::WaitingForNode, "Waiting for background blockchain validation.");
    }
    int end_height{chain["blocks"].getInt<int>()};
    const auto& index{indexes["basic block filter index"]};
    if (!index["synced"].get_bool() || index["best_block_height"].getInt<int>() < end_height) {
        return Finish(control, Outcome::Retry, Phase::WaitingForFilters, "Waiting for compact block filters.");
    }
    if (!control->wallet_unlocked && wallet_info.exists("unlocked_until") && wallet_info["unlocked_until"].getInt<int64_t>() == 0) {
        return Finish(control, Outcome::Retry, Phase::WaitingForWallet, "Unlock the wallet to recover its history.", true);
    }
    if (wallet_info["scanning"].isObject() || wallet_info["scanning"].isTrue()) {
        return Finish(control, Outcome::Retry, Phase::WaitingForWallet, "Waiting for the current wallet scan.");
    }
    std::string anchor{Call(backend, "getblockhash", Params(end_height)).get_str()};
    auto descriptors{ReadDescriptors(backend, uri)};
    if (descriptors.empty()) {
        return Finish(control, Outcome::Retry, Phase::WaitingForWallet, "Waiting for wallet descriptors.");
    }

    int start_height{0};
    int descriptor_passes{1};
    while (true) {
        const auto scripts{DescriptorScripts(descriptors, *control)};
        std::vector<std::pair<int, std::string>> candidates;
        const int blocks_to_scan{end_height - start_height + 1};
        control->update({Phase::Scanning, 0, blocks_to_scan});
        for (int start{start_height}; start <= end_height;) {
            CheckAnchor(backend, end_height, anchor, *control);
            const int last{static_cast<int>(std::min<int64_t>(end_height, int64_t{start} + FILTER_BATCH_SIZE - 1))};
            std::vector<BlockFilter> filters;
            if (!backend.read_filters(start, last, filters) || filters.size() != static_cast<std::size_t>(last - start + 1)) {
                throw std::runtime_error{"Compact block filters could not be read. Wallet history recovery is incomplete."};
            }
            for (std::size_t offset{0}; offset < filters.size(); ++offset) {
                CheckCancelled(*control);
                const auto& filter{filters[offset]};
                if (filter.GetFilterType() != BlockFilterType::BASIC) {
                    throw std::runtime_error{"Unexpected block filter type during wallet history recovery."};
                }
                if (filter.GetFilter().MatchAny(scripts)) {
                    candidates.emplace_back(start + static_cast<int>(offset), filter.GetBlockHash().GetHex());
                }
            }
            CheckAnchor(backend, end_height, anchor, *control);
            control->update({Phase::Scanning, last - start_height + 1, blocks_to_scan});
            if (last == end_height) break;
            start = last + 1;
        }

        const int total{static_cast<int>(candidates.size())};
        for (int position{0}; position < total; ++position) {
            const auto& [height, hash]{candidates[position]};
            CheckAnchor(backend, end_height, anchor, *control);
            CheckAnchor(backend, height, hash, *control);
            for (int attempt{0}; attempt < 2; ++attempt) {
                control->update({Phase::Downloading, position, total});
                FetchBlock(backend, hash, *control);
                CheckAnchor(backend, end_height, anchor, *control);
                CheckAnchor(backend, height, hash, *control);
                control->update({Phase::Rescanning, position, total});
                try {
                    const auto scanned{Call(backend, "rescanblockchain", Params(height, height), uri)};
                    if (!scanned["start_height"].isNum() || !scanned["stop_height"].isNum() ||
                        scanned["start_height"].getInt<int>() != height || scanned["stop_height"].getInt<int>() != height) {
                        throw std::runtime_error{"The wallet did not finish scanning a historical block. Retry wallet history recovery."};
                    }
                    break;
                } catch (const RpcFailure& error) {
                    if (attempt == 0 && std::string{error.what()}.find("pruned") != std::string::npos) continue;
                    throw;
                }
            }
            CheckAnchor(backend, end_height, anchor, *control);
            control->update({Phase::Rescanning, position + 1, total});
        }

        CheckAnchor(backend, end_height, anchor, *control);
        auto updated_descriptors{ReadDescriptors(backend, uri)};
        auto current_chain{Call(backend, "getblockchaininfo")};
        CheckAnchor(backend, end_height, anchor, *control);
        if (SameDescriptors(descriptors, updated_descriptors) && current_chain["blocks"].getInt<int>() == end_height) {
            control->update({Phase::Rescanning, 0, 1, "Checking current wallet activity."});
            FetchBlock(backend, anchor, *control);
            CheckAnchor(backend, end_height, anchor, *control);
            const auto scanned{Call(backend, "rescanblockchain", Params(end_height), uri)};
            if (!scanned["start_height"].isNum() || !scanned["stop_height"].isNum() ||
                scanned["start_height"].getInt<int>() != end_height || scanned["stop_height"].getInt<int>() < end_height) {
                throw std::runtime_error{"The wallet did not finish checking current activity. Retry wallet history recovery."};
            }
            updated_descriptors = ReadDescriptors(backend, uri);
            current_chain = Call(backend, "getblockchaininfo");
            CheckAnchor(backend, end_height, anchor, *control);
        }
        if (updated_descriptors.empty()) {
            throw std::runtime_error{"Wallet descriptors disappeared during recovery. Retry wallet history recovery."};
        }
        const bool descriptors_changed{!SameDescriptors(descriptors, updated_descriptors)};
        const int current_height{current_chain["blocks"].getInt<int>()};
        if (current_height < end_height) {
            throw std::runtime_error{"The blockchain changed during recovery. Retry wallet history recovery."};
        }
        if (!descriptors_changed && current_height == end_height) {
            CheckCancelled(*control);
            return Finish(control, Outcome::Complete, Phase::Complete);
        }
        if (descriptors_changed) {
            if (++descriptor_passes > MAX_DESCRIPTOR_PASSES) {
                throw std::runtime_error{"The wallet address range kept expanding. Retry recovery to scan the additional addresses."};
            }
            descriptors = std::move(updated_descriptors);
            start_height = 0;
        } else {
            start_height = end_height + 1;
        }
        end_height = current_height;
        anchor = current_chain["bestblockhash"].get_str();
        WaitForFilters(backend, end_height, anchor, control);
    }
}
} // namespace

void Control::update(Progress value)
{
    const std::lock_guard lock{mutex};
    progress = std::move(value);
}

Progress Control::snapshot() const
{
    const std::lock_guard lock{mutex};
    return progress;
}

Result Recover(const Backend& backend, const std::string& wallet_name, const std::shared_ptr<Control>& control)
{
    try {
        return Run(backend, wallet_name, control);
    } catch (const Cancelled&) {
        return Finish(control, Outcome::Cancelled, Phase::Cancelled);
    } catch (const RpcFailure& error) {
        if (error.code == RPC_WALLET_UNLOCK_NEEDED) {
            return Finish(control, Outcome::Retry, Phase::WaitingForWallet, "Unlock the wallet to recover its history.", true);
        }
        if (error.code == RPC_WALLET_ERROR && std::string{error.what()}.find("currently rescanning") != std::string::npos) {
            return Finish(control, Outcome::Retry, Phase::WaitingForWallet, "Waiting for the current wallet scan.");
        }
        return Finish(control, Outcome::Failed, Phase::Failed, error.what());
    } catch (const UniValue& error) {
        return Finish(control, Outcome::Failed, Phase::Failed, error["message"].get_str());
    } catch (const std::exception& error) {
        return Finish(control, Outcome::Failed, Phase::Failed, error.what());
    }
}

Result Recover(interfaces::Node& node, const std::string& wallet_name, const std::shared_ptr<Control>& control)
{
    Backend backend;
    backend.rpc = [&node](const std::string& command, const UniValue& params, const std::string& uri) {
        return node.executeRpc(command, params, uri);
    };
    backend.read_filters = [&node](int start, int end, std::vector<BlockFilter>& filters) {
        auto* context{node.context()};
        auto* index{GetBlockFilterIndex(BlockFilterType::BASIC)};
        if (!context || !context->chainman || !index) return false;
        auto& chainman{*context->chainman};
        const CBlockIndex* stop;
        {
            LOCK(cs_main);
            if (chainman.ActiveChainstate().m_assumeutxo != Assumeutxo::VALIDATED) return false;
            stop = chainman.ActiveChain()[end];
            if (!stop) return false;
            const auto summary{index->GetSummary()};
            const auto* indexed{chainman.m_blockman.LookupBlockIndex(summary.best_block_hash)};
            if (!summary.synced || !indexed || indexed->GetAncestor(end) != stop) return false;
        }
        return index->LookupFilterRange(start, stop, filters);
    };
    return Recover(backend, wallet_name, control);
}
} // namespace wallet_history
