// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETHISTORYRECOVERYBACKEND_H
#define BITCOIN_QML_WALLETHISTORYRECOVERYBACKEND_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <univalue.h>

class BlockFilter;
namespace interfaces {
class Node;
}

namespace wallet_history {
enum class Phase {
    Checking,
    WaitingForNode,
    WaitingForFilters,
    WaitingForWallet,
    Scanning,
    Downloading,
    Rescanning,
    Complete,
    Disabled,
    Failed,
    Cancelled,
};

struct Progress {
    Phase phase{Phase::Checking};
    int current{0};
    int total{0};
    std::string detail{};
    bool needs_unlock{false};
};

struct Control {
    std::atomic<bool> cancelled{false};
    mutable std::mutex mutex;
    Progress progress;
    bool wallet_unlocked{false};

    void update(Progress value);
    Progress snapshot() const;
};

enum class Outcome {
    Complete,
    Disabled,
    Retry,
    Failed,
    Cancelled,
};

struct Result {
    Outcome outcome;
    std::string error;
};

struct Backend {
    std::function<UniValue(const std::string&, const UniValue&, const std::string&)> rpc;
    std::function<bool(int, int, std::vector<BlockFilter>&)> read_filters;
};

Result Recover(const Backend& backend, const std::string& wallet_name, const std::shared_ptr<Control>& control);
Result Recover(interfaces::Node& node, const std::string& wallet_name, const std::shared_ptr<Control>& control);
} // namespace wallet_history

#endif // BITCOIN_QML_WALLETHISTORYRECOVERYBACKEND_H
