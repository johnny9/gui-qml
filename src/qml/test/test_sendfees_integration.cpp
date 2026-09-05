// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/wallet/sendpreview.h>
#include <qml/wallet/walletsendmodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/wallet_test_fixture.h>
#include <key_io.h>
#include <univalue.h>
#include <QTest>

class SendFeesIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
public:
    explicit SendFeesIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void previewsAreUnsignedBoundedAndDoNotConsumeChangeKeys()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        fixture.fund(*backend);
        const std::string wallet_uri = "/wallet/" + backend->getWalletName();
        const auto before = m_app.node().executeRpc("getwalletinfo", UniValue{UniValue::VARR}, wallet_uri);
        WalletOperationExecutor executor;
        WalletSession session(backend, 501, executor);
        WalletSendModel send(session, m_app.node().getDustRelayFee());
        send.recipients()->setAddress(0, QString::fromStdString(EncodeDestination(WitnessV0KeyHash(uint160{}))));
        send.recipients()->setAmount(0, "1");
        QTRY_VERIFY_WITH_TIMEOUT(send.fees()->result().fee.has_value(), 10'000);
        QVERIFY(send.fees()->affordable());
        const quint64 revision = send.revision();
        const auto expected = EstimateSendFee(*backend, send.snapshot(), 1);
        QCOMPARE(send.fees()->result().fee, expected.fee);
        QCOMPARE(send.revision(), revision); // A preview must not feed back as a new edit.
        send.fees()->setCustomRate("8.001");
        send.fees()->setCustom(true);
        QTRY_VERIFY_WITH_TIMEOUT(!send.fees()->pending(), 10'000);
        QVERIFY(send.fees()->result().fee.has_value());
        QCOMPARE(CoinControlForDraft(send.snapshot(), true).m_feerate->GetFeePerK(), CAmount{8001});
        const auto after = m_app.node().executeRpc("getwalletinfo", UniValue{UniValue::VARR}, wallet_uri);
        QCOMPARE(after.find_value("keypoolsize_hd_internal").getInt<int>(), before.find_value("keypoolsize_hd_internal").getInt<int>());
        QCOMPARE(after.find_value("txcount").getInt<int>(), before.find_value("txcount").getInt<int>());
        send.recipients()->setMaximum(0, true);
        QTRY_VERIFY_WITH_TIMEOUT(!send.fees()->pending(), 10'000);
        QVERIFY(send.fees()->affordable());
        QVERIFY(send.snapshot().recipients[0].maximum);
        QCOMPARE(send.snapshot().recipients[0].amount, CAmount{0}); // Preview did not rewrite the draft.
        session.invalidate();
        QVERIFY(!send.fees()->result().fee);
    }
};

BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(SendFeesIntegrationTests)
#include <test_sendfees_integration.moc>
