// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/fixture/test_application.h>
#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_util.h>

#include <QDir>
#include <QObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>

class ApplicationLifecycleTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    WalletMode m_wallet_mode;

public:
    explicit ApplicationLifecycleTests(BitcoinQmlApplication& app, WalletMode wallet_mode)
        : m_app{app}, m_wallet_mode{wallet_mode} {}

private Q_SLOTS:
    void startupShowsNodePage()
    {
        QVERIFY(!m_app.nodeModel().errorState());
        auto* window = m_app.window();
        QVERIFY(window);
        QVERIFY_AWAIT(window->isVisible(), qmltest::Property(window, "visible"));
        QVERIFY_AWAIT(window->isExposed(), qmltest::Frame(window));
        QVERIFY_AWAIT(qmltest::TransitionsFinished(window), qmltest::Ui(m_app.window()));
        const auto* shell = m_wallet_mode == WalletMode::Enabled ? "desktopWalletsPage" : "nodeRunner";
        for (const auto* name : {shell, "blockClock"}) {
            auto* item = qobject_cast<QQuickItem*>(qmltest::Find(m_app, name));
            QVERIFY2(item, name);
            QVERIFY_AWAIT(item->isVisible(), qmltest::Property(item, "visible"));
            QVERIFY_AWAIT(item->width() > 0 && item->height() > 0, qmltest::Property(item, "width"), qmltest::Property(item, "height"));
            QCOMPARE(item->window(), window);
            QVERIFY2(item->mapRectToScene(item->boundingRect()).intersects(window->contentItem()->boundingRect()), name);
        }
        QObject frame_observer;
        bool rendered{false};
        connect(window, &QQuickWindow::afterRendering, &frame_observer,
                [&] { rendered = true; }, Qt::QueuedConnection);
        window->update();
        QVERIFY_AWAIT(rendered, qmltest::Frame(window));

        const auto info = qmltest::Call(m_app, &interfaces::Node::executeRpc, "getblockchaininfo", UniValue{UniValue::VARR}, "");
        QCOMPARE(info.find_value("chain").get_str(), std::string{"regtest"});
        QCOMPARE(qmltest::Call(m_app, &interfaces::Node::getNodeCount, ConnectionDirection::Both), 0U);
        QVERIFY(QDir{QStringLiteral(":/translations")}.exists(QStringLiteral("bitcoin_es.qm")));
        QVERIFY(QDir{QStringLiteral(":/translations")}.exists(QStringLiteral("bitcoin_qml_es.qm")));
    }

    void startupUsesRequestedWalletMode()
    {
        const bool enabled = m_wallet_mode == WalletMode::Enabled;
        QCOMPARE(m_app.window()->property("walletAvailableForUi").toBool(), enabled);
        QCOMPARE(m_app.window()->property("desktopWalletMode").toBool(), enabled);
        QCOMPARE(qmltest::Find(m_app, "desktopWalletsPage") != nullptr, enabled);
        QCOMPARE(qmltest::Model<QObject>(m_app, "walletController") != nullptr, enabled);
        QCOMPARE(qmltest::Model<QObject>(m_app, "walletListModel") != nullptr, enabled);
        if (enabled) {
            const auto wallets = qmltest::Call(m_app, &interfaces::Node::executeRpc, "listwallets", UniValue{UniValue::VARR}, "");
            QVERIFY(wallets.isArray());
            QCOMPARE(wallets.size(), 0U);
        }
    }

    void closingWindowShutsDownApplication()
    {
        auto* model = &m_app.nodeModel();
        auto* window = m_app.window();
        QVERIFY(window);
        QVERIFY(window->isVisible());
        QSignalSpy requested{model, &NodeModel::requestedShutdown};
        window->close();
        QVERIFY(qmltest::AwaitSignal(requested));
        QCOMPARE(requested.count(), 1);
        QVERIFY(window->property("shutdownInProgress").toBool());
        auto* shutdown_page = qobject_cast<QQuickItem*>(qmltest::Find(m_app, "shutdownPage"));
        QVERIFY(shutdown_page);
        QVERIFY_AWAIT(shutdown_page->isVisible(), qmltest::Property(shutdown_page, "visible"));
    }
};

std::unique_ptr<QObject> CreateLifecycleTests(BitcoinQmlApplication& app, WalletMode wallet_mode)
{
    return std::make_unique<ApplicationLifecycleTests>(app, wallet_mode);
}

#include <test_lifecycle.moc>
