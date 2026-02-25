// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/guiconstants.h>
#include <qml/networkstyle.h>

class NetworkStyleTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void instantiate_knownNetworks_haveExpectedNames();
    void instantiate_nonMainNetwork_hasBracketedChainTitleSuffix();
};

void NetworkStyleTests::instantiate_knownNetworks_haveExpectedNames()
{
    const NetworkStyle* mainnet_style = NetworkStyle::instantiate(ChainType::MAIN);
    QVERIFY(mainnet_style != nullptr);
    QCOMPARE(mainnet_style->getAppName(), QString(QAPP_APP_NAME_DEFAULT));
    QCOMPARE(mainnet_style->getTitleAddText(), QString());
    delete mainnet_style;

    const NetworkStyle* signet_style = NetworkStyle::instantiate(ChainType::SIGNET);
    QVERIFY(signet_style != nullptr);
    QCOMPARE(signet_style->getAppName(), QString(QAPP_APP_NAME_SIGNET));
    delete signet_style;
}

void NetworkStyleTests::instantiate_nonMainNetwork_hasBracketedChainTitleSuffix()
{
    const NetworkStyle* regtest_style = NetworkStyle::instantiate(ChainType::REGTEST);
    QVERIFY(regtest_style != nullptr);

    const QString title_suffix = regtest_style->getTitleAddText();
    QVERIFY(title_suffix.startsWith('['));
    QVERIFY(title_suffix.endsWith(']'));
    QVERIFY(title_suffix.contains(QStringLiteral("regtest")));
    delete regtest_style;
}

int RunNetworkStyleTests(int argc, char* argv[])
{
    NetworkStyleTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#ifndef BITCOINQML_NO_TEST_MAIN
QTEST_MAIN(NetworkStyleTests)
#endif
#include "test_networkstyle.moc"
