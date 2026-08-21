// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Design-time fallback for the version-specific AppSettings.qml embedded by
// CMake. This source file is intentionally not listed in bitcoin_qml.qrc, so
// production builds continue using qml/compat/qt62 or qml/compat/qt65.
import QtQml 2.15

QtObject {}
