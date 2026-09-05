// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/psbtdocument.h>
#include <qml/test/qt_test_registry.h>
#include <streams.h>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class PsbtDocumentTests : public QObject
{
    Q_OBJECT
    static QByteArray fixture()
    {
        CMutableTransaction tx;
        tx.vin.emplace_back(Txid{}, 0);
        tx.vout.emplace_back(9000, CScript{} << OP_TRUE);
        PartiallySignedTransaction psbt(tx);
        psbt.inputs[0].witness_utxo = CTxOut{10000, CScript{} << OP_TRUE};
        psbt.unknown[std::vector<unsigned char>{0xfa, 0x01}] = {0x42, 0x43};
        psbt.inputs[0].unknown[std::vector<unsigned char>{0xfb, 0x02}] = {0x44};
        psbt.outputs[0].unknown[std::vector<unsigned char>{0xf0, 0x03}] = {0x45};
        DataStream stream;
        stream << psbt;
        return QByteArray::fromStdString(stream.str());
    }
private Q_SLOTS:
    void nativeAndFileUrlPathsReadTheSameDocument()
    {
        QTemporaryDir directory;
        QFile file(directory.filePath(QStringLiteral("review with spaces.psbt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes = fixture();
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        QString error;
        const auto native = ReadPsbtDocument(file.fileName(), error);
        QVERIFY2(native, qPrintable(error));
        const auto url = ReadPsbtDocument(QUrl::fromLocalFile(file.fileName()).toString(), error);
        QVERIFY2(url, qPrintable(error));
        QCOMPARE(native->serialized(), bytes);
        QCOMPARE(url->serialized(), bytes);
    }

    void binaryBase64AndUnknownFields()
    {
        const auto bytes = fixture();
        QString error;
        auto binary = DecodePsbtDocument(bytes, error);
        QVERIFY2(binary, qPrintable(error));
        QCOMPARE(binary->serialized(), bytes);
        auto base64 = DecodePsbtDocument(bytes.toBase64() + '\n', error);
        QVERIFY2(base64, qPrintable(error));
        QCOMPARE(base64->serialized(), bytes);
        QCOMPARE(base64->current.unknown.size(), size_t{1});
        QCOMPARE(base64->current.inputs[0].unknown.size(), size_t{1});
        QCOMPARE(base64->current.outputs[0].unknown.size(), size_t{1});
    }
    void invalidUnsupportedAndOversized()
    {
        QString error;
        auto unsupported = fixture();
        const auto version = unsupported.indexOf(QByteArray::fromHex("01fb0402000000"));
        QVERIFY(version >= 0);
        unsupported[version + 3] = 3;
        QVERIFY(!DecodePsbtDocument(unsupported, error));
        QVERIFY(error.contains("Unsupported version"));
        for (const auto& bytes : {QByteArray{}, QByteArray("not a PSBT"), QByteArray("cHNidP8="), fixture().first(10)}) {
            QVERIFY(!DecodePsbtDocument(bytes, error));
            QVERIFY(!error.isEmpty());
        }
        QTemporaryDir directory;
        QFile oversized(directory.filePath("large.psbt"));
        QVERIFY(oversized.open(QIODevice::WriteOnly));
        QVERIFY(oversized.resize(PSBT_FILE_LIMIT + 1));
        oversized.close();
        QVERIFY(!ReadPsbtDocument(oversized.fileName(), error));
        QVERIFY(error.contains("100 MiB"));
        QVERIFY(!ReadPsbtDocument("https://example.invalid/wallet.psbt", error));
    }
};

BITCOINQML_REGISTER_QT_TEST(PsbtDocumentTests)
#include <test_psbtdocument.moc>
