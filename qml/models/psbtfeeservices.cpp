// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/psbtfeeservices.h>

#include <coins.h>
#include <interfaces/node.h>
#include <univalue.h>
#include <util/moneystr.h>
#include <util/strencodings.h>

psbtfee::Lookup psbtfee::NodeLookup(interfaces::Node* node)
{
    if (!node) return {};
    return [node](const COutPoint& outpoint) -> std::optional<CTxOut> {
        const auto coin = node->getUnspentOutput(outpoint);
        if (coin && !coin->IsSpent()) return coin->out;
        // The pinned Node coin API reads confirmed chain state only. The public
        // RPC interface also authenticates unconfirmed outputs in the mempool,
        // without accessing the internal node context or running work on GUI.
        UniValue parameters{UniValue::VARR};
        parameters.push_back(outpoint.hash.ToString());
        parameters.push_back(outpoint.n);
        parameters.push_back(true);
        const auto output = node->executeRpc("gettxout", parameters, "");
        if (output.isNull()) return std::nullopt;
        const auto amount = ParseMoney(output.find_value("value").getValStr());
        const auto script = TryParseHex<unsigned char>(output.find_value("scriptPubKey").find_value("hex").get_str());
        if (!amount || !script) return std::nullopt;
        return CTxOut{*amount, CScript{script->begin(), script->end()}};
    };
}

psbtfee::Resolution psbtfee::Resolve(const PartiallySignedTransaction& document, const Analysis& seed, const Lookup& lookup)
{
    auto verified = seed.verified;
    auto proof = Analyze(document, verified);
    if (proof.state == State::Missing) {
        for (const auto& input : document.inputs) {
            const auto outpoint = input.GetOutPoint();
            if (input.non_witness_utxo || verified.contains(outpoint)) continue;
            if (auto output = lookup(outpoint)) verified.emplace(outpoint, std::move(*output));
        }
        proof = Analyze(document, verified);
    }
    bool complete{false};
    if (proof.state == State::Known) {
        auto candidate = document;
        FillMissing(candidate, proof);
        complete = FinalizePSBT(candidate);
    }
    return {std::make_shared<const Analysis>(std::move(proof)), complete, false};
}

execution::Admission psbtfee::Request(const Services& services, std::shared_ptr<const PartiallySignedTransaction> document,
                                     std::shared_ptr<const Analysis> seed, std::function<void(Resolution)> publish)
{
    if (!services.scheduler || !services.lookup || !services.post) return {{}, execution::Rejection::Transport};
    auto post = [transport = services.post, publish = std::move(publish)](Resolution resolution) {
        auto value = std::make_shared<Resolution>(std::move(resolution));
        transport([publish, value] { publish(std::move(*value)); });
    };
    return execution::Submit(*services.scheduler,
        [document = std::move(document), seed = std::move(seed), lookup = services.lookup] {
            return Resolve(*document, *seed, lookup);
        },
        post, [post](std::exception_ptr) { post({{}, false, true}); });
}
