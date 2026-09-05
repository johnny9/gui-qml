// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_SENDSUBMIT_H
#define BITCOIN_QML_WALLET_SENDSUBMIT_H

#include <qml/wallet/senddraftsnapshot.h>
#include <qml/wallet/transactionreviewmodel.h>
namespace interfaces { class Wallet; }

struct SendSubmissionResult {
    bool recorded{false};
    bool accepted{false};
    bool confirmed{false};
    bool status_known{true};
    QString transaction_id;
    QString error;
};

//! One exclusive session action; a recorded wallet transaction may still be rejected by the node.
SendSubmissionResult SubmitWalletSend(interfaces::Wallet& wallet,
                                      const ReviewSnapshot& review, const std::vector<SendRecipientValue>& recipients);

#endif // BITCOIN_QML_WALLET_SENDSUBMIT_H
