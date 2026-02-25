// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtQuickTest/quicktest.h>

#include <QQmlEngine>

class QmlTestsSetup : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->addImportPath(QStringLiteral(BITCOINQML_QML_SOURCE_DIR));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(bitcoinqml_qmltests, QmlTestsSetup)

#include "qml_tests_main.moc"
