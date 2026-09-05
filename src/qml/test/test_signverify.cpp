// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/qt_test_registry.h>
#include <qml/wallet/signverifymessagemodel.h>
#include <chainparams.h>
#include <common/signmessage.h>
#include <key.h>
#include <key_io.h>
#include <QTest>

class SignVerifyTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void legacyProtocolValidation()
    {
        SelectParams(ChainType::REGTEST);
        ECC_Context context;
        CKey key;
        key.MakeNewKey(true);
        const QString address{QString::fromStdString(EncodeDestination(PKHash{key.GetPubKey()}))};
        const QString witness{QString::fromStdString(EncodeDestination(WitnessV0KeyHash{key.GetPubKey()}))};
        std::string signature;
        QVERIFY(MessageSign(key, "exact message", signature));
        const QString encoded{QString::fromStdString(signature)};
        QVERIFY(SignVerifyMessageModel::Verify(address, "exact message", encoded));
        QVERIFY(!SignVerifyMessageModel::Verify(address, "exact message ", encoded));
        QVERIFY(!SignVerifyMessageModel::Verify(address, "exact message", "not base64"));
        QVERIFY(!SignVerifyMessageModel::Verify("invalid-address", "exact message", encoded));
        QVERIFY(!SignVerifyMessageModel::Verify(witness, "exact message", encoded));
        QVERIFY(SignVerifyMessageModel::IsLegacyAddress(address));
        QVERIFY(!SignVerifyMessageModel::IsLegacyAddress(witness));
    }
};
BITCOINQML_REGISTER_QT_TEST(SignVerifyTests)
#include <test_signverify.moc>
