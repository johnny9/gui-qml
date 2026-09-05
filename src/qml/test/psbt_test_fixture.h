// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_PSBT_TEST_FIXTURE_H
#define BITCOIN_QML_TEST_PSBT_TEST_FIXTURE_H

#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <outputtype.h>
#include <univalue.h>
#include <util/moneystr.h>
#include <QByteArray>

/** RPC prepares external data; signing/import/submission under test use models. */
inline QByteArray FundedPsbt(interfaces::Node& node, interfaces::Wallet& wallet, bool signed_psbt = false, bool multiple_outputs = false, CAmount amount = COIN)
{
    UniValue outputs(UniValue::VOBJ);
    outputs.pushKV(EncodeDestination(WitnessV0KeyHash(uint160{})), UniValue(UniValue::VNUM, FormatMoney(amount)));
    if (multiple_outputs) outputs.pushKV(EncodeDestination(PKHash(uint160{})), 2);
    UniValue options(UniValue::VOBJ);
    options.pushKV("fee_rate", 2);
    UniValue args(UniValue::VARR);
    args.push_back(UniValue(UniValue::VARR));
    args.push_back(outputs);
    args.push_back(0);
    args.push_back(options);
    args.push_back(true);
    const std::string uri = "/wallet/" + wallet.getWalletName();
    auto result = node.executeRpc("walletcreatefundedpsbt", args, uri);
    std::string psbt = result.find_value("psbt").get_str();
    if (signed_psbt) {
        UniValue sign(UniValue::VARR);
        sign.push_back(psbt);
        result = node.executeRpc("walletprocesspsbt", sign, uri);
        if (!result.find_value("complete").get_bool()) throw std::runtime_error("PSBT fixture signing failed");
        psbt = result.find_value("psbt").get_str();
    }
    return QByteArray::fromBase64(QByteArray::fromStdString(psbt));
}

#endif // BITCOIN_QML_TEST_PSBT_TEST_FIXTURE_H
