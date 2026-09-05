// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/wallet/walletsendmodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/wallet_test_fixture.h>
#include <key_io.h>
#include <QTest>

class SendDraftIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
public:
    explicit SendDraftIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void draftsBelongToTheOriginalSession()
    {
        WalletTestFixture fixture(m_app.node());
        WalletOperationExecutor executor;
        WalletSession first(fixture.create(), 401, executor);
        WalletSession second(fixture.create(), 402, executor);
        WalletSendModel first_send(first, m_app.node().getDustRelayFee());
        WalletSendModel second_send(second, m_app.node().getDustRelayFee());
        QVERIFY(first_send.available());
        const auto initial_revision = first_send.revision();
        first_send.recipients()->setAddress(0, QString::fromStdString(EncodeDestination(WitnessV0KeyHash(uint160{}))));
        first_send.recipients()->setAmount(0, "1");
        const auto snapshot = first_send.snapshot();
        QCOMPARE(snapshot.session_id, quint64{401});
        QVERIFY(snapshot.revision > initial_revision);
        QCOMPARE(snapshot.recipients.size(), size_t{1});
        QVERIFY(second_send.snapshot().recipients.empty());
        first_send.recipients()->setAmount(0, "2");
        QCOMPARE(snapshot.recipients.front().amount, COIN);
        first.invalidate();
        QVERIFY(!first_send.available());
        QVERIFY(first_send.revision() > snapshot.revision);
        QVERIFY(second_send.available());
        first_send.discard();
        QVERIFY(first_send.snapshot().recipients.empty());
    }
};

BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(SendDraftIntegrationTests)
#include <test_senddraft_integration.moc>
