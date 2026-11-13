// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/testbridge.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractListModel>
#include <QLocalSocket>
#include <QQuickItem>
#include <QPointer>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <memory>
#include <vector>
#include <limits>

class BridgeActionPort final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(quint64 operationId READ operationId CONSTANT)
    Q_PROPERTY(Phase phase READ phase CONSTANT)
public:
    enum class Phase { Waiting, Ready };
    Q_ENUM(Phase)
    quint64 ticket{0};
    int requests{0}, boolean_calls{0}, void_calls{0};
    QString original_path;
    quint64 operationId() const { return ticket; }
    Phase phase() const { return Phase::Ready; }
    Q_INVOKABLE quint64 request(const QString& path) { ++requests; original_path = path; return ticket; }
    Q_INVOKABLE quint64 requestOriginal() { ++requests; return ticket; }
    Q_INVOKABLE bool accepted() { ++boolean_calls; return true; }
    Q_INVOKABLE void unchanged() { ++void_calls; }
};

class BridgeRows final : public QAbstractListModel
{
public:
    QVariantMap entry{{QStringLiteral("text"), QStringLiteral("original")}, {QStringLiteral("isChange"), true},
        {QStringLiteral("amount"), QVariant::fromValue<long>(500000)}};
    mutable int reads{0};
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 1; }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() != 0 || role != Qt::UserRole) return {};
        ++reads; return entry;
    }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "entry"}}; }
};

