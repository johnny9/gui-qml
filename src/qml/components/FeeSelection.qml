// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"
import "../components"

RowLayout {
    id: root
    objectName: "feeSelectionControl"

    property var walletModel: null
    property int currentTarget: 2
    property int selectedIndex: walletModel ? walletModel.feeTargetIndex(currentTarget) : 1
    property string selectedLabel: feeModel.get(root.selectedIndex).feeLabel
    property string selectedDuration: feeModel.get(root.selectedIndex).feeDuration
    property int selectedTarget: feeModel.get(root.selectedIndex).target
    property string selectedEstimate: walletModel
        ? (walletModel.feeEstimateRevision, walletModel.estimatedFeeForTarget(selectedTarget))
        : ""
    readonly property bool hasFeeEstimate: {
        const feeEstimateRevision = walletModel ? walletModel.feeEstimateRevision : 0
        if (!walletModel || feeEstimateRevision < 0) {
            return false
        }

        for (let i = 0; i < feeModel.count; ++i) {
            const option = feeModel.get(i)
            if (walletModel.estimatedFeeForTarget(option.target).length > 0) {
                return true
            }
        }

        return false
    }
    readonly property int estimateColumnWidth: {
        const feeEstimateRevision = walletModel ? walletModel.feeEstimateRevision : 0
        if (!walletModel || feeEstimateRevision < 0) {
            return 0
        }

        let maxWidth = 0
        for (let i = 0; i < feeModel.count; ++i) {
            const option = feeModel.get(i)
            const estimate = walletModel.estimatedFeeForTarget(option.target)
            if (estimate.length > 0) {
                maxWidth = Math.max(maxWidth, estimateFontMetrics.advanceWidth(estimate))
            }
        }

        return Math.ceil(maxWidth)
    }

    signal feeChanged(int target)

    spacing: 16

    FontMetrics {
        id: estimateFontMetrics
        font.pixelSize: 18
    }

    CoreText {
        Layout.preferredWidth: 110
        horizontalAlignment: Text.AlignLeft
        font.pixelSize: 18
        text: qsTr("Fee")
    }

    CoreText {
        id: estimateLabel
        objectName: "feeSelectionEstimateLabel"
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignLeft
        font.pixelSize: 18
        color: Theme.color.neutral7
        text: root.selectedEstimate
    }

    Button {
        id: dropDownButton
        objectName: "feeSelectionDropdownButton"
        hoverEnabled: true
        leftPadding: 10
        rightPadding: 8
        topPadding: 4
        bottomPadding: 4
        implicitHeight: 32

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        onPressed: feePopup.open()

        contentItem: RowLayout {
            spacing: 0

            CoreText {
                text: root.selectedLabel
                font.pixelSize: 18
                color: dropDownButton.enabled ? Theme.color.neutral9 : Theme.color.neutral4
            }

            Item { width: 5 }

            CoreText {
                text: root.selectedDuration
                font.pixelSize: 18
                color: dropDownButton.enabled ? Theme.color.neutral7 : Theme.color.neutral4
            }

            Icon {
                source: "image://images/caret-down-medium-filled"
                Layout.preferredWidth: 30
                size: 30
                color: dropDownButton.enabled ? Theme.color.orange : Theme.color.neutral4
            }
        }

        background: Rectangle {
            id: dropDownButtonBg
            color: dropDownButton.hovered ? Theme.color.neutral2 : Theme.color.background
            radius: 6

            Behavior on color {
                ColorAnimation { duration: 150 }
            }
        }
    }

    Popup {
        id: feePopup
        objectName: "feeSelectionPopup"
        modal: true
        dim: false
        width: root.hasFeeEstimate ? 360 : 280
        height: Math.min(feeModel.count * 44 + 12, 300)
        x: feePopup.parent.width - feePopup.width
        y: feePopup.parent.height + 6
        padding: 6

        background: Rectangle {
            color: Theme.color.background
            radius: 6
            border.color: Theme.color.neutral4
        }

        contentItem: ListView {
            id: feeList
            objectName: "feeSelectionList"
            model: feeModel
            interactive: false
            width: feePopup.availableWidth
            height: contentHeight

            delegate: ItemDelegate {
                id: delegate
                objectName: "feeSelectionOption" + index
                required property string feeLabel
                required property string feeDuration
                required property int index
                required property int target

                width: ListView.view.width
                height: 44
                leftPadding: 12
                rightPadding: 12
                topPadding: 0
                bottomPadding: 0

                background: Item {
                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: Theme.color.neutral2
                        visible: delegate.hovered
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: delegate.index === feeModel.count - 1 ? 0 : 1
                        color: Theme.color.neutral4
                    }
                }

                contentItem: Item {
                    Row {
                        id: feeDetails
                        anchors.left: parent.left
                        anchors.right: estimateText.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4

                        CoreText {
                            text: feeLabel
                            font.pixelSize: 18
                            color: Theme.color.neutral9
                        }

                        CoreText {
                            text: feeDuration
                            font.pixelSize: 18
                            color: Theme.color.neutral7
                        }
                    }

                    CoreText {
                        id: estimateText
                        objectName: "feeSelectionOptionEstimate" + delegate.index
                        anchors.right: selectionIconSlot.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.estimateColumnWidth
                        horizontalAlignment: Text.AlignRight
                        text: root.walletModel
                            ? (root.walletModel.feeEstimateRevision, root.walletModel.estimatedFeeForTarget(target))
                            : ""
                        font.pixelSize: 18
                        color: Theme.color.neutral7
                    }

                    Item {
                        id: selectionIconSlot
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 20
                        height: 20

                        Icon {
                            anchors.centerIn: parent
                            opacity: delegate.index === root.selectedIndex ? 1 : 0
                            source: "image://images/check"
                            color: Theme.color.orange
                            size: 20
                        }
                    }
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }

                onClicked: {
                    root.feeChanged(target)
                    feePopup.close()
                }
            }
        }
    }

    ListModel {
        id: feeModel
        ListElement { feeLabel: qsTr("High"); feeDuration: qsTr("(~10 mins)"); target: 1 }
        ListElement { feeLabel: qsTr("Default"); feeDuration: qsTr("(~20 mins)"); target: 2 }
        ListElement { feeLabel: qsTr("Low"); feeDuration: qsTr("(~60 mins)"); target: 6 }
    }
}
