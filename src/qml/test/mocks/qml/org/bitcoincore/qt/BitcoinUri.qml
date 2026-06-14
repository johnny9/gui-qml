// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

pragma Singleton
import QtQuick 2.15

QtObject {
    function parseBitcoinUri(uriText) {
        if (!uriText || !uriText.startsWith("bitcoin:")) {
            return { success: false, error: "Not a valid Bitcoin payment URI." }
        }

        if (uriText.startsWith("bitcoin://")) {
            return { success: false, error: "'bitcoin://' is not a valid URI. Use 'bitcoin:' instead." }
        }

        const payload = uriText.slice("bitcoin:".length)
        const separator = payload.indexOf("?")
        const address = separator === -1 ? payload : payload.slice(0, separator)
        const query = separator === -1 ? "" : payload.slice(separator + 1)
        const result = {
            success: address.length > 0,
            error: address.length > 0 ? "" : "Not a valid Bitcoin payment URI.",
            address: address,
            amountSats: 0,
            hasAmount: false,
            label: "",
            hasLabel: false,
            uriMessage: "",
            hasMessage: false,
        }

        if (!result.success) {
            return result
        }

        for (const item of query.split("&")) {
            if (!item)
                continue
            const parts = item.split("=")
            const key = decodeURIComponent(parts[0])
            const value = decodeURIComponent(parts.length > 1 ? parts.slice(1).join("=") : "")
            if (key === "amount") {
                const pieces = value.split(".")
                const whole = pieces[0] ? Number(pieces[0]) : 0
                const fraction = (pieces.length > 1 ? pieces[1] : "").slice(0, 8).padEnd(8, "0")
                result.amountSats = whole * 100000000 + Number(fraction || "0")
                result.hasAmount = true
            } else if (key === "label") {
                result.label = value
                result.hasLabel = true
            } else if (key === "message") {
                result.uriMessage = value
                result.hasMessage = true
            }
        }

        return result
    }

    function parseBitcoinUriFromFile(_sourcePath) {
        return { success: false, error: "File parsing is unavailable in QML unit tests." }
    }
}
