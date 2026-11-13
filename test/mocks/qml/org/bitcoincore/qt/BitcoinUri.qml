// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

pragma Singleton
import QtQuick 2.15

QtObject {
    id: parser
    property int _sequence: 0
    property bool holdPublications: false
    property var _heldOperations: []
    function releasePublications() {
        holdPublications = false
        const held = _heldOperations
        _heldOperations = []
        for (const operation of held) if (operation) operation.retryPublication()
    }
    property Component _operationFactory: Component { BitcoinUriParseOperation {} }

    function _request(outcome, owner, replace) {
        if (!owner) return null
        if (replace && replace.requestOwner === owner) {
            replace.closeAdmission()
            replace.destroy()
        }
        const operation = _operationFactory.createObject(owner, {requestId: ++_sequence, requestOwner: owner})
        operation.prepare(outcome, holdPublications)
        if (holdPublications) _heldOperations.push(operation)
        return operation
    }
    property var _sourceIds: ({})
    property int _sourceSequence: 0
    function requestParse(text, owner, replace) { return requestProbe(text, {}, owner, replace) }
    function requestProbe(text, context, owner, replace) {
        let outcome
        try { outcome = _parse(text) }
        catch (error) { outcome = {success: false, error: "Not a valid Bitcoin payment URI."} }
        outcome.isPaymentUri = /^bitcoin:/i.test(String(text || "").trim())
        outcome.probeSucceeded = true
        // Fixture-only stable identity; production hashes on Domain.
        if (!_sourceIds[text]) _sourceIds[text] = "fixture-source-" + (++_sourceSequence)
        outcome.sourceId = _sourceIds[text]
        outcome.privateNote = outcome.hasMessage && outcome.uriMessage.length > 0
            ? outcome.uriMessage + (outcome.hasLabel && outcome.label.length > 0 ? " - " + outcome.label : "") : ""
        const field = context.sourceField || ""
        outcome.conflicts = outcome.success && (
            (field !== "address" && !!context.address && context.address !== outcome.address)
            || (field !== "amount" && outcome.hasAmount && !!context.amount && context.amount !== outcome.amountSats)
            || (field !== "label" && !!context.note && !!outcome.privateNote && context.note !== outcome.privateNote))
        outcome.matchesExistingRequest = outcome.success && context.address === outcome.address
            && (!outcome.hasAmount || context.amount === outcome.amountSats)
            && (!outcome.hasLabel || context.requestLabel === outcome.label)
        outcome.pasteProvenance = true
        if (context.edited) {
            const previous = context.previousText || "", current = context.currentText || ""
            outcome.containsPaymentUri = /bitcoin:/i.test(current)
            let prefix = 0, suffix = 0
            while (prefix < Math.min(previous.length, current.length) && previous[prefix] === current[prefix]) prefix++
            while (suffix < Math.min(previous.length, current.length) && previous[previous.length - suffix - 1] === current[current.length - suffix - 1]) suffix++
            const unchanged = current.length - text.length
            const low = Math.max(0, unchanged - suffix), high = Math.min(prefix, unchanged)
            const found = current.indexOf(text, low)
            outcome.pasteProvenance = !!text.length && previous !== current && current.length >= text.length
                && unchanged <= previous.length && low <= high && found >= 0 && found <= high
        }
        return _request(outcome, owner, replace)
    }
    function requestParseFile(path, owner, replace) {
        return _request({success: false, error: "File parsing is unavailable in QML unit tests."}, owner, replace)
    }
    function _parse(uriText) {
        uriText = String(uriText || "").trim()
        if (!/^bitcoin:/i.test(uriText)) {
            return { success: false, error: "Not a valid Bitcoin payment URI." }
        }

        if (/^bitcoin:\/\//i.test(uriText)) {
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

}
