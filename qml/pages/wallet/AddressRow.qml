// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

AbstractButton {
    id: root
    objectName: "addressRow"

    required property string address
    required property string label
    required property string category
    required property string displayAmount
    required property bool hasAmount
    required property string scriptType
    required property bool isUsed
    required property bool canEditLabel
    required property int index

    property bool showDivider: true
    property string pendingLabel: root.label
    property AddressListModel addressModel: null
    property TextEditSessionModel editor: addressModel ? addressModel.labelEditor(address) : null
    property string editorError: ""
    property bool commitOnFocusLoss: true

    signal noteDraftEdited(string address, string label)
    signal noteDraftDiscarded(string address)
    signal editLabelRequested(string address, string label)
    signal detailsRequested(string address, string label, string amount, bool hasAmount, string category, string scriptType, bool used)

    function openDetails() {
        root.detailsRequested(root.address, noteField.text, root.displayAmount, root.hasAmount,
            root.category, root.scriptType, root.isUsed)
    }

    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.TabFocus
    padding: 0
    implicitHeight: noteError.visible ? 96 : 76
    height: implicitHeight
    Accessible.name: root.label.length > 0 ? root.label : qsTr("Address details")
    Accessible.description: root.address
    Accessible.role: Accessible.ListItem

    onClicked: root.openDetails()

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Item {
        id: rowContent

        Column {
            id: addressColumn
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.right: detailsButton.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            AddressLabel {
                objectName: "addressRowAddressText"
                width: Math.min(naturalWidth, parent.width)
                address: root.address
                truncateWhenNeeded: true
                textStyle: Theme.text.monoCaption
                leftPadding: 8
                rightPadding: 8
                topPadding: 0
                bottomPadding: 0
            }

            CoreTextField {
                id: noteField
                objectName: "addressRowNoteField"
                property string submittedText: root.label
                width: Math.min(350, parent.width)
                implicitHeight: 30
                text: root.editor && root.editor.available && root.editor.phase !== TextEditSessionModel.Loading ? root.editor.text.text : root.pendingLabel
                placeholderText: root.canEditLabel
                    //: Placeholder shown when an address has no label yet.
                    ? qsTr("Add label...")
                    : qsTr("Change address")
                placeholderTextColor: Theme.color.neutral6
                color: root.canEditLabel ? Theme.color.neutral9 : Theme.color.neutral7
                font: Theme.text.description.font
                readOnly: !root.canEditLabel
                activeFocusOnPress: root.canEditLabel
                activeFocusOnTab: root.canEditLabel
                selectByMouse: root.canEditLabel
                hoverEnabled: root.canEditLabel && AppMode.isDesktop
                leftPadding: 8
                rightPadding: 8
                topPadding: 4
                bottomPadding: 4

                background: Rectangle {
                    radius: 8
                    color: noteField.activeFocus ? Theme.color.neutral2 : "transparent"

                    FocusBorder {
                        objectName: "addressRowNoteFocusBorder"
                        visible: noteField.activeFocus
                        border.color: Theme.color.orange
                        borderRadius: 10
                        topMargin: -2
                        bottomMargin: -2
                        leftMargin: -2
                        rightMargin: -2
                    }
                }

                onActiveFocusChanged: {
                    if (noteField.activeFocus) {
                        noteField.submittedText = root.label
                        if (root.addressModel && (!root.editor || root.editor.phase === TextEditSessionModel.Closed)) {
                            root.editor = root.addressModel.editLabel(root.address)
                            root.editorError = root.editor ? "" : qsTr("The note editor could not be opened. Close another note editor and try again.")
                        }
                    }
                }
                onTextEdited: {
                    if (root.editor) root.editor.text.edit(noteField.text)
                    else root.noteDraftEdited(root.address, noteField.text)
                }
                onAccepted: noteField.focus = false
                onEditingFinished: {
                    if (!root.visible || !root.commitOnFocusLoss) return
                    if (root.editor) { root.editor.save(); return }
                    if (noteField.text !== root.label
                            && noteField.text !== noteField.submittedText) {
                        noteField.submittedText = noteField.text
                        root.editLabelRequested(root.address, noteField.text)
                    }
                }
                Keys.onEscapePressed: function(event) {
                    if (root.editor) root.editor.discard()
                    else { root.noteDraftDiscarded(root.address); noteField.text = root.label }
                    noteField.focus = false
                    event.accepted = true
                }
            }
        }

        CoreText {
            id: noteError
            objectName: "addressRowNoteError"
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            text: root.editor && root.editor.available ? root.editor.error.message : root.editorError
            visible: text.length > 0
            color: Theme.color.red
            font: Theme.text.caption.font
        }

        AbstractButton {
            id: detailsButton
            objectName: "addressRowDetailsButton"
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            anchors.rightMargin: 8
            width: Math.min(parent.width * 0.5, Math.max(80, Math.ceil(amountMetrics.advanceWidth) + 30))
            hoverEnabled: AppMode.isDesktop
            focusPolicy: Qt.TabFocus
            padding: 0
            Accessible.name: qsTr("Address details")
            onClicked: root.openDetails()

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }

            background: null

            contentItem: RowLayout {
                spacing: 8

                BitcoinAmountDisplayLabel {
                    id: amountText
                    objectName: "addressRowAmountText"
                    Layout.fillWidth: true
                    text: root.displayAmount
                    color: root.hasAmount ? Theme.color.green : Theme.color.neutral7
                    font.family: ApplicationModels.optionsModel.moneyFont.family
                    font.weight: ApplicationModels.optionsModel.moneyFont.weight
                    font.pixelSize: Theme.text.description.font.pixelSize
                    lineHeight: Theme.text.description.lineHeight
                    lineHeightMode: Text.FixedHeight
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    wrap: false
                }

                CaretRightIcon {
                    objectName: "addressRowDisclosureIndicator"
                    size: 14
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    Layout.alignment: Qt.AlignVCenter
                    color: Theme.color.neutral7
                }
            }
        }

        TextMetrics {
            id: amountMetrics
            font: amountText.font
            text: root.displayAmount
        }

        Separator {
            objectName: "addressRowDivider"
            visible: root.showDivider
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            color: Theme.color.neutral3
        }
    }

    background: Rectangle {
        radius: 16
        color: "transparent"

        FocusBorder {
            visible: root.visualFocus
            borderRadius: 18
            topMargin: -2
            bottomMargin: -2
            leftMargin: -2
            rightMargin: -2
        }
    }
}
