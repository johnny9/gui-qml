// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/pages/wallet"

TestCase {
    id: testCase
    name: "SendComplete"
    when: windowShown
    width: 900
    height: 700
    Component { id: completeComponent; SendComplete {} }
    function init() { testSetup.failOnWarnings() }
    function test_submission_presentation_data() {
        return [
            {tag: "unknown", state: WalletQmlModel.SubmissionUnknown, title: "Transaction saved", text: "could not be confirmed", accepted: false},
            {tag: "mempool", state: WalletQmlModel.SubmissionMempool, title: "Transaction submitted", text: "confirm within 6 blocks", accepted: true},
            {tag: "confirmed", state: WalletQmlModel.SubmissionConfirmed, title: "Transaction confirmed", text: "is confirmed", accepted: false},
            {tag: "not-in-mempool", state: WalletQmlModel.SubmissionNotInMempool, title: "Transaction saved", text: "not in the mempool", accepted: false},
            {tag: "conflicted", state: WalletQmlModel.SubmissionConflicted, title: "Transaction conflicted", text: "conflicting transaction", accepted: false},
            {tag: "abandoned", state: WalletQmlModel.SubmissionAbandoned, title: "Transaction abandoned", text: "marked abandoned", accepted: false},
            {tag: "node-accepted", state: WalletQmlModel.SubmissionAccepted, title: "Transaction submitted", text: "confirm within 6 blocks", accepted: true}
        ]
    }
    function test_submission_presentation(data) {
        const page = createTemporaryObject(completeComponent, testCase.Window.window.contentItem, {width: width, height: height, submissionState: data.state, targetBlocks: 6})
        verify(page !== null)
        compare(page.titleText, data.title)
        compare(page.accepted, data.accepted)
        const glyph = findChild(page, "sendCompleteStatusGlyph")
        verify(glyph !== null)
        compare(glyph.visible, !data.accepted && data.state !== WalletQmlModel.SubmissionConfirmed)
        verify(page.descriptionText.indexOf(data.text) >= 0)
        if (!data.accepted) verify(page.descriptionText.indexOf("confirm within") === -1)
    }
}