struct BridgeFixture {
    QTemporaryDir directory;
    QQmlApplicationEngine engine;
    BridgeActionPort port;
    BridgeRows rows;
    std::unique_ptr<TestBridge> bridge;
    QLocalSocket client;
    bool valid{false};
    BridgeFixture() {
        if (!directory.isValid()) return;
        engine.loadData("import QtQuick 2.15; Item {objectName: 'root'}");
        if (engine.rootObjects().size() != 1) return;
        auto* root = engine.rootObjects().front();
        port.setParent(root); rows.setParent(root);
        root->setProperty("port", QVariant::fromValue(static_cast<QObject*>(&port)));
        port.setProperty("rows", QVariant::fromValue(static_cast<QObject*>(&rows)));
        port.setProperty("revision", QVariant::fromValue(quint64{7}));
        bridge = std::make_unique<TestBridge>(&engine, directory.filePath("bridge.sock"));
        client.connectToServer(directory.filePath("bridge.sock"));
        valid = client.waitForConnected(1000);
    }
    QJsonObject request(const QJsonObject& command) {
        client.write(QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n'); client.flush();
        if (!QTest::qWaitFor([&] { return client.canReadLine(); }, 1000)) {
            QTest::qFail("Original bridge request did not return", __FILE__, __LINE__); return {};
        }
        return QJsonDocument::fromJson(client.readLine()).object();
    }
};

class TestBridgeTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void originalActionTicketsRemainLossless_data() {
        QTest::addColumn<quint64>("ticket");
        QTest::newRow("refused-zero") << quint64{0};
        QTest::newRow("full-uint64") << std::numeric_limits<quint64>::max();
    }
    void originalActionTicketsRemainLossless() {
        QFETCH(quint64, ticket); BridgeFixture f; QVERIFY(f.valid); f.port.ticket = ticket;
        for (const auto& method : {QStringLiteral("request"), QStringLiteral("requestOriginal")}) {
            QJsonObject command{{"cmd", "invoke_property_object"}, {"objectName", "root"}, {"prop", "port"}, {"method", method}};
            if (method == QLatin1String("request")) command["args"] = QJsonArray{QStringLiteral("owned-path")};
            const auto result = f.request(command); QVERIFY(!result.contains("error"));
            QCOMPARE(result.value("valueType").toString(), QStringLiteral("quint64"));
            QCOMPARE(result.value("value").toString(), QString::number(ticket));
        }
        QCOMPARE(f.port.requests, 2); QCOMPARE(f.port.original_path, QStringLiteral("owned-path"));
        const auto observed = f.request({{"cmd", "get_property"}, {"objectName", "root"}, {"prop", "port.operationId"}});
        QCOMPARE(observed.value("valueType").toString(), QStringLiteral("quint64"));
        QCOMPARE(observed.value("value").toString(), QString::number(ticket));
        const auto boolean = f.request({{"cmd", "invoke_property_object"}, {"objectName", "root"}, {"prop", "port"}, {"method", "accepted"}});
        QVERIFY(boolean.value("value").isBool()); QVERIFY(boolean.value("value").toBool()); QCOMPARE(f.port.boolean_calls, 1);
        const auto nothing = f.request({{"cmd", "invoke_property_object"}, {"objectName", "root"}, {"prop", "port"}, {"method", "unchanged"}});
        QVERIFY(!nothing.contains("error")); QVERIFY(!nothing.contains("value")); QCOMPARE(f.port.void_calls, 1);
    }
    void namedEnumsUseActualMetaObject() {
        BridgeFixture f; QVERIFY(f.valid);
        const auto result = f.request({{"cmd", "get_enum_property"}, {"objectName", "root"}, {"prop", "port.phase"}});
        QVERIFY(!result.contains("error")); QCOMPARE(result.value("enum").toString(), QStringLiteral("Phase"));
        QCOMPARE(result.value("key").toString(), QStringLiteral("Ready"));
        const auto rejected = f.request({{"cmd", "get_enum_property"}, {"objectName", "root"}, {"prop", "port.operationId"}});
        QVERIFY(rejected.contains("error")); QCOMPARE(f.port.requests, 0);
    }
    void modelRowsStayBoundedAndOriginal_data() {
        QTest::addColumn<int>("case_kind");
        QTest::newRow("original") << 0; QTest::newRow("stale-revision") << 1;
        QTest::newRow("wrong-inventory") << 2; QTest::newRow("oversized-role") << 3;
        QTest::newRow("same-revision-replacement-model") << 4;
        QTest::newRow("oversized-string-list") << 5;
    }
    void modelRowsStayBoundedAndOriginal() {
        QFETCH(int, case_kind); BridgeRows replacement; BridgeFixture f; QVERIFY(f.valid);
        if (case_kind == 3) f.rows.entry[QStringLiteral("text")] = QString(1025, 'x');
        if (case_kind == 5) {
            QStringList values; values.reserve(33);
            for (int row{0}; row < 33; ++row) values.append(QStringLiteral("x"));
            f.rows.entry[QStringLiteral("text")] = values;
        }
        const auto original = f.request({{"cmd", "get_model_row"}, {"objectName", "root"}, {"prop", "port.rows"},
            {"row", -1}, {"revisionProperty", "port.revision"}, {"revision", "7"}});
        QVERIFY(!original.contains("error")); QCOMPARE(f.rows.reads, 0);
        if (case_kind == 4) f.port.setProperty("rows", QVariant::fromValue(static_cast<QObject*>(&replacement)));
        QJsonObject command{{"cmd", "get_model_row"}, {"objectName", "root"}, {"prop", "port.rows"},
            {"row", 0}, {"role", "entry"}, {"expectedRows", case_kind == 2 ? 2 : 1},
            {"modelId", original.value("modelId")}, {"revisionProperty", "port.revision"}, {"revision", case_kind == 1 ? "6" : "7"}};
        const auto result = f.request(command);
        if (case_kind != 0) {
            QVERIFY(result.contains("error")); QCOMPARE(f.rows.reads, case_kind == 3 || case_kind == 5 ? 1 : 0);
        } else {
            QVERIFY(!result.contains("error")); QCOMPARE(result.value("rowCount").toInt(), 1);
            QCOMPARE(result.value("revision").toString(), QStringLiteral("7"));
            QCOMPARE(result.value("value").toObject().value("text").toString(), QStringLiteral("original"));
            QVERIFY(result.value("value").toObject().value("isChange").toBool()); QCOMPARE(f.rows.reads, 1);
            QCOMPARE(result.value("value").toObject().value("amount").toVariant().toLongLong(), qlonglong{500000});
            command["role"] = QStringLiteral("missing"); QVERIFY(f.request(command).contains("error"));
            QCOMPARE(f.rows.reads, 1);
            f.port.setProperty("revision", QVariant::fromValue(quint64{8}));
            command["role"] = QStringLiteral("entry"); QVERIFY(f.request(command).contains("error"));
            QCOMPARE(f.rows.reads, 1);
        }
        QCOMPARE(replacement.reads, 0);
        QCOMPARE(f.rows.entry.value(QStringLiteral("isChange")).toBool(), true);
    }
    void modelObservationInventoryReclaimsDestroyedOwners() {
        BridgeFixture f; QVERIFY(f.valid);
        std::vector<std::unique_ptr<BridgeRows>> models; models.reserve(129);
        QString first_id, last_id;
        const QJsonObject metadata{{"cmd", "get_model_row"}, {"objectName", "root"}, {"prop", "port.rows"}, {"row", -1}};
        for (int row{0}; row < 128; ++row) {
            models.push_back(std::make_unique<BridgeRows>());
            f.port.setProperty("rows", QVariant::fromValue(static_cast<QObject*>(models.back().get())));
            const auto result = f.request(metadata); QVERIFY(!result.contains("error"));
            last_id = result.value("modelId").toString();
            if (!row) first_id = last_id;
            QCOMPARE(models.back()->reads, 0);
        }
        models.push_back(std::make_unique<BridgeRows>());
        f.port.setProperty("rows", QVariant::fromValue(static_cast<QObject*>(models.back().get())));
        QVERIFY(f.request(metadata).contains("error")); QCOMPARE(models.back()->reads, 0);
        const QPointer<BridgeRows> original{models.front().get()}; models.front().reset();
        QVERIFY(!original); // Observation is weak; destruction refunds capacity.
        const auto retried = f.request(metadata); QVERIFY(!retried.contains("error"));
        const auto new_id = retried.value("modelId").toString();
        QVERIFY(new_id != first_id && new_id != last_id);
        auto stale = metadata; stale["modelId"] = first_id;
        QVERIFY(f.request(stale).contains("error")); QCOMPARE(models.back()->reads, 0);
    }
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
