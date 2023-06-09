// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"

InformationPage {
    navRightDetail: NavButton {
        iconSource: "image://images/info"
        iconHeight: 24
        iconWidth: 24
        iconColor: Theme.color.neutral0
        iconBackground: Rectangle {
            radius: 12
            color: Theme.color.neutral9
        }
    }
    bannerItem: Image {
        source: "image://images/notifications-dark"
        sourceSize.width: 200
        sourceSize.height: 200
    }
    bold: true
    headerText: qsTr("Enable notifications")
    description: qsTr("Allow the application to continue to to load and verify " +
           "transactions, even when you are not actively using it.\n\n" +
           "Quickly see the status in a permanent system notification.")
    buttonText: qsTr("Enable")
    onContinueClicked: {
        optionsModel.notificationsEnabled = true
        console.log("NOTIFICATIONS set to true")
    }
}
