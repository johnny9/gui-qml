// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/initexecutor.h>

#include <QMetaObject>
#include <QMetaMethod>
#include <QObject>
#include <QTest>

#include <type_traits>

class QmlInitExecutorApiTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void exposesExpectedQObjectApi();
};

static bool HasMethodByName(const QMetaObject& meta, const QByteArray& method_name)
{
    for (int i = 0; i < meta.methodCount(); ++i) {
        if (meta.method(i).name() == method_name) return true;
    }
    return false;
}

void QmlInitExecutorApiTests::exposesExpectedQObjectApi()
{
    QVERIFY((std::is_base_of_v<QObject, QmlInitExecutor>));
    QVERIFY((std::is_constructible_v<QmlInitExecutor, interfaces::Node&>));

    const QMetaObject& meta = QmlInitExecutor::staticMetaObject;

    QVERIFY(HasMethodByName(meta, "initialize"));
    QVERIFY(HasMethodByName(meta, "shutdown"));
    QVERIFY(HasMethodByName(meta, "initializeResult"));
    QVERIFY(HasMethodByName(meta, "shutdownResult"));
    QVERIFY(HasMethodByName(meta, "runawayException"));
}

int RunQmlInitExecutorApiTests(int argc, char* argv[])
{
    QmlInitExecutorApiTests tc;
    return QTest::qExec(&tc, argc, argv);
}

#include <test_qmlinitexecutor_api.moc>
