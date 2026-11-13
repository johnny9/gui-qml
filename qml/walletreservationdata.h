// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETRESERVATIONDATA_H
#define BITCOIN_QML_WALLETRESERVATIONDATA_H

#include <qml/walletopeningstate.h>

#include <addresstype.h>
#include <consensus/amount.h>

#include <QString>

#include <functional>
#include <memory>

namespace interfaces { class Wallet; }
namespace walletrequests {
struct Draft {
    QString address, label, message, note, address_type;
    CAmount amount{0};
};
/** Plain original-attempt identity. A password question retains no Wallet,
 * Action lease or secret, and cannot become a later view's address intent. */
struct AddressIntent {
    const walletopening::Opening opening;
    const uint64_t token, view, view_revision;
    const Draft draft;
    const bool next, explicit_type, create, select;
};
struct ReservedAddress {
    CTxDestination destination;
    QString address, address_type, label;
    bool persisted{false};
};
struct StoredAddress {
    QString address;
    bool retired{false}, ok{true};
};
/** All callbacks run on Native with the original public Wallet. */
struct ReservationStorage {
    std::function<StoredAddress(interfaces::Wallet&, const QString&)> read;
    std::function<bool(interfaces::Wallet&, const QString&)> write;
    std::function<bool(interfaces::Wallet&, const QString&)> retire;
    std::function<bool(interfaces::Wallet&, const QString&)> clear;
};
ReservationStorage SettingsReservationStorage();
} // namespace walletrequests

#endif // BITCOIN_QML_WALLETRESERVATIONDATA_H
