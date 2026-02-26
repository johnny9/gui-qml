// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

StackView {
    property bool vertical: false
    // Expose the deepest active page in nested PageStack hierarchies.
    readonly property var currentLeafItem: resolveCurrentLeafItem(currentItem)
    readonly property string currentLeafObjectName: {
        const leaf = currentLeafItem
        return leaf && leaf.objectName ? leaf.objectName : ""
    }

    function resolveCurrentLeafItem(item) {
        let current = item
        let guard = 0
        while (current && guard < 64) {
            guard += 1
            if (current.currentItem !== undefined &&
                current.depth !== undefined &&
                current.currentItem) {
                current = current.currentItem
                continue
            }
            return current
        }
        return current
    }

    pushEnter: Transition {
        NumberAnimation {
            property: vertical ? "y" : "x"
            from: vertical ? parent.height : parent.width
            to: 0
            duration: 500
            easing.type: Easing.InOutCubic
        }
    }
    pushExit: Transition {
        NumberAnimation {
            property: vertical ? "y" : "x"
            from: 0
            to: vertical ? -parent.height : -parent.width
            duration: 500
            easing.type: Easing.InOutCubic
        }
    }
    popEnter: Transition {
        NumberAnimation {
            property: vertical ? "y" : "x"
            from: vertical ? -parent.height : -parent.width
            to: 0
            duration: 500
            easing.type: Easing.InOutCubic
        }
    }
    popExit: Transition {
        NumberAnimation {
            property: vertical ? "y" : "x"
            from: 0
            to: vertical ? parent.height : parent.width
            duration: 500
            easing.type: Easing.InOutCubic
        }
    }
}
