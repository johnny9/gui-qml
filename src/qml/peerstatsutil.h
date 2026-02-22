// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PEERSTATSUTIL_H
#define BITCOIN_QML_PEERSTATSUTIL_H

#include <net.h>

#include <QString>

#include <chrono>

namespace PeerStatsUtil {

QString ConnectionTypeToQString(ConnectionType conn_type, bool prepend_direction);
QString FormatDurationStr(std::chrono::seconds dur);
QString FormatServicesStr(quint64 mask);
QString FormatPingTime(std::chrono::microseconds ping_time);
QString FormatTimeOffset(int64_t time_offset);
QString FormatBytes(uint64_t bytes);

} // namespace PeerStatsUtil

#endif // BITCOIN_QML_PEERSTATSUTIL_H
