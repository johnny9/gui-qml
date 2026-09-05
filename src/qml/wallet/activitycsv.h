// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_ACTIVITYCSV_H
#define BITCOIN_QML_WALLET_ACTIVITYCSV_H
#include <QByteArray>
#include <QVariantMap>
#include <QVector>
/** Value-only snapshot, with monetary amounts distinct from requested amounts. */
QByteArray SerializeActivityCsv(const QVector<QVariantMap>& rows, int display_unit = 3);
bool WriteActivityCsv(const QString& path, const QVector<QVariantMap>& rows, QString& error, int display_unit = 3);
#endif // BITCOIN_QML_WALLET_ACTIVITYCSV_H
