// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/psbtdocument.h>
#include <qml/test/qt_test_registry.h>
#include <streams.h>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class PsbtExportTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void originalAndModifiedMetadataSurviveFailedWrites()
    {
        CMutableTransaction tx;
        tx.vin.emplace_back(Txid{}, 0);
        tx.vout.emplace_back(9000, CScript{} << OP_TRUE);
        PartiallySignedTransaction value(tx);
        value.unknown[std::vector<unsigned char>{0xfa}] = {1, 2, 3};
        value.inputs[0].unknown[std::vector<unsigned char>{0xfb}] = {4, 5, 6};
        value.outputs[0].unknown[std::vector<unsigned char>{0xf0}] = {7, 8, 9};
        const PSBTProprietary proprietary{1, {'q', 'm', 'l'}, {0xfc, 3, 'q', 'm', 'l', 1, 42}, {9, 8, 7}};
        value.m_proprietary.insert(proprietary);
        value.inputs[0].m_proprietary.insert(proprietary);
        value.outputs[0].m_proprietary.insert(proprietary);
        DataStream stream;
        stream << value;
        const auto original = QByteArray::fromStdString(stream.str());
        QString error;
        auto document = DecodePsbtDocument(original, error);
        QVERIFY(document);
        QTemporaryDir directory;
        QVERIFY(!WritePsbtDocument(*document, directory.filePath("missing/result.psbt"), error));
        QCOMPARE(document->serialized(), original);
        QVERIFY(WritePsbtDocument(*document, directory.filePath("result.psbt"), error));
        auto restored = ReadPsbtDocument(directory.filePath("result.psbt"), error);
        QVERIFY(restored);
        QCOMPARE(restored->serialized(), original);
        document->modified = true;
        document->current.inputs[0].witness_utxo = CTxOut{10000, CScript{} << OP_TRUE};
        QVERIFY(WritePsbtDocument(*document, directory.filePath("result.psbt"), error));
        restored = ReadPsbtDocument(directory.filePath("result.psbt"), error);
        QVERIFY(restored);
        QVERIFY(restored->serialized() != original);
        QCOMPARE(restored->current.unknown, value.unknown);
        QCOMPARE(restored->current.inputs[0].unknown, value.inputs[0].unknown);
        QCOMPARE(restored->current.outputs[0].unknown, value.outputs[0].unknown);
        QCOMPARE(restored->current.m_proprietary.size(), size_t{1});
        QCOMPARE(restored->current.m_proprietary.begin()->value, proprietary.value);
        QCOMPARE(restored->current.inputs[0].m_proprietary.size(), size_t{1});
        QCOMPARE(restored->current.inputs[0].m_proprietary.begin()->value, proprietary.value);
        QCOMPARE(restored->current.outputs[0].m_proprietary.size(), size_t{1});
        QCOMPARE(restored->current.outputs[0].m_proprietary.begin()->value, proprietary.value);
        QCOMPARE(document->original, original);
    }
};

BITCOINQML_REGISTER_QT_TEST(PsbtExportTests)
#include <test_psbtexport.moc>
