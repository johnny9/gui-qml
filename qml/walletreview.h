// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETREVIEW_H
#define BITCOIN_QML_WALLETREVIEW_H

#include <qml/models/psbtfee.h>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace walletreview {
struct Recipient {
    QString address, label;
    CAmount amount{0};
    bool payment_request{false};
};
struct Snapshot {
    CTransactionRef transaction;
    QList<Recipient> recipients;
    QMap<QString, QString> notes;
    QVariantList presentation;
    QVariantMap flow;
    QSet<QString> destinations;
    std::shared_ptr<const psbtfee::Analysis> proof;
    CAmount amount{0}, fee{0};
    int target_blocks{0}, display_unit{0};
    QString fee_rate;
    bool can_send{false}, sweeps{false};
    std::optional<CAmount> maximum;
};
QVariantList FormatRecipients(const QList<Recipient>& recipients, int display_unit);
} // namespace walletreview

#endif // BITCOIN_QML_WALLETREVIEW_H
