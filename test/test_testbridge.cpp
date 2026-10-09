// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/testbridge.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {
class TextItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text)
public:
    explicit TextItem(QQuickItem* parent) : QQuickItem(parent) {}
    QString text() const { ++reads; return value; }
    QString value;
    mutable int reads{0};
};

struct BridgeFixture {
    QQmlApplicationEngine engine;
    QTemporaryDir directory;
    TestBridge bridge{&engine, directory.filePath(QStringLiteral("bridge.sock"))};
    QLocalSocket socket;

    BridgeFixture()
    {
        engine.loadData("import QtQuick 2.15\nItem { objectName: \"root\" }");
        socket.connectToServer(directory.filePath(QStringLiteral("bridge.sock")));
    }

    QJsonObject request(const QJsonObject& command)
    {
        if (!QTest::qWaitFor([&] { return socket.state() == QLocalSocket::ConnectedState; }, 5000)) return {};
        socket.write(QJsonDocument{command}.toJson(QJsonDocument::Compact) + '\n');
        socket.flush();
        QByteArray response;
        if (!QTest::qWaitFor([&] {
            response += socket.readAll();
            return response.contains('\n');
        }, 5000)) return {};
        return QJsonDocument::fromJson(response).object();
    }

    QQuickItem* root() const
    {
        return engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickItem*>(engine.rootObjects().front());
    }
};
} // namespace

class TestBridgeTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void snapshotPreservesQObjectThenVisualDepthFirstOrder()
    {
        BridgeFixture fixture;
        QVERIFY(fixture.root());
        auto* branch = new QObject{fixture.root()};
        branch->setObjectName(QStringLiteral("owned branch"));
        auto* leaf = new TextItem{nullptr};
        leaf->setParent(branch);
        leaf->setObjectName(QStringLiteral("owned leaf"));
        auto* sibling = new TextItem{fixture.root()};
        sibling->setObjectName(QStringLiteral("owned sibling"));

        // These items are reachable through visual parenting only. Their
        // separate QObject owner is deliberately outside the engine's roots.
        QQuickItem external_owner;
        auto* first_visual = new TextItem{&external_owner};
        first_visual->setObjectName(QStringLiteral("first visual"));
        first_visual->setParentItem(fixture.root());
        auto* visual_leaf = new TextItem{first_visual};
        visual_leaf->setObjectName(QStringLiteral("visual leaf"));
        auto* second_visual = new TextItem{&external_owner};
        second_visual->setObjectName(QStringLiteral("second visual"));
        second_visual->setParentItem(fixture.root());

        const auto snapshot = fixture.request({{QStringLiteral("cmd"), QStringLiteral("list_objects")}});
        QVERIFY(snapshot.contains(QStringLiteral("objects")));
        QStringList names;
        QList<int> depths;
        for (const auto& entry : snapshot.value(QStringLiteral("objects")).toArray()) {
            const auto object = entry.toObject();
            names.append(object.value(QStringLiteral("objectName")).toString());
            depths.append(object.value(QStringLiteral("depth")).toInt());
        }
        QCOMPARE(names, QStringList({QStringLiteral("root"), QStringLiteral("owned branch"), QStringLiteral("owned leaf"), QStringLiteral("owned sibling"), QStringLiteral("first visual"), QStringLiteral("visual leaf"), QStringLiteral("second visual")}));
        QCOMPARE(depths, QList<int>({0, 1, 2, 1, 1, 2, 1}));
        QCOMPARE(leaf->reads + sibling->reads + first_visual->reads + visual_leaf->reads + second_visual->reads, 0);
    }

    void textSnapshotIsOptInAndFresh()
    {
        BridgeFixture fixture;
        QVERIFY(fixture.root());
        auto* item = new TextItem{fixture.root()};
        item->setObjectName(QStringLiteral("input"));
        item->value = QStringLiteral("first");

        const auto plain = fixture.request({{QStringLiteral("cmd"), QStringLiteral("list_objects")}});
        QVERIFY(plain.contains(QStringLiteral("objects")));
        for (const auto& entry : plain.value(QStringLiteral("objects")).toArray()) {
            QVERIFY(!entry.toObject().contains(QStringLiteral("text")));
        }
        QCOMPARE(item->reads, 0);

        for (const QString& value : {QStringLiteral("first"), QStringLiteral("second")}) {
            item->value = value;
            const auto snapshot = fixture.request({{QStringLiteral("cmd"), QStringLiteral("list_objects")}, {QStringLiteral("includeText"), true}});
            QVERIFY(snapshot.contains(QStringLiteral("objects")));
            int inputs{0};
            for (const auto& entry : snapshot.value(QStringLiteral("objects")).toArray()) {
                const auto object = entry.toObject();
                if (object.value(QStringLiteral("objectName")) == QStringLiteral("input")) {
                    ++inputs;
                    QCOMPARE(object.value(QStringLiteral("text")).toString(), value);
                }
            }
            QCOMPARE(inputs, 1);
        }
        QCOMPARE(item->reads, 2);
    }

    void snapshotVisitsSharedAndCyclicEdgesOnceWithoutMergingNames()
    {
        BridgeFixture fixture;
        QVERIFY(fixture.root());
        auto* hidden = new TextItem{fixture.root()};
        auto* visible = new TextItem{hidden};
        hidden->setObjectName(QStringLiteral("duplicate"));
        visible->setObjectName(QStringLiteral("duplicate"));
        hidden->value = QStringLiteral("seeded passphrase");
        visible->value = QStringLiteral("public text");

        // QObject ownership is root -> hidden -> visible, while visual
        // parenting is root -> visible -> hidden: the combined graph cycles.
        visible->setParentItem(fixture.root());
        hidden->setParentItem(visible);
        hidden->setVisible(false);
        const auto restore_parents = qScopeGuard([&] {
            hidden->setParentItem(fixture.root());
            visible->setParentItem(hidden);
        });

        const auto snapshot = fixture.request({{QStringLiteral("cmd"), QStringLiteral("list_objects")}, {QStringLiteral("includeText"), true}});
        QVERIFY(snapshot.contains(QStringLiteral("objects")));
        QStringList texts;
        for (const auto& entry : snapshot.value(QStringLiteral("objects")).toArray()) {
            const auto object = entry.toObject();
            if (object.value(QStringLiteral("objectName")) == QStringLiteral("duplicate")) {
                texts.append(object.value(QStringLiteral("text")).toString());
            }
        }
        QCOMPARE(texts, QStringList({QStringLiteral("seeded passphrase"), QStringLiteral("public text")}));
        QCOMPARE(hidden->reads, 1);
        QCOMPARE(visible->reads, 1);

        // Ordinary name lookup keeps its existing visible-object priority.
        const auto selected = fixture.request({{QStringLiteral("cmd"), QStringLiteral("get_property")}, {QStringLiteral("objectName"), QStringLiteral("duplicate")}, {QStringLiteral("prop"), QStringLiteral("text")}});
        QCOMPARE(selected.value(QStringLiteral("value")).toString(), QStringLiteral("public text"));
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(TestBridgeTests)
#else
QTEST_MAIN(TestBridgeTests)
#endif
#include "test_testbridge.moc"
