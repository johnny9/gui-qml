// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"

Page {
    signal back

    id: root
    objectName: "settingsDebugLog"
    background: null

    property int pendingNewLines: 0
    property int displayedLines: 0
    onPendingNewLinesChanged: if (pendingNewLines > 0) displayedLines = pendingNewLines
    property bool duringRefresh: false
    property bool userIsScrolled: false
    property bool scrolledToBottom: false

    Binding { target: nodeModel; property: "debugLogLineNumColor"; value: Theme.color.neutral5 }
    Binding { target: nodeModel; property: "debugLogMessageColor"; value: Theme.color.neutral9 }
    Binding { target: nodeModel; property: "debugLogTimestampColor"; value: Theme.color.neutral5 }

    // Coalesce rapid file writes (e.g. during node startup) into a single refresh.
    Timer {
        id: autoRefreshDebounce
        interval: 500
        repeat: false
        onTriggered: root.refreshLog()
    }

    Connections {
        target: nodeModel
        function onDebugLogChanged() { autoRefreshDebounce.restart() }
        function onNewDebugLogLines(count) {
            if (root.userIsScrolled) root.pendingNewLines += count
        }
    }

    // Debounce timer: delay propagating the search text so the C++ filter
    // does not run synchronously on every key press for large log files.
    Timer {
        id: searchDebounce
        interval: 150
        repeat: false
        onTriggered: nodeModel.debugLogFilter = searchField.text
    }

    Timer {
        interval: 60000
        repeat: true
        running: true
        onTriggered: nodeModel.updateDebugLogTimestamps()
    }

    function refreshLog(isFullLoad) {
        var prevScrolled = root.userIsScrolled
        root.duringRefresh = true
        var atTop = logFlick.contentY === 0
        var savedY = logFlick.contentY
        nodeModel.refreshDebugLog(isFullLoad)
        Qt.callLater(function() {
            logFlick.contentY = atTop ? 0 : savedY
            root.duringRefresh = false
            root.userIsScrolled = atTop ? false : prevScrolled
        })
    }

    header: NavigationBar2 {
        leftItem: NavButton {
            objectName: "debugLogBackButton"
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
        centerItem: Header {
            headerBold: true
            headerSize: 18
            header: qsTr("debug.log")
        }
        rightItem: RowLayout {
            spacing: 0
            NavButton {
                id: refreshBtn
                objectName: "debugLogRefreshButton"
                iconSource: "image://images/refresh"
                iconHeight: 24
                iconWidth: 24
                onClicked: {
                    root.refreshLog()
                    spinAnimation.restart()
                }

                RotationAnimation on rotation {
                    id: spinAnimation
                    from: 0
                    to: 360
                    duration: 600
                    running: false
                    easing.type: Easing.InOutQuad
                }
            }
            NavButton {
                iconSource: "image://images/export"
                iconHeight: 24
                iconWidth: 24
                onClicked: nodeModel.openDebugLogFile()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10

        TextField {
            id: searchField
            objectName: "debugLogSearchField"
            Layout.fillWidth: true
            implicitHeight: 40
            leftPadding: 40
            rightPadding: 12
            font.family: "Inter"
            font.styleName: "Regular"
            font.pixelSize: 15
            color: Theme.color.neutral9
            placeholderTextColor: Theme.color.neutral5
            placeholderText: qsTr("Search...")
            Accessible.name: qsTr("Search debug log")
            Accessible.role: Accessible.EditableText
            onTextChanged: searchDebounce.restart()
            background: Rectangle {
                color: Theme.color.neutral2
                radius: 10
                Icon {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/icons/search"
                    color: Theme.color.neutral5
                    size: 16
                }
            }
        }

        Item {
            Layout.fillWidth: true
            height: 48

            Rectangle {
                id: newEntriesPill
                anchors.centerIn: parent

                visible: opacity > 0
                opacity: (root.pendingNewLines > 0 && root.userIsScrolled) ? 1.0 : 0.0
                Behavior on opacity { NumberAnimation { duration: 150 } }

                width: 16 + arrowText.implicitWidth + 8 + countText.implicitWidth + 24 + closeText.width + 24
                height: 32
                radius: 16

                Behavior on color { ColorAnimation { duration: 150 } }
                color: pressHandler.pressed ? Theme.color.orangeLight2
                    : hoverHandler.hovered ? Theme.color.orangeLight1
                    : Theme.color.orange

                Text {
                    id: arrowText
                    text: "↑"
                    color: "white"
                    font.pixelSize: 15
                    font.family: "Inter"
                    font.bold: true
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    id: countText
                    text: root.displayedLines === 1
                        ? qsTr("1 new entry")
                        : qsTr("%1 new entries").arg(root.displayedLines)
                    color: "white"
                    font.pixelSize: 13
                    font.family: "Inter"
                    anchors.left: arrowText.right
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    id: closeText
                    text: "×"
                    color: "white"
                    font.pixelSize: 20
                    font.bold: true
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: closeArea.containsMouse ? 1.0 : 0.85
                }

                MouseArea {
                    id: closeArea
                    anchors {
                        right: parent.right
                        top: parent.top
                        bottom: parent.bottom
                        rightMargin: 6
                    }
                    width: 32
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.pendingNewLines = 0
                }

                HoverHandler {
                    id: hoverHandler
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    id: pressHandler
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        logFlick.contentY = 0
                        root.pendingNewLines = 0
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Flickable {
                id: logFlick
                objectName: "debugLogListView"
                property int count: nodeModel.debugLogLineCount
                anchors.fill: parent
                contentHeight: logTextEdit.height
                clip: true
                ScrollBar.vertical: ScrollBar {}
                onContentYChanged: {
                    if (!root.duringRefresh) {
                        root.userIsScrolled = contentY > 0
                        root.scrolledToBottom = atYEnd
                        if (contentY <= 0 && root.pendingNewLines > 0) {
                            root.pendingNewLines = 0
                        }
                    }
                }
                onHeightChanged: {
                    if (!root.duringRefresh && root.scrolledToBottom) {
                        root.duringRefresh = true
                        contentY = Math.max(0, contentHeight - height)
                        Qt.callLater(function() { root.duringRefresh = false })
                    }
                }
                onContentHeightChanged: {
                    if (!root.duringRefresh && root.scrolledToBottom) {
                        root.duringRefresh = true
                        contentY = Math.max(0, contentHeight - height)
                        Qt.callLater(function() { root.duringRefresh = false })
                    }
                }

                TextEdit {
                    id: logTextEdit
                    width: logFlick.width
                    text: nodeModel.formattedDebugLog
                    textFormat: TextEdit.RichText
                    font.pixelSize: 11
                    font.family: "monospace"
                    wrapMode: Text.WrapAnywhere
                    readOnly: true
                    selectByMouse: true
                    selectionColor: Theme.color.orange
                    selectedTextColor: "white"
                    bottomPadding: nodeModel.debugLogHasMoreLines ? 60 : 0
                }
            }

            TextButton {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8
                visible: nodeModel.debugLogHasMoreLines && logFlick.atYEnd
                text: qsTr("Load more")
                textSize: 13
                bold: false
                onClicked: {
                    nodeModel.debugLogLoadLimit += 1000
                    root.refreshLog(true)
                }
            }
        }

    }

    Component.onCompleted: {
        nodeModel.debugLogLoadLimit = 1000
        nodeModel.debugLogFilter = ""
        root.refreshLog(true)
    }
}
