// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/sendpreview.h>
#include <qml/test/qt_test_registry.h>
#include <test/util/setup_common.h>
#include <QTest>

class CoinSelectionTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void explicitInputPolicyNeverFallsBackToAutomatic()
    {
        BasicTestingSetup setup{ChainType::REGTEST};
        SendDraftSnapshot draft;
        auto automatic = CoinControlForDraft(draft, true);
        QVERIFY(automatic.m_allow_other_inputs);
        QVERIFY(!automatic.HasSelected());
        draft.selected_only = true;
        QVERIFY_EXCEPTION_THROWN(CoinControlForDraft(draft, true), std::runtime_error);
        const COutPoint input{Txid::FromUint256(uint256::ONE), 2};
        draft.selected_inputs = {input};
        const auto preview = CoinControlForDraft(draft, true);
        const auto prepare = CoinControlForDraft(draft, false);
        QVERIFY(!preview.m_allow_other_inputs);
        QVERIFY(!prepare.m_allow_other_inputs);
        QVERIFY(preview.IsSelected(input));
        QCOMPARE(preview.ListSelected(), prepare.ListSelected());
        QVERIFY(IsValidDestination(preview.destChange));
        QVERIFY(!IsValidDestination(prepare.destChange));
        draft.selected_inputs.clear();
        QVERIFY(preview.IsSelected(input)); // A captured policy cannot change with the form.
    }
};

BITCOINQML_REGISTER_QT_TEST(CoinSelectionTests)
#include <test_coinselection.moc>
