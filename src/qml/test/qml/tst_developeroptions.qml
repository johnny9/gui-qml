// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2

// Tests the binding logic used by the debugLogSetting in DeveloperOptions.qml:
//
//   errorText: nodeModel.debugLogOpenError
//   showErrorText: nodeModel.debugLogOpenError.length > 0
//
// We exercise this via a plain QtObject that mirrors the same bindings,
// avoiding the org.bitcoincore.qt C++ plugin dependency (Theme / AppMode)
// that Setting.qml requires and that is not available in the test runner.
TestCase {
    name: "DebugLogSetting"

    Component {
        id: mockSetting
        QtObject {
            // Simulates nodeModel.debugLogOpenError
            property string debugLogOpenError: ""

            // Bindings copied from DeveloperOptions.qml
            property string errorText: debugLogOpenError
            property bool showErrorText: debugLogOpenError.length > 0
        }
    }

    function test_noErrorByDefault() {
        const s = createTemporaryObject(mockSetting, this)
        verify(s !== null)
        compare(s.showErrorText, false)
        compare(s.errorText, "")
    }

    function test_errorShownWhenSet() {
        const s = createTemporaryObject(mockSetting, this)
        verify(s !== null)
        s.debugLogOpenError = "Debug log file not found: /tmp/debug.log"
        compare(s.showErrorText, true)
        compare(s.errorText, "Debug log file not found: /tmp/debug.log")
    }

    function test_errorClearedWhenReset() {
        const s = createTemporaryObject(mockSetting, this)
        verify(s !== null)
        s.debugLogOpenError = "Some error"
        compare(s.showErrorText, true)
        s.debugLogOpenError = ""
        compare(s.showErrorText, false)
        compare(s.errorText, "")
    }
}
