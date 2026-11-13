// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/monospaceoutputmodel.h>
#include <test/mocks/manualexecutionenvironment.h>

#include <QScopeGuard>
#include <QGuiApplication>
#include <QClipboard>
#include <QtTest/QtTest>

namespace {
class Source final : public QAbstractListModel {
public:
    QStringList lines;
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : lines.size(); }
    QVariant data(const QModelIndex& index, int role) const override { return index.isValid() && role == Qt::UserRole ? QVariant{lines.at(index.row())} : QVariant{}; }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "content"}}; }
};
struct Ports {
    ManualExecutionEnvironment env;
    std::deque<std::function<void()>> releases, retries;
    bool reject_retirement{false};
    MonospaceOutputModel::Services services() {
        return {env.scheduler, [&](auto callback) { env.post(std::move(callback)); },
            [&](auto release) { if (reject_retirement) return false; releases.push_back(std::move(release)); return true; },
            [&](auto retry) { retries.push_back(std::move(retry)); }};
    }
    void releaseAll() { while (!releases.empty()) { auto release = std::move(releases.front()); releases.pop_front(); release(); } }
    void settle() {
        for (int i = 0; i < 1000; ++i) {
            env.drain(); env.publishAll(); releaseAll(); env.publishAll();
            if (env.quiescent() && releases.empty()) break;
        }
    }
};
}

class MonospaceOutputModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void projectionAndSearchWaitForWorkerAndPresentation()
    {
        Ports ports;
        Source source; source.lines = {"first line<br>İstanbul SAVINGS", "last .*+?"};
        MonospaceOutputModel model;
        model.setServices(ports.services()); model.setRichText(true); model.setSourceModel(&source);
        const auto cleanup = qScopeGuard([&] { model.closeAdmission(); ports.settle(); MonospaceOutputModel::retryRetirements(); });
        QCoreApplication::processEvents(); // Bounded GUI role capture only.
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!ports.env.turnsIdle());
        ports.env.drain();
        QCOMPARE(model.rowCount(), 0);
        ports.env.publishAll();
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.data(model.index(1, 0), MonospaceOutputModel::PlainContentRole).toString(), QStringLiteral("İstanbul SAVINGS"));
        model.setSearchText("savings");
        QVERIFY(model.searchBusy()); QCOMPARE(model.searchResultCount(), 0);
        ports.env.drain(); QCOMPARE(model.searchResultCount(), 0);
        ports.env.publishAll();
        QCOMPARE(model.searchResultCount(), 1); QCOMPARE(model.currentSearchRow(), 1);
        QCOMPARE(model.currentSearchStart(), 9); QCOMPARE(model.currentSearchEnd(), 16);
        model.setSearchText(".*+?"); ports.settle();
        QCOMPARE(model.searchResultCount(), 1); QCOMPARE(model.currentSearchRow(), 2);
        QCOMPARE(model.currentSearchStart(), 5); QCOMPARE(model.currentSearchEnd(), 9);
        QSignalSpy copied(&model, &MonospaceOutputModel::copyFinished);
        QGuiApplication::clipboard()->setText(QStringLiteral("before"));
        model.copyAll(); ports.env.drain();
        QCOMPARE(copied.size(), 0);
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("before"));
        ports.env.publishAll();
        QCOMPARE(copied.size(), 1); QVERIFY(copied.front().front().toBool());
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("first line\nİstanbul SAVINGS\nlast .*+?"));
    }

    void retirementGateRetainsRejectedHandoffAndWaitsForActualRelease()
    {
        Ports ports;
        MonospaceOutputModel::setDefaultServices(ports.services());
        Source source; source.lines = {QString(5000, QLatin1Char('x'))};
        auto model = std::make_unique<MonospaceOutputModel>();
        model->setSourceModel(&source);
        QCoreApplication::processEvents(); ports.settle();
        QCOMPARE(model->rowCount(), 5);
        ports.reject_retirement = true;
        QVERIFY(!MonospaceOutputModel::closeAll());
        QVERIFY(MonospaceOutputModel::pendingRetirements() > 0);
        bool done{false};
        MonospaceOutputModel::whenRetired(this, [&] { done = true; });
        QCoreApplication::processEvents();
        QVERIFY(!done); QVERIFY(!ports.retries.empty());
        model.reset(); // Deferred mailbox retains rejected large references.
        ports.reject_retirement = false;
        auto retry = std::move(ports.retries.front()); ports.retries.pop_front(); retry();
        QVERIFY(!done); // Admission alone is not resource retirement.
        QVERIFY(!ports.releases.empty());
        ports.releaseAll();
        while (!ports.retries.empty()) { auto next = std::move(ports.retries.front()); ports.retries.pop_front(); next(); }
        ports.env.publishAll();
        QVERIFY(done); QCOMPARE(MonospaceOutputModel::pendingRetirements(), size_t{0});
        MonospaceOutputModel::setDefaultServices({});
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(MonospaceOutputModelTests)
#else
QTEST_MAIN(MonospaceOutputModelTests)
#endif
#include <test_monospaceoutputmodel.moc>
