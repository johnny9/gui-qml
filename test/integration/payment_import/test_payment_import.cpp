// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTemporaryDir>
#include <QUrl>
#include <algorithm>

class PaymentImportIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    WalletQmlModel* m_wallet{nullptr};
    QString m_address;

public:
    explicit PaymentImportIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void init()
    {
        QGuiApplication::clipboard()->clear();
        m_wallet = qmltest::CreateWallet(m_app);
        QVERIFY(m_wallet);
        m_address = qmltest::NewAddress(m_app, m_wallet->name());
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendPage"));
    }

    void cleanup() { QGuiApplication::clipboard()->clear(); }

    void equalImportedAmountReplacesMaximumIntentWithoutChangingTheRequest()
    {
        QVERIFY(qmltest::FundWallet(m_app, *m_wallet));
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton")); QTRY_COMPARE(m_wallet->maximumRecipient(), recipient);
        QTRY_VERIFY(recipient->amount()->satoshi() > 0);
        const auto requested = recipient->amount()->satoshi();
        const QString uri = "bitcoin:" + m_address + "?amount=" + BitcoinAmount::satsToBtcString(requested) + "&label=Alice&message=Exact";
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendOptionsOpenPaymentRequestButton"));
        QVERIFY(qmltest::SetText(m_app, "sendUriImportInput", uri));
        QVERIFY(qmltest::Click(m_app, "sendUriImportApplyButton"));
        auto* replacement = qmltest::Find(m_app, "sendPaymentUriOverwritePopup"); QVERIFY(replacement);
        QTRY_VERIFY(replacement->property("opened").toBool());
        QCOMPARE(m_wallet->maximumRecipient(), recipient); QCOMPARE(recipient->amount()->satoshi(), requested);
        QVERIFY(qmltest::Click(m_app, "sendPaymentUriOverwriteConfirmButton"));
        QTRY_VERIFY(recipient->hasPaymentRequest()); QTRY_COMPARE(m_wallet->maximumRecipient(), nullptr);
        QCOMPARE(recipient->amount()->satoshi(), requested); QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("Alice"));
        QCOMPARE(recipient->message(), QStringLiteral("Exact"));
        m_wallet->setCustomFeeRate("10"); m_wallet->scheduleFeeEstimates(); QTRY_VERIFY(!m_wallet->feeEstimatePending());
        QCOMPARE(recipient->amount()->satoshi(), requested); QVERIFY(!m_wallet->sendDraftSweepsWallet());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }

    void typedExplicitImportPreservesPrivateNoteAndPublicMetadata() {
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        recipient->editPrivateNote(QStringLiteral("Private bookkeeping"));
        const QString uri = "bitcoin:" + m_address + "?amount=0.01234567&label=Public%20invoice&message=Merchant%20message";
        auto* page = qmltest::Find(m_app, "walletSendPage"); QVERIFY(page);
        QVERIFY(QMetaObject::invokeMethod(page, "applyPaymentRequestFromText", Q_ARG(QVariant, uri), Q_ARG(QVariant, "explicit request")));
        QTRY_COMPARE(recipient->amount()->satoshi(), 1'234'567);
        QCOMPARE(recipient->label(), QStringLiteral("Private bookkeeping"));
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("Public invoice"));
        QCOMPARE(recipient->message(), QStringLiteral("Merchant message"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void typedPsbtFileCreatesOriginalReviewWithoutSigningOrSubmission() {
        QVERIFY(qmltest::FundWallet(m_app, *m_wallet));
        const auto encoded = qmltest::Rpc(m_app, "walletcreatefundedpsbt",
            qmltest::Json("[[],[{\"" + m_address.toStdString() + "\":0.005}],0,{\"fee_rate\":2}]"), m_wallet->name()).find_value("psbt").get_str();
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("original.psbt");
        { QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); const auto bytes = QByteArray::fromStdString(encoded); QCOMPARE(file.write(bytes), bytes.size()); }
        auto* page = qmltest::Find(m_app, "walletSendPage"); QVERIFY(page);
        QVERIFY(QMetaObject::invokeMethod(page, "applyPaymentRequestFromFile", Q_ARG(QVariant, path)));
        QTRY_VERIFY(m_wallet->currentTransaction()); QVERIFY(m_wallet->currentTransactionIsImportedPsbt());
        QVERIFY(m_wallet->importedReviewOwner() > 0); QVERIFY(m_wallet->importedReviewChild() > 0);
        QVERIFY(m_wallet->currentTransaction()->feeKnown());
        const auto document = m_wallet->currentTransaction()->assessedPsbt(); QVERIFY(document);
        QVERIFY(qmltest::Call(m_app, [document](interfaces::Node*) {
            return std::all_of(document->inputs.begin(), document->inputs.end(), [](const auto& input) {
                return input.partial_sigs.empty() && input.m_tap_key_sig.empty() && input.m_tap_script_sigs.empty() && input.m_musig2_partial_sigs.empty() &&
                    input.final_script_sig.empty() && input.final_script_witness.IsNull();
            });
        }));
        const auto view = m_wallet->currentTransaction();
        auto* review_popup = qmltest::Find(m_app, "transactionReviewPopup"); QVERIFY(review_popup);
        QTRY_VERIFY(review_popup->property("opened").toBool());
        QCOMPARE(m_wallet->currentTransaction(), view);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void clipboardPasteRequiresConfirmationBeforeOverwriting()
    {
        QVERIFY(qmltest::SetText(m_app, "sendNoteInput", "My private note"));
        const QString uri = "bitcoin:" + m_address + "?amount=0.01234567&label=Alice&message=Lunch";
        QGuiApplication::clipboard()->setText(uri);
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        QCOMPARE(recipient->label(), QStringLiteral("My private note"));
        QVERIFY(qmltest::Click(m_app, "sendAddressInput"));
        auto* field = qmltest::Find(m_app, "sendAddressInput");
        QVERIFY(QMetaObject::invokeMethod(field, "forceActiveFocus"));
        QVERIFY(QMetaObject::invokeMethod(field, "paste"));
        auto* popup = qmltest::Find(m_app, "sendPaymentUriOverwritePopup");
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QCOMPARE(recipient->label(), QStringLiteral("My private note"));
        QVERIFY(qmltest::Click(m_app, "sendPaymentUriOverwriteCancelButton"));
        QTRY_VERIFY(!popup->property("opened").toBool());
        QCOMPARE(recipient->label(), QStringLiteral("My private note"));
        QVERIFY(QMetaObject::invokeMethod(field, "forceActiveFocus"));
        QVERIFY(QMetaObject::invokeMethod(field, "paste"));
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(qmltest::Click(m_app, "sendPaymentUriOverwriteConfirmButton"));
        QTRY_VERIFY(!popup->property("opened").toBool());
        QCOMPARE(recipient->amount()->satoshi(), 1'234'567);
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("Alice"));
        QCOMPARE(recipient->message(), QStringLiteral("Lunch"));
        QCOMPARE(recipient->label(), QStringLiteral("Lunch - Alice"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void manualImportRejectsMalformedUriWithoutChangingTheDraft()
    {
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        recipient->setAddress(m_address);
        recipient->amount()->setSatoshi(10'000);
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendOptionsOpenPaymentRequestButton"));
        QVERIFY(qmltest::SetText(m_app, "sendUriImportInput", "bitcoin://" + m_address + "?amount=1"));
        QVERIFY(qmltest::Click(m_app, "sendUriImportApplyButton"));
        auto* status = qmltest::WaitForVisible(m_app, "sendPaymentRequestStatusText");
        QVERIFY(status);
        QTRY_VERIFY(status->property("text").toString().contains("bitcoin://"));
        QCOMPARE(recipient->amount()->satoshi(), 10'000);
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendOptionsOpenPaymentRequestButton"));
        QVERIFY(qmltest::SetText(m_app, "sendUriImportInput", "bitcoin:" + m_address + "?amount=0.02&label=manual"));
        QVERIFY(qmltest::Click(m_app, "sendUriImportApplyButton"));
        auto* replacement = qmltest::Find(m_app, "sendPaymentUriOverwritePopup"); QVERIFY(replacement);
        QTRY_VERIFY(replacement->property("opened").toBool()); QCOMPARE(recipient->amount()->satoshi(), 10'000);
        QVERIFY(qmltest::Click(m_app, "sendPaymentUriOverwriteConfirmButton"));
        QTRY_COMPARE(recipient->amount()->satoshi(), 2'000'000);
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("manual"));
    }

    void localFilesAndFileUrlsReachTheSendModel_data()
    {
        QTest::addColumn<bool>("file_url");
        QTest::newRow("path") << false;
        QTest::newRow("file-url") << true;
    }

    void localFilesAndFileUrlsReachTheSendModel()
    {
        QFETCH(bool, file_url);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file{directory.filePath(QString::fromUtf8("payment request ü.txt"))};
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto uri = ("bitcoin:" + m_address + "?amount=0.03&label=file&message=invoice").toUtf8();
        QCOMPARE(file.write(uri), uri.size());
        file.close();
        const auto path = file_url ? QUrl::fromLocalFile(file.fileName()).toString() : file.fileName();
        auto* page = qmltest::Find(m_app, "walletSendPage");
        QVERIFY(QMetaObject::invokeMethod(page, "applyPaymentRequestFromFile", Q_ARG(QVariant, path)));
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        QTRY_COMPARE(recipient->amount()->satoshi(), 3'000'000);
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("file"));
        QCOMPARE(recipient->message(), QStringLiteral("invoice"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void nativePasteImportsUrisFromOtherRecipientFields_data()
    {
        QTest::addColumn<QString>("field");
        QTest::newRow("amount") << "sendAmountInput";
        QTest::newRow("private-note") << "sendNoteInput";
    }

    void nativePasteImportsUrisFromOtherRecipientFields()
    {
        QFETCH(QString, field);
        QGuiApplication::clipboard()->setText("bitcoin:" + m_address + "?amount=0.07&label=field-paste");
        QVERIFY(qmltest::Click(m_app, qPrintable(field)));
        auto* input = qmltest::Find(m_app, qPrintable(field));
        QVERIFY(input);
        QVERIFY(QMetaObject::invokeMethod(input, "forceActiveFocus"));
        QVERIFY(QMetaObject::invokeMethod(input, "paste"));
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        QTRY_COMPARE(recipient->amount()->satoshi(), 7'000'000);
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("field-paste"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void ordinaryPasteIntoThePrivateNoteKeepsNormalEditing()
    {
        QVERIFY(qmltest::SetText(m_app, "sendNoteInput", "Private note"));
        QGuiApplication::clipboard()->setText("-ordinary-paste");
        auto* input = qmltest::Find(m_app, "sendNoteInput");
        QVERIFY(input);
        QVERIFY(QMetaObject::invokeMethod(input, "paste"));
        QTRY_COMPARE(input->property("text").toString(), QStringLiteral("Private note-ordinary-paste"));
        QVERIFY(m_wallet->sendRecipientList()->currentRecipient()->paymentRequestLabel().isEmpty());
    }

    void droppedTextAndUrlsReachTheProductionDropArea_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("text") << "text";
        QTest::newRow("bitcoin-url") << "bitcoin-url";
        QTest::newRow("file-url") << "file-url";
    }

    void droppedTextAndUrlsReachTheProductionDropArea()
    {
        QFETCH(QString, kind);
        const QString uri = "bitcoin:" + m_address + "?amount=0.04&label=dropped";
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QMimeData mime;
        if (kind == "text") {
            mime.setText(uri);
        } else if (kind == "bitcoin-url") {
            mime.setUrls({QUrl{uri}});
        } else {
            QFile file{directory.filePath(QString::fromUtf8("dropped payment ü.txt"))};
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto bytes = uri.toUtf8();
            QCOMPARE(file.write(bytes), bytes.size());
            file.close();
            mime.setUrls({QUrl::fromLocalFile(file.fileName())});
        }
        auto* target = qobject_cast<QQuickItem*>(qmltest::WaitForVisible(m_app, "sendDropArea"));
        QVERIFY(target);
        QVERIFY(qmltest::WaitForPolish(target));
        const auto position = target->mapToScene(target->boundingRect().center());
        QDragEnterEvent enter{position.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
        QCoreApplication::sendEvent(m_app.window(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop{position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
        QCoreApplication::sendEvent(m_app.window(), &drop);
        auto* recipient = m_wallet->sendRecipientList()->currentRecipient();
        QTRY_COMPARE(recipient->amount()->satoshi(), 4'000'000);
        QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("dropped"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }
};

std::unique_ptr<QObject> CreatePaymentImportTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<PaymentImportIntegrationTests>(app);
}

#include <test_payment_import.moc>
