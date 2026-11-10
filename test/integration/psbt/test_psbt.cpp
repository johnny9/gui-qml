// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <qml/models/psbtqmlmodel.h>
#include <key_io.h>
#include <util/moneystr.h>
#include <util/string.h>

#include <QFile>
#include <QTemporaryDir>

class PsbtIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

public:
    explicit PsbtIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void authenticatedLegacyFeeReview_data()
    {
        QTest::addColumn<uint>("version"); QTest::addColumn<int>("owned"); QTest::addColumn<bool>("forged"); QTest::addColumn<bool>("confirmed");
        for (uint version : {0U, 2U}) for (int owned : {0, 1, 2}) for (bool forged : {false, true}) {
            const auto name = QString("v%1-owned%2-%3").arg(version).arg(owned).arg(forged ? "forged" : "control").toUtf8();
            QTest::newRow(name.constData()) << version << owned << forged << true;
        }
        for (uint version : {0U, 2U}) for (bool forged : {false, true}) {
            const auto name = QString("v%1-unconfirmed-%2").arg(version).arg(forged ? "forged" : "control").toUtf8();
            QTest::newRow(name.constData()) << version << 0 << forged << false;
        }
    }
    void authenticatedLegacyFeeReview()
    {
        QFETCH(uint, version); QFETCH(int, owned); QFETCH(bool, forged); QFETCH(bool, confirmed);
        auto* miner = qmltest::CreateWallet(m_app, "fee-miner"); QVERIFY(miner);
        auto* foreign = qmltest::CreateWallet(m_app, "fee-foreign"); QVERIFY(foreign);
        auto* reviewer = qmltest::CreateWallet(m_app, "fee-reviewer"); QVERIFY(reviewer);
        QVERIFY(qmltest::FundWallet(m_app, *miner));
        const auto first = qmltest::NewAddress(m_app, owned > 0 ? reviewer->name() : foreign->name(), "legacy");
        const auto second = qmltest::NewAddress(m_app, owned > 1 ? reviewer->name() : foreign->name(), "legacy");
        const auto mining = qmltest::NewAddress(m_app, miner->name());
        const std::vector<QString> addresses{first, second};
        std::vector<std::string> funding;
        funding.reserve(addresses.size());
        for (const auto& address : addresses) {
            funding.push_back(qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("[\"" + address.toStdString() + "\", 0.01]"), miner->name()).get_str());
        }
        if (confirmed) qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1, \"" + mining.toStdString() + "\"]"));
        CMutableTransaction tx;
        CAmount actual_inputs{0};
        for (size_t i = 0; i < addresses.size(); ++i) {
            const auto raw = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + funding[i] + "\"]"), miner->name()).find_value("hex").get_str();
            const auto decoded = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + raw + "\"]"));
            for (const auto& output : decoded.find_value("vout").getValues()) {
                const auto address = output.find_value("scriptPubKey").find_value("address");
                if (!address.isStr() || address.get_str() != addresses[i].toStdString()) continue;
                const auto index = output.find_value("n").getInt<int>();
                tx.vin.emplace_back(COutPoint{*Txid::FromHex(funding[i]), uint32_t(index)});
                const auto coin = qmltest::Rpc(m_app, "gettxout", qmltest::Json("[\"" + funding[i] + "\", " + util::ToString(index) + "]"));
                const auto value = ParseMoney(coin.find_value("value").getValStr());
                QVERIFY(value); QCOMPARE(*value, CAmount{1'000'000}); actual_inputs += *value;
            }
        }
        QCOMPARE(tx.vin.size(), size_t{2});
        tx.vout.emplace_back(1'500'000, GetScriptForDestination(DecodeDestination(mining.toStdString())));
        const CAmount actual_fee{actual_inputs - tx.vout[0].nValue};
        QCOMPARE(actual_fee, CAmount{500'000}); // Independent live gettxout oracle, not PSBT analysis.
        PartiallySignedTransaction unsigned_psbt{tx, version};
        auto encoded = PsbtQmlModel::SerializePsbtRaw(unsigned_psbt).toBase64().toStdString();
        for (const auto* signer : {reviewer, foreign}) {
            encoded = qmltest::Rpc(m_app, "walletprocesspsbt", qmltest::Json("[\"" + encoded + "\", true, \"ALL\", true, true]"), signer->name()).find_value("psbt").get_str();
        }
        const auto parsed = DecodeBase64PSBT(encoded); QVERIFY(parsed);
        auto psbt = *parsed;
        CMutableTransaction signed_control; QVERIFY(FinalizeAndExtractPSBT(psbt, signed_control));
        if (forged || !confirmed) {
            for (size_t i{0}; i < psbt.inputs.size(); ++i) {
                if (confirmed && i != 1) continue;
                auto& input = psbt.inputs[i]; QVERIFY(input.non_witness_utxo);
                input.witness_utxo = input.non_witness_utxo->vout[input.prev_out];
                input.non_witness_utxo = nullptr;
                if (forged && i == 1) input.witness_utxo.nValue -= actual_fee - 1;
            }
        }
        auto verification = psbt;
        CMutableTransaction signed_candidate; QVERIFY(FinalizeAndExtractPSBT(verification, signed_candidate));
        QCOMPARE(signed_candidate.GetHash(), signed_control.GetHash());
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("legacy.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(psbt, path), QString{});
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::Find(m_app, "psbtImportPathField")->setProperty("text", path));
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendImportPsbtFromFileButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QVERIFY(reviewer->currentTransaction());
        QTRY_VERIFY(!reviewer->transactionFeePending());
        const auto fee_text = qmltest::Find(m_app, "sendTransactionReviewFee")->property("value").toString();
        if (forged) {
            QVERIFY(!reviewer->currentTransactionCanSend()); QVERIFY(!reviewer->currentTransactionCanBroadcast());
            QCOMPARE(fee_text, QString("Unknown"));
            QVERIFY(!reviewer->currentTransactionFlow().value("feeKnown").toBool());
            QVERIFY(!reviewer->broadcastCurrentTransaction()); QVERIFY(!reviewer->sendTransaction());
            QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), confirmed ? 0U : 2U);
        } else {
            QCOMPARE(reviewer->currentTransaction()->getTransactionFee(), actual_fee);
            QVERIFY(reviewer->currentTransactionCanBroadcast());
            QVERIFY2(reviewer->broadcastCurrentTransaction(), qPrintable(reviewer->transactionError()));
            const auto entry = qmltest::Rpc(m_app, "getmempoolentry", qmltest::Json("[\"" + signed_control.GetHash().GetHex() + "\"]"));
            QCOMPARE(ParseMoney(entry.find_value("fees").find_value("base").getValStr()), std::optional<CAmount>{actual_fee});
        }
    }
    void failedImportPreservesTheDraftAndAllowsRetry_data()
    {
        QTest::addColumn<bool>("create_invalid_file");
        QTest::addColumn<QString>("expected_error");
        QTest::newRow("missing-file") << false << QStringLiteral("Could not open PSBT file");
        QTest::newRow("malformed-file") << true << QStringLiteral("Could not decode PSBT");
    }

    void failedImportPreservesTheDraftAndAllowsRetry()
    {
        QFETCH(bool, create_invalid_file);
        QFETCH(QString, expected_error);
        auto* recipient = qmltest::CreateWallet(m_app, "recipient");
        QVERIFY(recipient);
        const auto address = qmltest::NewAddress(m_app, recipient->name());
        auto* wallet = qmltest::CreateWallet(m_app, "signer");
        QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto params = qmltest::Json("[[], {\"" + address.toStdString() + "\": 1}, 0, {\"fee_rate\": 2}, true]");
        const auto psbt = qmltest::Rpc(m_app, "walletcreatefundedpsbt", params, wallet->name()).find_value("psbt").get_str();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto invalid_path = directory.filePath("invalid.psbt");
        if (create_invalid_file) {
            QFile file{invalid_path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            const QByteArray invalid{"not a PSBT"};
            QCOMPARE(file.write(invalid), invalid.size());
        }
        QFile valid{directory.filePath("valid.psbt")};
        QVERIFY(valid.open(QIODevice::WriteOnly));
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(psbt));
        QCOMPARE(valid.write(bytes), bytes.size());
        valid.close();

        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", address));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "0.25"));
        auto* draft = wallet->sendRecipientList()->currentRecipient();
        QCOMPARE(draft->address()->address(), address);
        QCOMPARE(draft->amount()->satoshi(), COIN / 4);

        // Inject only the file-picker result; the menu and error handling are production QML.
        auto* path = qmltest::Find(m_app, "psbtImportPathField");
        QVERIFY(path);
        QVERIFY(path->setProperty("text", invalid_path));
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendImportPsbtFromFileButton"));
        auto* error = qmltest::WaitForVisible(m_app, "unsupportedPsbtPopup");
        QVERIFY(error);
        QTRY_VERIFY(error->property("opened").toBool());
        QVERIFY(qmltest::Find(m_app, "unsupportedPsbtPopupMessage")->property("text").toString().contains(expected_error));
        QVERIFY(!qmltest::Find(m_app, "transactionReviewPopup")->property("visible").toBool());
        QVERIFY(!wallet->currentTransaction());
        QCOMPARE(draft->address()->address(), address);
        QCOMPARE(draft->amount()->satoshi(), COIN / 4);
        QCOMPARE(qmltest::Find(m_app, "sendAddressInput")->property("text").toString(), draft->address()->formattedAddress());
        QCOMPARE(qmltest::Find(m_app, "sendAmountInput")->property("text").toString(), QStringLiteral("0.25000000"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);

        // Closing the error must allow a valid import without restarting or clearing the draft.
        QVERIFY(qmltest::Click(m_app, "unsupportedPsbtPopupOkButton"));
        QTRY_VERIFY(!error->property("visible").toBool());
        QVERIFY(path->setProperty("text", valid.fileName()));
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendImportPsbtFromFileButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QTRY_VERIFY(qmltest::Find(m_app, "transactionReviewPopup")->property("opened").toBool());
        QVERIFY(wallet->currentTransaction());
        QVERIFY(wallet->currentTransactionIsImportedPsbt());
        QVERIFY(wallet->currentTransactionCanSend());
        QVERIFY(!wallet->currentTransactionCanBroadcast());
        QVERIFY(wallet->importedPsbt()->error().isEmpty());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void importSignBroadcastAndRecognizeKnownTransaction()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "psbt");
        QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        auto* recipient = qmltest::CreateWallet(m_app, "recipient");
        QVERIFY(recipient);
        const auto address = qmltest::NewAddress(m_app, recipient->name());
        const auto params = qmltest::Json("[[], {\"" + address.toStdString() + "\": 1}, 0, {\"fee_rate\": 2}, true]");
        const auto psbt = qmltest::Rpc(m_app, "walletcreatefundedpsbt", params, wallet->name()).find_value("psbt").get_str();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("unsigned.psbt");
        QFile file{path};
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(psbt));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        QCOMPARE(wallet->importPsbtFromFile(path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(wallet->currentTransaction());
        QVERIFY(wallet->currentTransactionIsImportedPsbt());
        QVERIFY(wallet->currentTransactionCanSend());
        QVERIFY(!wallet->currentTransactionCanBroadcast());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
        const auto exported_path = directory.filePath("exported.psbt");
        QCOMPARE(wallet->saveCurrentTransactionAsPsbt(exported_path), QString{});
        QVERIFY2(wallet->sendTransaction(), qPrintable(wallet->transactionError()));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 1U);
        QCOMPARE(wallet->importPsbtFromFile(exported_path), WalletQmlModel::PsbtImportResult::TransactionAlreadyKnown);
    }

    void foreignPsbtCanBeReviewedWithoutSigning()
    {
        auto* source = qmltest::CreateWallet(m_app, "source");
        QVERIFY(source);
        QVERIFY(qmltest::FundWallet(m_app, *source));
        auto* reviewer = qmltest::CreateWallet(m_app, "reviewer");
        QVERIFY(reviewer);
        const auto address = qmltest::NewAddress(m_app, reviewer->name());
        const auto params = qmltest::Json("[[], {\"" + address.toStdString() + "\": 1}, 0, {\"fee_rate\": 2}, true]");
        const auto psbt = qmltest::Rpc(m_app, "walletcreatefundedpsbt", params, source->name()).find_value("psbt").get_str();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file{directory.filePath("foreign.psbt")};
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(psbt));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        QCOMPARE(reviewer->importPsbtFromFile(file.fileName()), WalletQmlModel::PsbtImportResult::WalletCannotSign);
        QVERIFY(reviewer->currentTransaction());
        QVERIFY(reviewer->currentTransactionIsImportedPsbt());
        QVERIFY(!reviewer->currentTransactionCanSend());
        QVERIFY(!reviewer->currentTransactionCanBroadcast());
        QVERIFY(!reviewer->sendTransaction());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void feeReplacementRequiresConfirmation()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        auto* recipient = wallet->sendRecipientList()->currentRecipient();
        recipient->setAddress(qmltest::NewAddress(m_app, wallet->name()));
        recipient->amount()->setSatoshi(COIN);
        wallet->setCustomFeeEnabled(true);
        wallet->setCustomFeeRate("1");
        QVERIFY2(wallet->prepareTransaction(), qPrintable(wallet->transactionError()));
        QVERIFY2(wallet->sendTransaction(), qPrintable(wallet->transactionError()));
        const auto txid = wallet->currentTransaction()->txid();
        auto* bump = wallet->bumpModel();
        bump->prepareFeeBump(txid, 2);
        QCOMPARE(bump->state(), BumpTransactionModel::NeedsConfirmation);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool")[0].get_str(), txid.toStdString());
        QVERIFY2(bump->confirmFeeBump(), qPrintable(bump->errorText()));
        QCOMPARE(bump->state(), BumpTransactionModel::Succeeded);
        QVERIFY(bump->newTxid() != txid);
        const auto pool = qmltest::Rpc(m_app, "getrawmempool");
        QCOMPARE(pool.size(), 1U);
        QCOMPARE(pool[0].get_str(), bump->newTxid().toStdString());
    }
};

std::unique_ptr<QObject> CreatePsbtTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<PsbtIntegrationTests>(app);
}

#include <test_psbt.moc>
