// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/testbridge.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <memory>
#include <vector>

class TestBridgeTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void traversalPreservesDepthOrderAndSharedIdentity()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QQmlApplicationEngine engine;
        engine.loadData("import QtQuick 2.15; Item {objectName: 'root'}");
        QCOMPARE(engine.rootObjects().size(), 1);
        auto* root = qobject_cast<QQuickItem*>(engine.rootObjects().front());
        QVERIFY(root);
        auto* shared = new QQuickItem(root);
        shared->setObjectName("shared"); // Both QObject and visual child.
        std::vector<std::unique_ptr<QObject>> chain;
        QObject* parent = shared;
        constexpr int depth{1200};
        for (int i = 0; i < depth; ++i) {
            auto child = std::make_unique<QObject>(parent);
            child->setObjectName(QString::number(i));
            parent = child.get();
            chain.push_back(std::move(child));
        }
        auto visual_only = std::make_unique<QQuickItem>();
        visual_only->setParentItem(root);
        visual_only->setObjectName("visual-only");
        TestBridge bridge{&engine, directory.filePath("bridge.sock")};
        QLocalSocket client;
        client.connectToServer(directory.filePath("bridge.sock"));
        QVERIFY(client.waitForConnected(1000));
        client.write("{\"cmd\":\"list_objects\"}\n");
        client.flush();
        QTRY_VERIFY(client.canReadLine());
        const auto objects = QJsonDocument::fromJson(client.readLine()).object().value("objects").toArray();
        QCOMPARE(objects.size(), depth + 3);
        QCOMPARE(objects[0].toObject().value("objectName").toString(), QString{"root"});
        QCOMPARE(objects[1].toObject().value("objectName").toString(), QString{"shared"});
        for (int i = 0; i < depth; ++i) {
            const auto entry = objects[i + 2].toObject();
            QCOMPARE(entry.value("objectName").toString(), QString::number(i));
            QCOMPARE(entry.value("depth").toInt(), i + 2);
        }
        QCOMPARE(objects.last().toObject().value("objectName").toString(), QString{"visual-only"});
        QCOMPARE(objects.last().toObject().value("depth").toInt(), 1);
        client.disconnectFromServer();
        // Destroy the fixture iteratively too, independently of traversal.
        while (!chain.empty()) chain.pop_back();
    }
};

#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(TestBridgeTests)
#include <test_testbridge.moc>
