// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    name: "FeeSelection"
    when: windowShown
    width: 900
    height: 700

    Component {
        id: feeSelectionComponent

        FeeSelection {
            walletModel: testWalletModel
            currentTarget: testWalletModel.targetBlocks
            onFeeChanged: function(target) {
                testWalletModel.targetBlocks = target
            }
        }
    }

    SignalSpy {
        id: feeChangedSpy
    }

    function init() {
        testWalletModel.clearFeeEstimates()
        testWalletModel.setFeeEstimate(1, "0.00000750 ₿")
        testWalletModel.setFeeEstimate(2, "0.00000500 ₿")
        testWalletModel.setFeeEstimate(6, "0.00000250 ₿")
        testWalletModel.targetBlocks = 2
    }

    function test_feeSelection_has_stable_selectors() {
        const control = createTemporaryObject(feeSelectionComponent, this)
        verify(control !== null)

        compare(control.objectName, "feeSelectionControl")
        verify(findChild(control, "feeSelectionEstimateLabel") !== null)
        verify(findChild(control, "feeSelectionDropdownButton") !== null)
        verify(findChild(control, "feeSelectionPopup") !== null)
        verify(findChild(control, "feeSelectionList") !== null)
    }

    function test_feeSelection_matches_standard_fee_spec_labels_and_estimate() {
        testWalletModel.targetBlocks = 2

        const control = createTemporaryObject(feeSelectionComponent, this)
        verify(control !== null)

        compare(control.selectedIndex, 1)
        compare(control.selectedLabel, "Default")
        compare(control.selectedDuration, "(~20 mins)")
        compare(control.selectedEstimate, "0.00000500 ₿")

        const estimateLabel = findChild(control, "feeSelectionEstimateLabel")
        verify(estimateLabel !== null)
        compare(estimateLabel.text, "0.00000500 ₿")
    }

    function test_feeSelection_selecting_option_updates_state_and_emits() {
        testWalletModel.targetBlocks = 2

        const control = createTemporaryObject(feeSelectionComponent, this)
        verify(control !== null)

        const popup = findChild(control, "feeSelectionPopup")
        const list = findChild(control, "feeSelectionList")
        verify(popup !== null)
        verify(list !== null)

        feeChangedSpy.target = control
        feeChangedSpy.signalName = "feeChanged"
        feeChangedSpy.clear()

        popup.open()
        tryVerify(function() {
            return list.itemAtIndex(2) !== null
        })

        const lowFeeOption = list.itemAtIndex(2)
        verify(lowFeeOption !== null)

        const lowFeeEstimate = findChild(lowFeeOption, "feeSelectionOptionEstimate2")
        verify(lowFeeEstimate !== null)
        compare(lowFeeEstimate.text, "0.00000250 ₿")

        lowFeeOption.clicked()

        compare(control.selectedIndex, 2)
        compare(control.selectedLabel, "Low")
        compare(control.selectedDuration, "(~60 mins)")
        compare(control.selectedEstimate, "0.00000250 ₿")
        compare(feeChangedSpy.count, 1)
        compare(feeChangedSpy.signalArguments[0][0], 6)
        compare(popup.visible, false)
    }

    function test_feeSelection_popup_width_matches_estimate_availability() {
        const control = createTemporaryObject(feeSelectionComponent, this)
        verify(control !== null)

        const popup = findChild(control, "feeSelectionPopup")
        verify(popup !== null)

        testWalletModel.clearFeeEstimates()
        compare(popup.width, 280)

        testWalletModel.setFeeEstimate(2, "0.00000500 ₿")

        tryVerify(function() {
            return popup.width === 360
        })
    }

    function test_feeSelection_popup_estimates_stay_aligned_when_selection_changes() {
        testWalletModel.targetBlocks = 2

        const control = createTemporaryObject(feeSelectionComponent, this)
        verify(control !== null)

        const popup = findChild(control, "feeSelectionPopup")
        const list = findChild(control, "feeSelectionList")
        verify(popup !== null)
        verify(list !== null)

        popup.open()
        tryVerify(function() {
            return list.itemAtIndex(0) !== null
                && list.itemAtIndex(1) !== null
                && list.itemAtIndex(2) !== null
        })

        const highFeeEstimate = findChild(list.itemAtIndex(0), "feeSelectionOptionEstimate0")
        const defaultFeeEstimate = findChild(list.itemAtIndex(1), "feeSelectionOptionEstimate1")
        const lowFeeEstimate = findChild(list.itemAtIndex(2), "feeSelectionOptionEstimate2")
        verify(highFeeEstimate !== null)
        verify(defaultFeeEstimate !== null)
        verify(lowFeeEstimate !== null)

        compare(highFeeEstimate.x + highFeeEstimate.width, defaultFeeEstimate.x + defaultFeeEstimate.width)
        compare(defaultFeeEstimate.x + defaultFeeEstimate.width, lowFeeEstimate.x + lowFeeEstimate.width)
    }

    function test_feeSelection_current_target_syncs_selected_preset() {
        testWalletModel.targetBlocks = 6

        const control = createTemporaryObject(feeSelectionComponent, this)
        verify(control !== null)

        compare(control.selectedIndex, 2)
        compare(control.selectedLabel, "Low")
        compare(control.selectedDuration, "(~60 mins)")
        compare(control.selectedEstimate, "0.00000250 ₿")

        testWalletModel.targetBlocks = 1
        compare(control.selectedIndex, 0)
        compare(control.selectedLabel, "High")
        compare(control.selectedDuration, "(~10 mins)")
        compare(control.selectedEstimate, "0.00000750 ₿")
    }
}
