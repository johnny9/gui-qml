// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/sendsubmit.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <wallet/types.h>

SendSubmissionResult SubmitWalletSend(interfaces::Wallet& wallet,
                                      const ReviewSnapshot& review, const std::vector<SendRecipientValue>& recipients)
{
    SendSubmissionResult result;
    if (!review.transaction) { result.error = QObject::tr("No transaction is prepared."); return result; }
    result.transaction_id = QString::fromStdString(review.transaction->GetHash().ToString());
    if (review.fee > wallet.getDefaultMaxTxFee()) {
        result.error = QObject::tr("The prepared fee exceeds the wallet's maximum fee. Prepare again.");
        return result;
    }
    const auto& transaction = review.transaction;
    if (!wallet.getWalletTx(transaction->GetHash()).tx) {
        std::vector<COutPoint> inputs;
        for (const auto& input : transaction->vin) inputs.push_back(input.prevout);
        const auto coins = wallet.getCoins(inputs);
        if (coins.size() != inputs.size()) {
            result.error = QObject::tr("Prepared inputs are unavailable. Prepare again.");
            return result;
        }
        for (size_t i = 0; i < inputs.size(); ++i) {
            if (coins[i].is_spent || coins[i].depth_in_main_chain < 0 || wallet.isLockedCoin(inputs[i])) {
                result.error = QObject::tr("A prepared input was spent or locked. Check coin selection and prepare again.");
                return result;
            }
        }
        wallet.commitTransaction(transaction, {});
    }
    result.recorded = bool(wallet.getWalletTx(transaction->GetHash()).tx);
    if (!result.recorded) {
        result.error = QObject::tr("The wallet did not record the transaction. Check activity before trying again.");
        return result;
    }
    for (const auto& recipient : recipients) {
        if (!recipient.label.isEmpty()) {
            const auto destination = DecodeDestination(recipient.address.toStdString());
            if (IsValidDestination(destination)) {
                const bool exists = wallet.getAddress(destination, nullptr, nullptr);
                // Sending to an existing receiving address must not turn it
                // into a sending-address entry.
                wallet.setAddressBook(destination, recipient.label.toStdString(),
                    exists ? std::nullopt : std::optional{wallet::AddressPurpose::SEND});
            }
        }
    }
    // Core alone owns wallet broadcast policy, including -walletbroadcast=0
    // and -blocksonly. Never follow its commit with an unconditional broadcast.
    interfaces::WalletTxStatus status{};
    std::vector<std::string> messages, requests;
    bool in_mempool{false};
    int blocks{0};
    wallet.getWalletTxDetails(transaction->GetHash(), status, messages, requests, in_mempool, blocks);
    result.confirmed = status.depth_in_main_chain > 0;
    result.accepted = in_mempool || result.confirmed;
    if (!result.accepted) result.error = QObject::tr("The transaction is recorded but is not in the node mempool. Broadcasting may be disabled, or Core may have rejected it. Check activity and the debug log.");
    return result;
}
