// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BACKUPPOLICYIO_H
#define BITCOIN_QML_MODELS_BACKUPPOLICYIO_H

#include <qml/models/backuppolicy.h>

#include <functional>
#include <mutex>
#include <string>

class QIODevice;

namespace backup {
/** Shared by the application's wallets; no wallet mutex is held during consent. */
struct InstallCoordinator { std::mutex mutex; };
using Producer = std::function<bool(const std::string&)>;
struct FileVersion { std::string digest; Error error{Error::None}; };
FileVersion ReadVersion(QIODevice& device);
Proposal Inspect(const std::string& raw, const std::string& wallet_name);
WriteResult Write(const Proposal& approved, bool destination_approved, const Producer& producer, InstallCoordinator& coordinator);
} // namespace backup

#endif // BITCOIN_QML_MODELS_BACKUPPOLICYIO_H
