// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_util.h>

#include <QMetaEnum>
#include <QPointer>
#include <QSignalSpy>
#include <QScopeGuard>

#include <utility>

class NavigationIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

public:
    explicit NavigationIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}

private Q_SLOTS:
    void cleanup()
    {
        QVERIFY(qmltest::ReturnToNode(m_app));
        auto* clock = qmltest::Find(m_app, "blockClock");
        QVERIFY(clock);
        QVERIFY(QTest::qWaitFor([&] {
            return clock->property("visible").toBool() && qmltest::TransitionsFinished(m_app.window());
        }));
    }

    void settingsPagesLoadAndReturnToRetainedBlockClock()
    {
        QPointer<QObject> clock{qmltest::Find(m_app, "blockClock")};
        QVERIFY(clock);
        QSignalSpy warnings{&m_app.engine(), &QQmlEngine::warnings};
        QVERIFY(qmltest::OpenSettings(m_app));
        QTRY_VERIFY(qmltest::Find(m_app, "settingsView"));
        auto* settings = qmltest::Find(m_app, "settingsView");
        QTRY_VERIFY(settings->property("visible").toBool());
        QTRY_VERIFY(qmltest::TransitionsFinished(m_app.window()));
        auto* container = qmltest::Find(m_app, "settingsPageContainer");
        QVERIFY(container);
        auto* sidebar = qobject_cast<QQuickItem*>(qmltest::Find(m_app, "settingsSidebarList"));
        QVERIFY(sidebar);
        const auto position_mode = sidebar->metaObject()->enumerator(sidebar->metaObject()->indexOfEnumerator("PositionMode"));
        const int contain = position_mode.keyToValue("Contain");
        QVERIFY(contain >= 0);
        for (const auto& [section, page_name] : {
                 std::pair{"display", "displaySettingsPage"},
                 std::pair{"window-behavior", "windowBehaviorSettingsPage"},
                 std::pair{"storage", "storageSettingsPage"},
                 std::pair{"connection", "connectionSettingsPage"},
                 std::pair{"about", "aboutSettingsPage"}}) {
            auto* button = qobject_cast<QQuickItem*>(qmltest::Find(m_app, qPrintable(QStringLiteral("settingsSidebar_%1").arg(section))));
            QVERIFY2(button, section);
            const int index = button->parentItem()->property("index").toInt();
            QVERIFY(QMetaObject::invokeMethod(sidebar, "positionViewAtIndex", Q_ARG(int, index), Q_ARG(int, contain)));
            QTRY_VERIFY(sidebar->contains(button->mapToItem(sidebar, button->boundingRect().center())));
            QVERIFY(qmltest::Click(button));
            QTRY_COMPARE(settings->property("selectedSectionId").toString(), QString::fromLatin1(section));
            QCOMPARE(container->property("currentSectionId").toString(), QString::fromLatin1(section));
            auto* page = qobject_cast<QQuickItem*>(container->property("currentItem").value<QObject*>());
            QVERIFY(page);
            QCOMPARE(page->objectName(), QString::fromLatin1(page_name));
            QTRY_VERIFY(page->isVisible());
            QTRY_VERIFY(page->width() > 0 && page->height() > 0);
            QVERIFY(page->mapRectToScene(page->boundingRect()).intersects(m_app.window()->contentItem()->boundingRect()));
            if (QString::fromLatin1(section) == "about") {
                QVERIFY(qmltest::Click(m_app, "aboutDeveloperRow"));
                QVERIFY(qmltest::WaitForVisible(m_app, "settingsDeveloper"));
                QVERIFY(qmltest::Click(m_app, "settingsDeveloperBack"));
                QTRY_VERIFY(page->isVisible());
            }
            QCOMPARE(warnings.count(), 0);
        }
        QVERIFY(qmltest::ReturnToNode(m_app));
        QTRY_VERIFY(clock && clock->property("visible").toBool());
        QTRY_VERIFY(qmltest::TransitionsFinished(m_app.window()));
        QCOMPARE(qmltest::Find(m_app, "blockClock"), clock.data());
        QCOMPARE(warnings.count(), 0);
    }

    void nativeFileRouteCannotNavigateBehindTheOwnedWalletModal()
    {
        auto* actions = qmltest::Find(m_app, "desktopMenuActions");
        QVERIFY(actions);
        auto* create = actions->property("createWallet").value<QObject*>();
        auto* settings = actions->property("settings").value<QObject*>();
        QVERIFY(create && settings);
        if (!m_app.window()->property("desktopWalletMode").toBool()) {
            // The wallet-disabled configuration exposes no Add-Wallet route.
            QVERIFY(!create->property("visible").toBool());
            return;
        }
        QVERIFY(create->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(create, "trigger"));
        QPointer<QObject> modal{qmltest::WaitForVisible(m_app, "walletCreationModal")};
        QVERIFY(modal);
        const auto close_modal = qScopeGuard([&] {
            if (modal) QMetaObject::invokeMethod(modal, "close");
            QTest::qWaitFor([&] { return !modal || !modal->property("visible").toBool(); });
        });
        QVERIFY(qmltest::Click(m_app, "walletTypeRegular"));
        auto* form = qmltest::WaitForVisible(m_app, "createWalletFormPage");
        QVERIFY(form);
        QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", QStringLiteral("Original modal draft")));
        QPointer<QObject> name{qmltest::Find(m_app, "createWalletNameInput")};
        QVERIFY(name);
        auto* node_tab = qmltest::Find(m_app, "blockClockTabButton");
        auto* settings_tab = qmltest::Find(m_app, "desktopWalletSettingsTabButton");
        QVERIFY(node_tab && settings_tab);
        QVERIFY(node_tab->property("checked").toBool());
        // Deliver the actual MenuCommand used by the native File menu. This
        // bypasses popup pointer interception, as the native menu does.
        QVERIFY(QMetaObject::invokeMethod(settings, "trigger"));
        QVERIFY(node_tab->property("checked").toBool());
        QVERIFY(!settings_tab->property("checked").toBool());
        QVERIFY(modal->property("visible").toBool());
        QCOMPARE(qmltest::Find(m_app, "createWalletFormPage"), form);
        QCOMPARE(name->property("text").toString(), QStringLiteral("Original modal draft"));
        QVERIFY(!settings->property("enabled").toBool());
    }
};

std::unique_ptr<QObject> CreateNavigationTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<NavigationIntegrationTests>(app);
}

#include <test_navigation.moc>
