// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/wallet/psbtmodel.h>
#include <qml/wallet/walletpassphrase.h>
#include <qml/wallet/walletsession.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/psbt_test_fixture.h>
#include <qml/test/wallet_test_fixture.h>
#include <streams.h>
#include <key.h>
#include <script/solver.h>
#include <util/strencodings.h>
#include <QTemporaryDir>
#include <QTest>

class PsbtSigningIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
public:
    explicit PsbtSigningIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void taprootSigningRequiresACommittedKey_data()
    {
        QTest::addColumn<bool>("script_path");
        QTest::newRow("internal-key") << false;
        QTest::newRow("committed-script-key") << true;
    }
    void taprootSigningRequiresACommittedKey()
    {
        QFETCH(bool, script_path);
        WalletTestFixture fixture(m_app.node());
        auto local = fixture.create();
        auto unrelated = fixture.create();
        fixture.fund(*local);
        QTRY_VERIFY(local->getBalance() > 0);
        const auto public_key = [&](interfaces::Wallet& wallet) {
            const auto destination = wallet.getNewDestination(OutputType::BECH32, "taproot-fixture");
            if (!destination) throw std::runtime_error("Taproot fixture address unavailable");
            UniValue args(UniValue::VARR);
            args.push_back(EncodeDestination(*destination));
            const auto info = m_app.node().executeRpc("getaddressinfo", args, "/wallet/" + wallet.getWalletName());
            const CPubKey pubkey(ParseHex(info.find_value("pubkey").get_str()));
            if (!pubkey.IsFullyValid()) throw std::runtime_error("Taproot fixture public key unavailable");
            return XOnlyPubKey(pubkey);
        };
        const auto key = public_key(*local);
        const auto irrelevant = public_key(*unrelated);
        TaprootBuilder builder;
        const CScript script = CScript{} << std::vector<unsigned char>(key.begin(), key.end()) << OP_CHECKSIG;
        if (script_path) builder.Add(0, script, TAPROOT_LEAF_TAPSCRIPT);
        builder.Finalize(script_path ? XOnlyPubKey::NUMS_H : key);
        const auto destination = builder.GetOutput();
        UniValue funding_args(UniValue::VOBJ);
        funding_args.pushKV("address", EncodeDestination(destination));
        funding_args.pushKV("amount", 1);
        funding_args.pushKV("fee_rate", 2);
        const auto funding_id = Txid::FromHex(m_app.node().executeRpc("sendtoaddress", funding_args, "/wallet/" + local->getWalletName()).get_str());
        QVERIFY(funding_id);
        QTRY_VERIFY(local->getTx(*funding_id));
        const auto funding = local->getTx(*funding_id);
        const auto output = std::ranges::find_if(funding->vout, [&](const auto& out) { return out.scriptPubKey == GetScriptForDestination(destination); });
        QVERIFY(output != funding->vout.end());
        CMutableTransaction transaction;
        transaction.vin.emplace_back(*funding_id, std::distance(funding->vout.begin(), output));
        transaction.vout.emplace_back(COIN - 1'000, GetScriptForDestination(WitnessV0KeyHash(uint160{})));
        PartiallySignedTransaction psbt(transaction);
        psbt.inputs[0].witness_utxo = *output;
        const auto spend = builder.GetSpendData();
        psbt.inputs[0].m_tap_internal_key = spend.internal_key;
        psbt.inputs[0].m_tap_merkle_root = spend.merkle_root;
        psbt.inputs[0].m_tap_scripts = spend.scripts;
        std::set<uint256> leaves;
        if (script_path) leaves.insert(ComputeTapleafHash(TAPROOT_LEAF_TAPSCRIPT, script));
        psbt.inputs[0].m_tap_bip32_paths[key] = {leaves, {}};
        psbt.inputs[0].m_tap_bip32_paths[irrelevant] = {leaves, {}};
        // Even a syntactically valid script/control block containing a local
        // key is irrelevant when it does not commit to the actual input.
        TaprootBuilder forged;
        const CScript forged_script = CScript{} << std::vector<unsigned char>(irrelevant.begin(), irrelevant.end()) << OP_CHECKSIG;
        forged.Add(0, forged_script, TAPROOT_LEAF_TAPSCRIPT).Finalize(irrelevant);
        const auto forged_spend = forged.GetSpendData();
        psbt.inputs[0].m_tap_scripts.insert(forged_spend.scripts.begin(), forged_spend.scripts.end());
        psbt.inputs[0].m_tap_bip32_paths[irrelevant].first.insert(ComputeTapleafHash(TAPROOT_LEAF_TAPSCRIPT, forged_script));
        DataStream stream;
        stream << psbt;
        const auto bytes = QByteArray::fromStdString(stream.str());
        WalletOperationExecutor executor;
        WalletSession local_session(local, 617, executor);
        WalletSession unrelated_session(unrelated, 618, executor);
        PsbtModel model(local_session, m_app.node());
        PsbtModel no_keys(unrelated_session, m_app.node());
        const auto mempool = m_app.node().getMempoolSize();
        model.importData(bytes);
        no_keys.importData(bytes);
        QTRY_VERIFY(!model.busy() && !no_keys.busy());
        QVERIFY2(model.canSign(), qPrintable(model.error()));
        QVERIFY(!no_keys.canSign());
        QCOMPARE(model.document()->serialized(), bytes);
        QCOMPARE(no_keys.document()->serialized(), bytes);
        model.sign();
        QTRY_VERIFY(!model.busy());
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QVERIFY(model.complete());
        QVERIFY(!model.canSign());
        QCOMPARE(m_app.node().getMempoolSize(), mempool);

        // Taproot needs every input amount/script for its normal sighash. An
        // explicit attempt with another input's UTXO missing contributes no
        // signature, even though this wallet really holds the first key.
        transaction.vin.emplace_back(Txid{}, 7);
        PartiallySignedTransaction missing_utxo(transaction);
        missing_utxo.inputs[0] = psbt.inputs[0];
        DataStream missing_stream;
        missing_stream << missing_utxo;
        const auto missing_bytes = QByteArray::fromStdString(missing_stream.str());
        QVERIFY(local->encryptWallet(WalletPassphrase("synthetic-taproot-password")));
        QVERIFY(local->isLocked());
        model.importData(missing_bytes);
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.canUnlockForSigning());
        model.sign("synthetic-taproot-password");
        QTRY_VERIFY(!model.busy());
        QVERIFY2(model.error().contains("Core added no new local signatures"), qPrintable(model.error()));
        QCOMPARE(model.document()->serialized(), missing_bytes);
        QVERIFY(!model.canSign());
        QVERIFY(!model.canUnlockForSigning());
        QVERIFY(local->isLocked());
    }

    void foreignMultisigCanReceiveOnlyTheAvailableLocalSignature_data()
    {
        QTest::addColumn<bool>("encrypted");
        QTest::newRow("available-keys") << false;
        QTest::newRow("operation-only-unlock") << true;
    }
    void foreignMultisigCanReceiveOnlyTheAvailableLocalSignature()
    {
        QFETCH(bool, encrypted);
        WalletTestFixture fixture(m_app.node());
        const auto password = encrypted ? WalletPassphrase("synthetic-multisig-password") : SecureString{};
        auto local = fixture.create(password);
        auto unrelated = fixture.create(password);
        fixture.fund(*local);
        QTRY_VERIFY(local->getBalance() > 0);
        const auto address = local->getNewDestination(OutputType::BECH32, "multisig-key");
        QVERIFY(address);
        UniValue address_args(UniValue::VARR);
        address_args.push_back(EncodeDestination(*address));
        const auto information = m_app.node().executeRpc("getaddressinfo", address_args, "/wallet/" + local->getWalletName());
        const auto public_bytes = ParseHex(information.find_value("pubkey").get_str());
        const CPubKey public_key(public_bytes);
        QVERIFY(public_key.IsFullyValid());
        const auto irrelevant_address = unrelated->getNewDestination(OutputType::BECH32, "irrelevant-key");
        QVERIFY(irrelevant_address);
        address_args = UniValue(UniValue::VARR);
        address_args.push_back(EncodeDestination(*irrelevant_address));
        const auto irrelevant_info = m_app.node().executeRpc("getaddressinfo", address_args, "/wallet/" + unrelated->getWalletName());
        const CPubKey irrelevant_key(ParseHex(irrelevant_info.find_value("pubkey").get_str()));
        CKey remote_key;
        remote_key.MakeNewKey(true);
        const auto remote_public = remote_key.GetPubKey();
        const auto witness_script = GetScriptForMultisig(2, {public_key, remote_public});
        const auto foreign_script = GetScriptForDestination(WitnessV0ScriptHash(witness_script));
        QVERIFY(!local->txoutIsMine(CTxOut{COIN, foreign_script}));
        UniValue funding_args(UniValue::VOBJ);
        funding_args.pushKV("address", EncodeDestination(WitnessV0ScriptHash(witness_script)));
        funding_args.pushKV("amount", 1);
        funding_args.pushKV("fee_rate", 2);
        if (encrypted) QVERIFY(local->unlock(password)); // External fixture funding only.
        const auto funding_id = Txid::FromHex(m_app.node().executeRpc("sendtoaddress", funding_args, "/wallet/" + local->getWalletName()).get_str());
        if (encrypted) QVERIFY(local->lock());
        QVERIFY(funding_id);
        QTRY_VERIFY(local->getTx(*funding_id));
        const auto funding = local->getTx(*funding_id);
        const auto output = std::ranges::find_if(funding->vout, [&](const auto& out) { return out.scriptPubKey == foreign_script; });
        QVERIFY(output != funding->vout.end());
        CMutableTransaction transaction;
        transaction.vin.emplace_back(*funding_id, std::distance(funding->vout.begin(), output));
        transaction.vout.emplace_back(COIN - 1'000, GetScriptForDestination(WitnessV0KeyHash(uint160{})));
        PartiallySignedTransaction value(transaction);
        value.inputs[0].non_witness_utxo = funding;
        value.inputs[0].witness_utxo = *output;
        value.inputs[0].witness_script = witness_script;
        value.inputs[0].hd_keypaths[public_key] = KeyOriginInfo{};
        value.inputs[0].hd_keypaths[remote_public] = KeyOriginInfo{};
        // A wallet key in unrelated derivation metadata must not imply that
        // this input requires its signature, for either ECDSA or Taproot.
        value.inputs[0].hd_keypaths[irrelevant_key] = KeyOriginInfo{};
        value.inputs[0].m_tap_bip32_paths[XOnlyPubKey(irrelevant_key)] = {{}, {}};
        DataStream stream;
        stream << value;
        const auto bytes = QByteArray::fromStdString(stream.str());
        WalletOperationExecutor executor;
        WalletSession local_session(local, 615, executor);
        WalletSession unrelated_session(unrelated, 616, executor);
        PsbtModel model(local_session, m_app.node());
        PsbtModel no_keys(unrelated_session, m_app.node());
        const auto mempool = m_app.node().getMempoolSize();
        model.importData(bytes);
        no_keys.importData(bytes);
        QTRY_VERIFY(!model.busy() && !no_keys.busy());
        QCOMPARE(model.canSign(), !encrypted);
        QCOMPARE(model.canUnlockForSigning(), encrypted);
        QVERIFY(!model.complete());
        QVERIFY(!no_keys.canSign());
        if (encrypted) {
            QVERIFY(local->isLocked()); // Import never unlocks or signs.
            QVERIFY(unrelated->isLocked());
            model.sign("wrong-synthetic-password");
            QTRY_VERIFY(!model.busy());
            QVERIFY(local->isLocked());
            QVERIFY(!model.error().isEmpty());
            QVERIFY(model.canUnlockForSigning());
            no_keys.sign("synthetic-multisig-password");
            QTRY_VERIFY(!no_keys.busy());
            QVERIFY(unrelated->isLocked());
            QVERIFY(!no_keys.canSign());
            QVERIFY(!no_keys.canUnlockForSigning());
            QVERIFY(no_keys.error().contains("No available local signing keys"));
            QCOMPARE(no_keys.document()->serialized(), bytes);
        }
        QCOMPARE(model.document()->serialized(), bytes); // Import added no signature.
        QCOMPARE(m_app.node().getMempoolSize(), mempool);
        model.sign(encrypted ? "synthetic-multisig-password" : "");
        QTRY_VERIFY(!model.busy());
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QVERIFY(!model.complete());
        QVERIFY(!model.canSign()); // This wallet already contributed its one key.
        QVERIFY(!model.canUnlockForSigning());
        QCOMPARE(local->isLocked(), encrypted);
        QCOMPARE(model.document()->current.inputs[0].partial_sigs.size(), size_t{1});
        QVERIFY(model.document()->current.inputs[0].partial_sigs.contains(public_key.GetID()));
        QTemporaryDir files;
        model.exportFile(files.filePath("partial-multisig.psbt"));
        QTRY_VERIFY(!model.busy());
        QString error;
        const auto exported = ReadPsbtDocument(files.filePath("partial-multisig.psbt"), error);
        QVERIFY2(exported, qPrintable(error));
        QCOMPARE(exported->current.inputs[0].partial_sigs, model.document()->current.inputs[0].partial_sigs);
        QCOMPARE(exported->current.inputs[0].witness_script, witness_script);
        QCOMPARE(exported->current.inputs[0].hd_keypaths.size(), size_t{3});
        QCOMPARE(m_app.node().getMempoolSize(), mempool);
    }

    void explicitSigningRelocksAndPreservesMetadata()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create(WalletPassphrase("qml-psbt-test-only"));
        fixture.fund(*backend);
        QTRY_VERIFY(backend->getBalance() > 0);
        QString error;
        auto document = DecodePsbtDocument(FundedPsbt(m_app.node(), *backend), error);
        QVERIFY(document);
        document->current.unknown[std::vector<unsigned char>{0xfa}] = {1, 2};
        document->current.inputs[0].unknown[std::vector<unsigned char>{0xfb}] = {3, 4};
        const PSBTProprietary proprietary{1, {'q', 'm', 'l'}, {0xfc, 3, 'q', 'm', 'l', 1, 42}, {9, 8, 7}};
        document->current.m_proprietary.insert(proprietary);
        document->current.inputs[0].m_proprietary.insert(proprietary);
        document->modified = true;
        const auto bytes = document->serialized();
        WalletOperationExecutor executor;
        WalletSession session(backend, 611, executor);
        PsbtModel model(session, m_app.node());
        model.importData(bytes);
        QTRY_VERIFY(!model.busy());
        QVERIFY(!model.canSign());
        QVERIFY(model.canUnlockForSigning());
        model.sign("wrong password");
        QTRY_VERIFY(!model.busy());
        QVERIFY(!model.error().isEmpty());
        QVERIFY(!model.error().contains("wrong password"));
        QCOMPARE(model.document()->serialized(), bytes);
        QVERIFY(backend->isLocked());
        const auto mempool = m_app.node().getMempoolSize();
        model.sign("qml-psbt-test-only");
        const auto revision = model.revision();
        model.sign("qml-psbt-test-only");
        QCOMPARE(model.revision(), revision);
        QTRY_VERIFY(!model.busy());
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QVERIFY(model.complete());
        QVERIFY(!model.canSign());
        QVERIFY(backend->isLocked());
        QCOMPARE(m_app.node().getMempoolSize(), mempool);
        QCOMPARE(model.document()->current.unknown, document->current.unknown);
        QCOMPARE(model.document()->current.inputs[0].unknown, document->current.inputs[0].unknown);
        QCOMPARE(model.document()->current.m_proprietary.size(), size_t{1});
        QCOMPARE(model.document()->current.m_proprietary.begin()->value, proprietary.value);
        QCOMPARE(model.document()->current.inputs[0].m_proprietary.size(), size_t{1});
        QCOMPARE(model.document()->current.inputs[0].m_proprietary.begin()->value, proprietary.value);
        QCOMPARE(model.document()->original, bytes);
        QTemporaryDir files;
        const auto signed_bytes = model.document()->serialized();
        model.exportFile(files.filePath("missing/result.psbt"));
        QTRY_VERIFY(!model.busy());
        QVERIFY(!model.error().isEmpty());
        QCOMPARE(model.document()->serialized(), signed_bytes);
        model.exportFile(files.filePath("result.psbt"));
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.error().isEmpty());
        const auto exported = ReadPsbtDocument(files.filePath("result.psbt"), error);
        QVERIFY(exported);
        QCOMPARE(exported->serialized(), signed_bytes);
        QVERIFY(backend->unlock(WalletPassphrase("qml-psbt-test-only")));
        model.importData(bytes);
        QTRY_VERIFY(!model.busy());
        model.sign();
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.complete());
        QVERIFY(!backend->isLocked()); // Preserve an already-unlocked session.
        QVERIFY(backend->lock());
    }

    void partialResultsRemainExportableWithoutClaimingCompleteness()
    {
        WalletTestFixture fixture(m_app.node());
        auto first = fixture.create();
        auto second = fixture.create();
        fixture.fund(*first);
        fixture.fund(*second);
        QTRY_VERIFY(first->getBalance() > 0 && second->getBalance() > 0);
        QString error;
        auto one = DecodePsbtDocument(FundedPsbt(m_app.node(), *first), error);
        auto two = DecodePsbtDocument(FundedPsbt(m_app.node(), *second), error);
        QVERIFY(one && two);
        auto tx = *one->current.GetUnsignedTx();
        const auto second_tx = *two->current.GetUnsignedTx();
        tx.vin.insert(tx.vin.end(), second_tx.vin.begin(), second_tx.vin.end());
        tx.vout.insert(tx.vout.end(), second_tx.vout.begin(), second_tx.vout.end());
        PartiallySignedTransaction combined(tx);
        combined.inputs = one->current.inputs;
        combined.inputs.insert(combined.inputs.end(), two->current.inputs.begin(), two->current.inputs.end());
        combined.outputs = one->current.outputs;
        combined.outputs.insert(combined.outputs.end(), two->current.outputs.begin(), two->current.outputs.end());
        DataStream stream;
        stream << combined;
        WalletOperationExecutor executor;
        WalletSession session(first, 612, executor);
        PsbtModel model(session, m_app.node());
        model.importData(QByteArray::fromStdString(stream.str()));
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.canSign());
        model.sign();
        QTRY_VERIFY(!model.busy());
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QVERIFY(!model.complete());
        QVERIFY(!model.canSign());
        QVERIFY(CountPSBTUnsignedInputs(model.document()->current) < CountPSBTUnsignedInputs(combined));
        QVERIFY(model.document()->modified);
        QVERIFY(DecodePsbtDocument(model.document()->serialized(), error));
    }

    void reviewExportStripsInputScriptsAndIsSessionBound()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        fixture.fund(*backend);
        QTRY_VERIFY(backend->getBalance() > 0);
        QString error;
        auto signed_document = DecodePsbtDocument(FundedPsbt(m_app.node(), *backend, true), error);
        QVERIFY(signed_document);
        CMutableTransaction signed_transaction;
        QVERIFY(FinalizeAndExtractPSBT(signed_document->current, signed_transaction));
        WalletOperationExecutor executor;
        WalletSession session(backend, 613, executor);
        PsbtModel model(session, m_app.node());
        TransactionReviewModel review;
        review.setSnapshot({999, session.generation(), 5, MakeTransactionRef(signed_transaction), 1000, std::nullopt, {}});
        QVERIFY(!model.importReview(&review));
        review.setSnapshot({session.id(), session.generation(), 5, MakeTransactionRef(signed_transaction), 1000, std::nullopt, {}});
        QVERIFY(model.importReview(&review));
        QTRY_VERIFY(!model.busy());
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QVERIFY(!model.complete());
        QCOMPARE(review.snapshot()->transaction->GetWitnessHash(), MakeTransactionRef(signed_transaction)->GetWitnessHash());
        for (const auto& input : model.document()->current.inputs) {
            QVERIFY(input.final_script_sig.empty());
            QVERIFY(input.final_script_witness.IsNull());
        }
    }
};

BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(PsbtSigningIntegrationTests)
#include <test_psbtsigning_integration.moc>
