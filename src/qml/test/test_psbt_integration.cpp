// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/applicationrouter.h>
#include <qml/wallet/psbtmodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/walletmanager.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/psbt_test_fixture.h>
#include <qml/test/wallet_test_fixture.h>
#include <node/types.h>
#include <QTest>
#include <QQmlApplicationEngine>
#include <streams.h>

class PsbtIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    QString m_original_route;
    QVariantMap m_original_parameters;
public:
    explicit PsbtIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void init()
    {
        m_original_route = m_app.router().currentRoute();
        m_original_parameters = m_app.router().currentParameters();
    }
    void cleanup()
    {
        QVERIFY(m_app.router().navigate(m_original_route, m_original_parameters));
    }
    void inspectPageLoadsForAnExplicitSession()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        auto& manager = *m_app.walletManager();
        QTRY_VERIFY(manager.selectedWallet());
        manager.selectWallet(QString::fromStdString(backend->getWalletName()));
        QTRY_VERIFY(manager.selectedWallet() && manager.selectedWallet()->overview()->name() == QString::fromStdString(backend->getWalletName()));
        QVERIFY(m_app.router().navigate("wallet-psbt", {{"sessionId", manager.selectedWallet()->sessionId()}}));
        auto* root = m_app.engine().rootObjects().constFirst();
        QTRY_VERIFY(root->findChild<QObject*>("walletPsbtPage"));
        QVERIFY(root->findChild<QObject*>("psbtImport"));
        QCOMPARE(root->findChild<QObject*>("psbtImport")->property("enabled").toBool(), false);
        QVERIFY(root->findChild<QObject*>("psbtStatus"));
        QVERIFY(m_app.router().navigate("wallets"));
    }
    void missingFeeDoesNotCreateAReview()
    {
        WalletTestFixture fixture(m_app.node());
        WalletOperationExecutor executor;
        WalletSession session(fixture.create(), 600, executor);
        PsbtModel model(session, m_app.node());
        CMutableTransaction transaction;
        transaction.vin.emplace_back(Txid{}, 0);
        transaction.vout.emplace_back(COIN, GetScriptForDestination(WitnessV0KeyHash(uint160{})));
        PartiallySignedTransaction psbt(transaction);
        DataStream stream;
        stream << psbt;
        model.importData(QByteArray::fromStdString(stream.str()));
        QTRY_VERIFY(!model.busy());
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QVERIFY(model.loaded());
        QCOMPARE(model.outputs().size(), 1);
        QVERIFY(!model.review()->hasReview());
        QVERIFY(!model.canSign());
    }
    void localAndForeignImportNeverSignOrBroadcast()
    {
        WalletTestFixture fixture(m_app.node());
        auto local = fixture.create();
        auto foreign = fixture.create();
        auto watch = fixture.create({}, wallet::WALLET_FLAG_DESCRIPTORS | wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS);
        fixture.fund(*local);
        QTRY_VERIFY(local->getBalance() > 0);
        const auto bytes = FundedPsbt(m_app.node(), *local, false, true);
        WalletOperationExecutor executor;
        WalletSession session(local, 601, executor);
        WalletSession other(foreign, 602, executor);
        WalletSession watching(watch, 603, executor);
        PsbtModel model(session, m_app.node());
        PsbtModel review_only(other, m_app.node());
        PsbtModel watch_only(watching, m_app.node());
        const auto mempool = m_app.node().getMempoolSize();
        model.importData(bytes);
        QTRY_VERIFY(!model.busy());
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QVERIFY(model.loaded());
        QVERIFY(model.canSign());
        QVERIFY(!model.complete());
        QVERIFY(model.review()->hasReview());
        QVERIFY(model.outputs().size() >= 2);
        QCOMPARE(model.document()->serialized(), bytes);
        review_only.importData(bytes);
        watch_only.importData(bytes);
        QTRY_VERIFY(!review_only.busy() && !watch_only.busy());
        QVERIFY(!review_only.canSign());
        QVERIFY(!watch_only.canSign());
        QVERIFY(review_only.review()->hasReview());
        QCOMPARE(review_only.document()->serialized(), bytes);
        QCOMPARE(m_app.node().getMempoolSize(), mempool);
        const auto revision = model.revision();
        model.importFile({});
        QCOMPARE(model.revision(), revision); // Native cancellation preserves the review.
        model.importData("invalid psbt");
        QTRY_VERIFY(!model.busy());
        QVERIFY(!model.loaded());
        QVERIFY(!model.review()->hasReview());
        QVERIFY(!model.error().isEmpty());
        QVERIFY(model.revision() > revision);
    }

    void completeForeignAndAlreadyKnownAreDistinct()
    {
        WalletTestFixture fixture(m_app.node());
        auto source = fixture.create();
        auto observer = fixture.create();
        fixture.fund(*source);
        QTRY_VERIFY(source->getBalance() > 0);
        const auto bytes = FundedPsbt(m_app.node(), *source, true);
        WalletOperationExecutor executor;
        WalletSession session(observer, 604, executor);
        PsbtModel model(session, m_app.node());
        model.importData(bytes);
        QTRY_VERIFY(!model.busy());
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QVERIFY(model.complete());
        QVERIFY(!model.canSign());
        QVERIFY(!model.known());
        QVERIFY(model.review()->hasReview());
        std::string error;
        QCOMPARE(m_app.node().broadcastTransaction(model.review()->snapshot()->transaction, source->getDefaultMaxTxFee(), error), node::TransactionError::OK);
        model.importData(bytes);
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.known());
        QVERIFY(!model.canSign());
        session.invalidate();
        QVERIFY(!model.available());
        QVERIFY(!model.loaded());
        QVERIFY(!model.review()->hasReview());
    }
};

BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(PsbtIntegrationTests)
#include <test_psbt_integration.moc>
