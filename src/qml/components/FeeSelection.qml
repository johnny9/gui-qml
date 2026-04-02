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
    readonly property int optionSpacing: 8
    readonly property int detailsSpacing: 4
    readonly property int selectionColumnWidth: 20
    readonly property int estimateColumnWidth: Math.ceil(estimateFontMetrics.advanceWidth("0.00000000 ₿"))

    signal feeChanged(int target)

    spacing: 16

    FontMetrics {
        id: estimateFontMetrics
        font.family: "Inter"
        font.styleName: "Regular"
        font.pixelSize: 18
        font.features: { "tnum": 1 }
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
        font.features: { "tnum": 1 }
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
        width: Math.ceil(Math.max(dropDownButton.implicitWidth + 24, feeList.maxItemImplicitWidth + leftPadding + rightPadding))
        height: Math.min(feeList.implicitHeight + topPadding + bottomPadding, 300)
        x: dropDownButton.x + dropDownButton.width - width
        y: feePopup.parent.height + 6
        padding: 6

        background: Rectangle {
            color: Theme.color.background
            radius: 6
            border.color: Theme.color.neutral4
        }

        contentItem: Column {
            id: feeList
            objectName: "feeSelectionList"
            readonly property int maxItemImplicitWidth: {
                const feeEstimateRevision = root.walletModel ? root.walletModel.feeEstimateRevision : 0
                return Math.ceil(Math.max(
                    feeOptionsRepeater.itemAt(0) ? feeOptionsRepeater.itemAt(0).implicitWidth : 0,
                    feeOptionsRepeater.itemAt(1) ? feeOptionsRepeater.itemAt(1).implicitWidth : 0,
                    feeOptionsRepeater.itemAt(2) ? feeOptionsRepeater.itemAt(2).implicitWidth : 0))
            }
            width: feePopup.availableWidth
            spacing: 0

            function itemAtIndex(index) {
                return feeOptionsRepeater.itemAt(index)
            }

            Repeater {
                id: feeOptionsRepeater
                model: feeModel

                delegate: ItemDelegate {
                    id: delegate
                    objectName: "feeSelectionOption" + index
                    required property string feeLabel
                    required property string feeDuration
                    required property int index
                    required property int target

                    implicitWidth: delegateContent.implicitWidth + leftPadding + rightPadding
                    width: feeList.width
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
                        id: delegateContent
                        implicitWidth: selectionSlot.width
                            + root.optionSpacing
                            + feeDetails.implicitWidth
                            + (estimateText.visible ? root.optionSpacing + root.estimateColumnWidth : 0)
                        implicitHeight: Math.max(selectionSlot.height, feeDetails.implicitHeight, estimateText.implicitHeight)

                        Item {
                            id: selectionSlot
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: root.selectionColumnWidth
                            height: 20

                            CoreText {
                                anchors.centerIn: parent
                                visible: delegate.index === root.selectedIndex
                                text: "\u2713"
                                font.pixelSize: 18
                                color: Theme.color.orange
                            }
                        }

                        Row {
                            id: feeDetails
                            anchors.left: selectionSlot.right
                            anchors.leftMargin: root.optionSpacing
                            anchors.right: estimateText.visible ? estimateText.left : parent.right
                            anchors.rightMargin: estimateText.visible ? root.optionSpacing : 0
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: root.detailsSpacing

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
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            visible: text.length > 0
                            width: visible ? root.estimateColumnWidth : 0
                            horizontalAlignment: Text.AlignRight
                            text: root.walletModel
                                ? (root.walletModel.feeEstimateRevision, root.walletModel.estimatedFeeForTarget(target))
                                : ""
                            font.pixelSize: 18
                            font.features: { "tnum": 1 }
                            color: Theme.color.neutral7
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
    }

    ListModel {
        id: feeModel
        ListElement { feeLabel: qsTr("High"); feeDuration: qsTr("(~10 mins)"); target: 1 }
        ListElement { feeLabel: qsTr("Default"); feeDuration: qsTr("(~20 mins)"); target: 2 }
        ListElement { feeLabel: qsTr("Low"); feeDuration: qsTr("(~60 mins)"); target: 6 }
    }
}
