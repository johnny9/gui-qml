// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/paymentrequest.h>
#include <qml/wallet/receiverequesthistorymodel.h>
#include <qml/wallet/walletqrimageprovider.h>
#include <qml/test/qt_test_registry.h>
#include <bitcoin-build-config.h>
#include <util/strencodings.h>
#include <QImage>
#include <QTest>

class ReceiveRequestTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void coreCompatiblePrefixAndOptionalPrivateNote()
    {
        QmlRecentRequestEntry entry;
        entry.id = 4'294'967'301LL;
        entry.date = QDateTime::fromSecsSinceEpoch(1'700'000'000);
        entry.recipient.address = "address";
        entry.recipient.label = "label";
        entry.recipient.amount = 12'345'678;
        entry.recipient.message = "memo";
        const std::string prefix{ReceiveRequestHistoryModel::SerializeEntry(entry)};
        QCOMPARE(QByteArray::fromStdString(prefix).toHex(), QByteArray{"01000000050000000100000000f15365010000000761646472657373056c6162656c4e61bc0000000000046d656d6f0000"});
        entry.recipient.noteSelf = "private";
        const auto extended{ReceiveRequestHistoryModel::SerializeEntry(entry)};
        QVERIFY(extended.starts_with(prefix));
        const auto decoded{ReceiveRequestHistoryModel::DeserializeEntries({prefix, extended})};
        QCOMPARE(decoded.size(), size_t{2});
        QCOMPARE(decoded[0].id, entry.id);
        QVERIFY(decoded[0].recipient.noteSelf.empty());
        QCOMPARE(decoded[1].recipient.noteSelf, std::string{"private"});
    }

    void repeatedAddressesKeepDistinctIds()
    {
        QmlRecentRequestEntry first, second;
        first.id = 1;
        first.recipient.address = "shared-address";
        second = first;
        second.id = 2;
        ReceiveRequestHistoryModel history;
        history.setEntries({first, second});
        QCOMPARE(history.count(), 2);
        QCOMPARE(history.matchingEntriesForAddress(QStringLiteral("shared-address")).size(), 2);
        QVERIFY(history.removeByRequestId(QStringLiteral("1")));
        QCOMPARE(history.count(), 1);
        QVERIFY(history.entryById(QStringLiteral("2")));
    }

    void exactAmountValidationAndBip21Escaping()
    {
        PaymentRequest draft;
        draft.setAmount(QStringLiteral("0.00000001"));
        QVERIFY(draft.validatedEntry());
        QCOMPARE(draft.validatedEntry()->recipient.amount, CAmount{1});
        for (const QString& invalid : {QStringLiteral("-1"), QStringLiteral("0.000000001"), QStringLiteral("1e2"), QStringLiteral("21000001")}) {
            draft.setAmount(invalid);
            QVERIFY(!draft.validatedEntry());
        }
        const auto uri{ReceiveRequestHistoryModel::BuildBitcoinUri(QStringLiteral("address"), 1, QStringLiteral("a & b"), QStringLiteral("100%"))};
        QCOMPARE(uri, QStringLiteral("bitcoin:address?amount=0.00000001&label=a%20%26%20b&message=100%25"));
    }

    void qrRenderingPreservesUriAndQuietZone()
    {
#ifdef USE_QRCODE
        const QString uri{QStringLiteral("bitcoin:address?label=100%25%20caf%C3%A9")};
        const auto image{WalletQRImageProvider::render(uri)};
        QVERIFY(!image.isNull());
        QVERIFY(image.width() >= 29);
        QCOMPARE(image.pixelColor(0, 0), QColor(Qt::white));
        QCOMPARE(image.pixelColor(4, 4), QColor(Qt::black));
        WalletQRImageProvider provider;
        QSize size;
        const auto encoded{QString::fromLatin1(uri.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))};
        QCOMPARE(provider.requestImage(encoded, &size, {}), image);
        QCOMPARE(size, image.size());
        QVERIFY(WalletQRImageProvider::render(QString(4000, QLatin1Char('x'))).isNull());
#else
        QVERIFY(WalletQRImageProvider::render(QStringLiteral("bitcoin:address")).isNull());
#endif
    }
};
BITCOINQML_REGISTER_QT_TEST(ReceiveRequestTests)
#include <test_receiverequests.moc>
