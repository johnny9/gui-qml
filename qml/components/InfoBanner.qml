// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../controls"

Rectangle {
    id: root

    enum Layout {
        Horizontal,
        Vertical
    }

    property url iconSource: ""
    property string title: ""
    property string titleObjectName: ""
    property string message: ""
    property string messageObjectName: ""
    property string primaryButtonText: ""
    property bool primaryButtonOutlined: false
    property string dismissButtonText: ""
    property bool showsCloseButton: false
    property int bannerLayout: InfoBanner.Layout.Horizontal
    property int contentMargin: 30
    property int horizontalContentMargin: contentMargin
    property int verticalContentMargin: contentMargin
    property int contentSpacing: 15
    property int iconSize: 24
    property int textRightMargin: 0
    property int messageFontPixelSize: 13
    property int messageLineHeight: 0
    property color backgroundColor: Qt.rgba(Theme.color.blue.r, Theme.color.blue.g, Theme.color.blue.b, 0.2)
    property color iconColor: Theme.color.neutral7
    property color titleColor: Theme.color.neutral9
    property color messageColor: Theme.color.neutral7

    signal primaryClicked()
    signal dismissClicked()

    radius: 10
    color: backgroundColor
    implicitHeight: contentLoader.implicitHeight + 2 * verticalContentMargin

    Loader {
        id: contentLoader
        objectName: root.objectName !== "" ? root.objectName + "Content" : ""
        x: root.horizontalContentMargin
        width: Math.max(0, (root.showsCloseButton ? closeButton.x - 8 : root.width - root.horizontalContentMargin) - x)
        anchors.verticalCenter: parent.verticalCenter
        sourceComponent: root.bannerLayout === InfoBanner.Layout.Vertical
            ? verticalContent : horizontalContent
    }

    IconButton {
        id: closeButton
        objectName: root.objectName !== "" ? root.objectName + "CloseButton" : ""
        visible: root.showsCloseButton
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 8
        anchors.rightMargin: 8
        size: 24
        iconSource: "image://images/cross"
        Accessible.name: qsTr("Dismiss")
        Accessible.role: Accessible.Button
        onClicked: root.dismissClicked()
    }

    Component {
        id: horizontalContent
        RowLayout {
            spacing: root.contentSpacing

            Icon {
                visible: root.iconSource != ""
                source: root.iconSource
                color: root.iconColor
                size: root.iconSize
                Layout.preferredWidth: root.iconSize
                Layout.preferredHeight: root.iconSize
            }

            Column {
                id: horizontalText
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
                Layout.rightMargin: root.textRightMargin
                spacing: 4

                CoreText {
                    objectName: root.titleObjectName
                    visible: root.title !== ""
                    text: root.title
                    font.pixelSize: 15
                    bold: true
                    color: root.titleColor
                    horizontalAlignment: Text.AlignLeft
                    width: horizontalText.width
                }

                CoreText {
                    objectName: root.messageObjectName
                    visible: root.message !== ""
                    text: root.message
                    font.pixelSize: root.messageFontPixelSize
                    lineHeightMode: root.messageLineHeight > 0 ? Text.FixedHeight : Text.ProportionalHeight
                    lineHeight: root.messageLineHeight > 0 ? root.messageLineHeight : 1.0
                    color: root.messageColor
                    horizontalAlignment: Text.AlignLeft
                    width: horizontalText.width
                    wrap: true
                }
            }

            OutlineButton {
                objectName: root.objectName !== "" ? root.objectName + "DismissButton" : ""
                visible: root.dismissButtonText !== ""
                text: root.dismissButtonText
                leftPadding: 20
                rightPadding: 20
                onClicked: root.dismissClicked()
            }

            Loader {
                visible: root.primaryButtonText !== ""
                sourceComponent: root.primaryButtonOutlined ? outlinedPrimaryButton : filledPrimaryButton
            }
        }
    }

    Component {
        id: verticalContent
        ColumnLayout {
            spacing: root.contentSpacing

            Icon {
                visible: root.iconSource != ""
                source: root.iconSource
                color: root.iconColor
                size: root.iconSize
                Layout.preferredWidth: root.iconSize
                Layout.preferredHeight: root.iconSize
                Layout.alignment: Qt.AlignHCenter
            }

            CoreText {
                objectName: root.titleObjectName
                visible: root.title !== ""
                text: root.title
                font.pixelSize: 15
                bold: true
                color: root.titleColor
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
            }

            CoreText {
                objectName: root.messageObjectName
                visible: root.message !== ""
                text: root.message
                font.pixelSize: root.messageFontPixelSize
                lineHeightMode: root.messageLineHeight > 0 ? Text.FixedHeight : Text.ProportionalHeight
                lineHeight: root.messageLineHeight > 0 ? root.messageLineHeight : 1.0
                color: root.messageColor
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
                Layout.bottomMargin: 10
                wrap: true
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 15

                OutlineButton {
                    objectName: root.objectName !== "" ? root.objectName + "DismissButton" : ""
                    visible: root.dismissButtonText !== ""
                    text: root.dismissButtonText
                    Layout.maximumWidth: 200
                    onClicked: root.dismissClicked()
                }

                Loader {
                    visible: root.primaryButtonText !== ""
                    Layout.maximumWidth: 200
                    sourceComponent: root.primaryButtonOutlined ? outlinedPrimaryButton : filledPrimaryButton
                }
            }
        }
    }

    Component {
        id: filledPrimaryButton
        ContinueButton {
            objectName: root.objectName !== "" ? root.objectName + "PrimaryButton" : ""
            text: root.primaryButtonText
            leftPadding: root.bannerLayout === InfoBanner.Layout.Vertical ? 30 : 20
            rightPadding: leftPadding
            onClicked: root.primaryClicked()
        }
    }

    Component {
        id: outlinedPrimaryButton
        OutlineButton {
            objectName: root.objectName !== "" ? root.objectName + "PrimaryButton" : ""
            text: root.primaryButtonText
            onClicked: root.primaryClicked()
        }
    }
}
