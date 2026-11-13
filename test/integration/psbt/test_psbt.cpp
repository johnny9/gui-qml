// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/mocks/preparationtest.h>
#include <test/mocks/sendtest.h>
#include <test/mocks/psbtactiontest.h>
#include <test/mocks/messagebumptest.h>
#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <qml/models/psbtqmlmodel.h>
#include <core_io.h>
#include <key.h>
#include <key_io.h>
#include <util/moneystr.h>
#include <util/string.h>
#include <util/strencodings.h>

#include <QFile>
#include <QTemporaryDir>
#include <array>

class PsbtIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

public:
    explicit PsbtIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void watchOnlySignedScriptsSurviveOriginalExport_data()
    {
        QTest::addColumn<bool>("nested"); QTest::addColumn<bool>("send");
        for (bool nested : {false, true}) for (bool send : {false, true})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(nested ? "nested" : "witness").arg(send ? "send" : "broadcast"))) << nested << send;
    }
    void watchOnlySignedScriptsSurviveOriginalExport()
    {
        QFETCH(bool, nested); QFETCH(bool, send);
        auto* miner = qmltest::CreateWallet(m_app, "scripts-miner"); QVERIFY(miner);
        QVERIFY(qmltest::FundWallet(m_app, *miner));
        UniValue create{UniValue::VOBJ};
        create.pushKV("wallet_name", "scripts-reviewer"); create.pushKV("disable_private_keys", true); create.pushKV("blank", true);
        qmltest::Rpc(m_app, "createwallet", create);
        auto* reviewer = qmltest::WaitForWallet(m_app, "scripts-reviewer"); QVERIFY(reviewer);
        const auto descriptors = qmltest::Call(m_app, [nested](interfaces::Node*) {
            std::array<unsigned char, 32> bytes{};
            bytes.back() = 1; CKey first; first.Set(bytes.begin(), bytes.end(), true);
            bytes.back() = 2; CKey second; second.Set(bytes.begin(), bytes.end(), true);
            auto private_desc = "wsh(multi(2," + EncodeSecret(first) + "," + EncodeSecret(second) + "))";
            auto public_desc = "wsh(multi(2," + HexStr(first.GetPubKey()) + "," + HexStr(second.GetPubKey()) + "))";
            if (nested) { private_desc = "sh(" + private_desc + ")"; public_desc = "sh(" + public_desc + ")"; }
            return std::pair{private_desc + "#" + GetDescriptorChecksum(private_desc), public_desc + "#" + GetDescriptorChecksum(public_desc)};
        });
        for (const auto& [wallet, descriptor] : std::array<std::pair<QString, std::string>, 2>{{
                 {miner->name(), descriptors.first}, {reviewer->name(), descriptors.second}}}) {
            UniValue entry{UniValue::VOBJ}; entry.pushKV("desc", descriptor); entry.pushKV("timestamp", "now");
            UniValue entries{UniValue::VARR}; entries.push_back(entry);
            UniValue params{UniValue::VARR}; params.push_back(entries);
            const auto imported = qmltest::Rpc(m_app, "importdescriptors", params, wallet);
            QVERIFY(imported[0].find_value("success").get_bool());
        }
        UniValue derive{UniValue::VARR}; derive.push_back(descriptors.second);
        const auto address = qmltest::Rpc(m_app, "deriveaddresses", derive)[0].get_str();
        const auto funding = qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("[\"" + address + "\",0.01]"), miner->name()).get_str();
        const auto mining = qmltest::NewAddress(m_app, miner->name());
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining.toStdString() + "\"]"));
        const auto received = qmltest::Json("[0,9999999,[\"" + address + "\"]]");
        QVERIFY(QTest::qWaitFor([&] { return qmltest::Rpc(m_app, "listunspent", received, reviewer->name()).size() == 1; }, 5000));
        const auto coin = qmltest::Rpc(m_app, "listunspent", received, reviewer->name())[0];
        QCOMPARE(coin.find_value("txid").get_str(), funding);
        QCOMPARE(coin.find_value("amount").getValStr(), std::string{"0.01000000"});
        const auto parent = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + funding + "\"]"), miner->name()).find_value("hex").get_str();
        const auto destination = qmltest::NewAddress(m_app, miner->name()).toStdString();
        const auto index = coin.find_value("vout").getInt<int>();
        const auto unsigned_psbt = qmltest::Call(m_app, [parent, destination, index](interfaces::Node*) {
            CMutableTransaction funding_tx;
            if (!DecodeHexTx(funding_tx, parent)) throw std::runtime_error("funding decode");
            const auto previous = MakeTransactionRef(funding_tx);
            if (index < 0 || size_t(index) >= previous->vout.size() || previous->vout[index].nValue != 1'000'000) throw std::runtime_error("funding output");
            CMutableTransaction transaction;
            transaction.vin.emplace_back(COutPoint{previous->GetHash(), uint32_t(index)});
            transaction.vout.emplace_back(990'000, GetScriptForDestination(DecodeDestination(destination)));
            PartiallySignedTransaction document{transaction, 2}; document.inputs[0].non_witness_utxo = previous;
            return PsbtQmlModel::SerializePsbtRaw(document).toBase64().toStdString();
        });
        const auto signed_psbt = qmltest::Rpc(m_app, "walletprocesspsbt", qmltest::Json("[\"" + unsigned_psbt + "\",true,\"ALL\",false,false]"), miner->name()).find_value("psbt").get_str();
        const auto original = qmltest::Call(m_app, [signed_psbt](interfaces::Node*) {
            const auto parsed = DecodeBase64PSBT(signed_psbt);
            if (!parsed || parsed->inputs[0].partial_sigs.size() != 2) throw std::runtime_error("original signatures");
            auto complete = *parsed; CMutableTransaction extracted;
            if (!FinalizeAndExtractPSBT(complete, extracted)) throw std::runtime_error("signed control");
            auto withheld = *parsed; withheld.inputs[0].witness_script.clear(); withheld.inputs[0].redeem_script.clear();
            auto control = withheld; CMutableTransaction absent;
            if (FinalizeAndExtractPSBT(control, absent)) throw std::runtime_error("scripts were not necessary");
            return std::pair{PsbtQmlModel::SerializePsbtRaw(withheld), MakeTransactionRef(extracted)->GetWitnessHash().ToString()};
        });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto source_path = directory.filePath("withheld.psbt");
        QFile source{source_path}; QVERIFY(source.open(QIODevice::WriteOnly)); QCOMPARE(source.write(original.first), original.first.size()); source.close();
        QCOMPARE(AwaitPsbtImport(*reviewer, source_path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(reviewer->currentTransactionCanSend()); QVERIFY(reviewer->currentTransactionCanBroadcast());
        QVERIFY(!qmltest::Rpc(m_app, "getwalletinfo", {}, reviewer->name()).find_value("private_keys_enabled").get_bool());
        const auto saved_path = directory.filePath("preserved.psbt"); QVERIFY(AwaitPsbtSave(*reviewer, saved_path));
        QFile saved{saved_path}; QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(), original.first);
        QVERIFY2(send ? AwaitCommittedSend(*reviewer) : AwaitPsbtBroadcast(*reviewer), qPrintable(reviewer->transactionError()));
        const auto txid = reviewer->currentTransaction()->getWtx()->GetHash().ToString();
        const auto actual = qmltest::Rpc(m_app, "getrawtransaction", qmltest::Json("[\"" + txid + "\",true]"));
        QCOMPARE(actual.find_value("hash").get_str(), original.second);
    }
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
        std::vector<CTransactionRef> funding_transactions;
        funding_transactions.reserve(addresses.size());
        CAmount actual_inputs{0};
        for (size_t i = 0; i < addresses.size(); ++i) {
            const auto raw = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + funding[i] + "\"]"), miner->name()).find_value("hex").get_str();
            const auto funding_transaction = qmltest::Call(m_app, [raw](interfaces::Node*) -> CTransactionRef {
                CMutableTransaction transaction;
                return DecodeHexTx(transaction, raw) ? MakeTransactionRef(std::move(transaction)) : CTransactionRef{};
            });
            QVERIFY(funding_transaction);
            QCOMPARE(funding_transaction->GetHash(), *Txid::FromHex(funding[i]));
            funding_transactions.push_back(funding_transaction);
            const auto decoded = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + raw + "\"]"));
            for (const auto& output : decoded.find_value("vout").getValues()) {
                const auto address = output.find_value("scriptPubKey").find_value("address");
                if (!address.isStr() || address.get_str() != addresses[i].toStdString()) continue;
                const auto index = output.find_value("n").getInt<int>();
                tx.vin.emplace_back(COutPoint{*Txid::FromHex(funding[i]), uint32_t(index)});
                const auto coin = qmltest::Rpc(m_app, "gettxout", qmltest::Json("[\"" + funding[i] + "\", " + util::ToString(index) + "]"));
                const auto value = ParseMoney(coin.find_value("value").getValStr());
                QVERIFY(value); QCOMPARE(*value, CAmount{1'000'000});
                QVERIFY(index >= 0 && size_t(index) < funding_transaction->vout.size());
                QCOMPARE(funding_transaction->vout[index].nValue, *value);
                actual_inputs += *value;
            }
        }
        QCOMPARE(tx.vin.size(), size_t{2});
        tx.vout.emplace_back(1'500'000, GetScriptForDestination(DecodeDestination(mining.toStdString())));
        const CAmount actual_fee{actual_inputs - tx.vout[0].nValue};
        QCOMPARE(actual_fee, CAmount{500'000}); // Independent live gettxout oracle, not PSBT analysis.
        PartiallySignedTransaction unsigned_psbt{tx, version};
        // Supply independently authenticated funding transactions explicitly.
        // A sender RPC result does not acknowledge the recipient wallet's
        // asynchronous unconfirmed notification, which walletprocesspsbt would
        // otherwise need to fill missing non-witness UTXOs from wallet history.
        QCOMPARE(unsigned_psbt.inputs.size(), funding_transactions.size());
        for (size_t i{0}; i < unsigned_psbt.inputs.size(); ++i) {
            QCOMPARE(funding_transactions[i]->GetHash(), tx.vin[i].prevout.hash);
            unsigned_psbt.inputs[i].non_witness_utxo = funding_transactions[i];
        }
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
            QVERIFY(!AwaitPsbtBroadcast(*reviewer)); QVERIFY(!AwaitCommittedSend(*reviewer));
            QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), confirmed ? 0U : 2U);
        } else {
            QCOMPARE(reviewer->currentTransaction()->getTransactionFee(), actual_fee);
            QVERIFY(reviewer->currentTransactionCanBroadcast());
            QVERIFY2(AwaitPsbtBroadcast(*reviewer), qPrintable(reviewer->transactionError()));
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
        QCOMPARE(AwaitPsbtImport(*wallet, path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(wallet->currentTransaction());
        QVERIFY(wallet->currentTransactionIsImportedPsbt());
        QVERIFY(wallet->currentTransactionCanSend());
        QVERIFY(!wallet->currentTransactionCanBroadcast());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
        const auto exported_path = directory.filePath("exported.psbt");
        QVERIFY(AwaitPsbtSave(*wallet, exported_path));
        QVERIFY2(AwaitCommittedSend(*wallet), qPrintable(wallet->transactionError()));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 1U);
        QCOMPARE(AwaitPsbtImport(*wallet, exported_path), WalletQmlModel::PsbtImportResult::TransactionAlreadyKnown);
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
        QCOMPARE(AwaitPsbtImport(*reviewer, file.fileName()), WalletQmlModel::PsbtImportResult::WalletCannotSign);
        QVERIFY(reviewer->currentTransaction());
        QVERIFY(reviewer->currentTransactionIsImportedPsbt());
        QVERIFY(!reviewer->currentTransactionCanSend());
        QVERIFY(!reviewer->currentTransactionCanBroadcast());
        QVERIFY(!AwaitCommittedSend(*reviewer));
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
        QVERIFY2(AwaitPreparation(*wallet), qPrintable(wallet->transactionError()));
        QVERIFY2(AwaitCommittedSend(*wallet), qPrintable(wallet->transactionError()));
        const auto txid = wallet->currentTransaction()->txid();
        auto* bump = wallet->bumpModel();
        QVERIFY(AwaitBumpPreparation(*bump, txid, 2));
        QCOMPARE(bump->state(), BumpTransactionModel::NeedsConfirmation);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool")[0].get_str(), txid.toStdString());
        QVERIFY2(AwaitBumpCommit(*bump), qPrintable(bump->errorText()));
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
