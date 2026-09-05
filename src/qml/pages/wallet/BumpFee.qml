// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../controls"

Page {
    id: root
    objectName: "walletBumpFeePage"
    required property string sessionId
    required property string transactionId
    property var wallet: null
    readonly property var bump: wallet ? wallet.bump : null
    property string acknowledgedRevision: ""
    Component.onCompleted: {
        if (!wallet) wallet = walletManager.walletBySession(sessionId)
        if (bump) bump.inspect(transactionId)
    }
    Component.onDestruction: if (bump) bump.discardReview()
    signal back()
    Binding { target: root.bump ? root.bump.review : null; property: "displayUnit"; value: optionsModel.displayUnit }
    background: Rectangle { color: Theme.color.background }
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Button { text: qsTr("Back"); onClicked: root.back() }
            Label { text: qsTr("Increase transaction fee"); Layout.fillWidth: true }
        }
    }
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 12
            Label { text: root.transactionId; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
            Label { objectName: "bumpError"; text: root.bump ? root.bump.error : qsTr("Wallet unavailable"); Layout.fillWidth: true; wrapMode: Text.Wrap }
            TextField {
                id: feeRate
                objectName: "bumpFeeRate"
                placeholderText: qsTr("Custom fee rate (sat/vB), optional")
                enabled: !!root.bump && root.bump.eligible && !root.bump.busy
                onTextEdited: { root.bump.discardReview(); confirmation.checked = false }
            }
            Button {
                objectName: "bumpPrepare"
                text: qsTr("Prepare fee increase")
                enabled: !!root.bump && root.bump.eligible && !root.bump.busy
                onClicked: { confirmation.checked = false; root.bump.prepare(feeRate.text) }
            }
            GroupBox {
                visible: !!root.bump && root.bump.review.hasReview
                title: qsTr("Review replacement")
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Label { objectName: "bumpOldFee"; text: root.bump ? qsTr("Old fee: %1").arg(root.bump.review.previousFeeText) : "" }
                    Label { objectName: "bumpNewFee"; text: root.bump ? qsTr("New fee: %1").arg(root.bump.review.feeText) : "" }
                    Label { objectName: "bumpFeeIncrease"; text: root.bump ? qsTr("Increase: %1").arg(root.bump.feeIncrease) : "" }
                    Repeater {
                        model: root.bump ? root.bump.review.outputs : []
                        delegate: Label {
                            required property var modelData
                            text: modelData.amount + " → " + modelData.address
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                    CheckBox {
                        id: confirmation
                        objectName: "bumpConfirmation"
                        text: qsTr("I have reviewed the replacement outputs and increased fee.")
                        enabled: !!root.bump && root.bump.canSubmit
                        onToggled: root.acknowledgedRevision = checked ? String(root.bump.revision) : ""
                    }
                    TextField {
                        id: password
                        objectName: "bumpPassphrase"
                        placeholderText: qsTr("Wallet password, if locked")
                        echoMode: TextInput.Password
                        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                        enabled: !!root.bump && !root.bump.busy
                        onVisibleChanged: if (!visible) clear()
                        Component.onDestruction: clear()
                    }
                    Button {
                        objectName: "bumpSubmit"
                        text: qsTr("Sign and submit replacement")
                        enabled: !!root.bump && root.bump.canSubmit && confirmation.checked && root.acknowledgedRevision === String(root.bump.revision)
                        onClicked: { const secret = password.text; password.clear(); root.bump.submit(secret) }
                    }
                }
            }
            Label { objectName: "bumpAccepted"; text: qsTr("The replacement was accepted by Core."); visible: !!root.bump && root.bump.accepted }
            Label { text: root.bump ? root.bump.replacementTransactionId : ""; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
        }
    }
}
