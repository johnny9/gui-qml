// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETBUMP_H
#define BITCOIN_QML_WALLETBUMP_H

#include <interfaces/wallet.h>
#include <qml/secretinput.h>
#include <primitives/transaction.h>
#include <support/allocators/secure.h>
#include <QString>

#include <memory>
#include <optional>

namespace walletopening { class Session; }

namespace walletbump {
struct Prepared {
    Txid original;
    CMutableTransaction transaction;
    CAmount old_fee{0}, new_fee{0};
    QString old_fee_text, new_fee_text, increase_text, old_txid;
};
enum class Error { None, Invalid, InvalidId, Ineligible, Create, NeedsUnlock, IncorrectPassphrase, Sign, Changed, RelockFailed, Commit, Uncertain };
struct Preparation { Error error{Error::None}; QString message; std::shared_ptr<const Prepared> review; };
struct Commit {
    Error error{Error::None};
    QString message;
    bool committed{false}, relock_failed{false};
    Txid original, txid;
};
Preparation Prepare(interfaces::Wallet& wallet, const QString& txid, unsigned int target,
                    const std::shared_ptr<walletopening::Session>& opening);
Commit Execute(interfaces::Wallet& wallet, const Prepared& review, std::optional<SecureString> passphrase,
               secretinput::Captured captured = {});
} // namespace walletbump

#endif // BITCOIN_QML_WALLETBUMP_H
