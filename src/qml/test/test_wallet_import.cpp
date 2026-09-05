// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/wallet_test_fixture.h>
#include <qml/wallet/walletmanager.h>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

class WalletImportTests : public QObject
{
    Q_OBJECT
public:
    explicit WalletImportTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void restore()
    {
        WalletTestFixture fixture(m_app.node());
        auto wallet = fixture.create();
        QVERIFY(wallet->backupWallet(fixture.backupPath().toStdString()));
        m_target = QStringLiteral("restore-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto& model = *m_app.walletManager()->importModel();
        QVERIFY(!model.restore(QStringLiteral("https://example.invalid/wallet.bak"), m_target));
        QCOMPARE(model.resultCode(), int{WalletOperationResult::InvalidInput});
        QSignalSpy success(&model, &WalletImportModel::succeeded);
        QVERIFY(model.restore(QUrl::fromLocalFile(fixture.backupPath()).toString(), m_target));
        QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 20'000);
        QCOMPARE(model.error(), QString{});
        QCOMPARE(success.size(), 1);
        QCOMPARE(m_app.walletManager()->selectedWallet()->overview()->name(), m_target);
        QVERIFY(!model.restore(fixture.backupPath(), m_target));
    }
    void migrate_data()
    {
        QTest::addColumn<QString>("filename");
        QTest::addColumn<QString>("password");
        QTest::newRow("unencrypted") << QStringLiteral("legacy-unencrypted.bak") << QString{};
        QTest::newRow("encrypted") << QStringLiteral("legacy-encrypted.bak") << QStringLiteral("qml-legacy-test-only");
        QTest::newRow("companions") << QStringLiteral("legacy-companions.bak") << QString{};
    }
    void migrate()
    {
        QFETCH(QString, filename);
        QFETCH(QString, password);
        WalletTestFixture fixture(m_app.node());
        const auto source = fixture.copyCanonicalBackup(filename);
        auto& manager = *m_app.walletManager();
        m_target = QStringLiteral("migrate-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (filename == QStringLiteral("legacy-companions.bak"))
            m_companions = {m_target + QStringLiteral("_watchonly"), m_target + QStringLiteral("_solvables")};
        const QString directory = manager.canonicalIdentity(m_target);
        QVERIFY(QDir{}.mkpath(directory));
        QVERIFY(QFile::copy(source, QDir(directory).filePath(QStringLiteral("wallet.dat"))));
        manager.refresh();
        auto& model = *manager.migration();
        QVERIFY(model.inspect(m_target));
        QCOMPARE(model.needsPassphrase(), !password.isEmpty());
        if (!password.isEmpty()) {
            QVERIFY(model.migrate(QStringLiteral("wrong-test-password")));
            QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 20'000);
            QVERIFY(!model.error().isEmpty());
            QVERIFY(model.inspect(m_target));
        }
        QSignalSpy success(&model, &WalletMigrationModel::succeeded);
        QStringList selected_results;
        QObject selection_context;
        const auto selection_connection = connect(&manager, &WalletManager::selectedWalletChanged, &selection_context, [&] {
            if (manager.selectedWallet()) selected_results.append(manager.selectedWallet()->overview()->name());
        });
        QVERIFY(model.migrate(password));
        QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 20'000);
        disconnect(selection_connection);
        QCOMPARE(model.error(), QString{});
        QCOMPARE(success.size(), 1);
        QVERIFY(QFileInfo::exists(model.backupPath()));
        QCOMPARE(manager.selectedWallet()->overview()->name(), m_target);
        QCOMPARE(manager.selectedWallet()->overview()->encrypted(), !password.isEmpty());
        QCOMPARE(selected_results.count(m_target), 1);
        if (filename == QStringLiteral("legacy-companions.bak")) {
            const QStringList expected{m_target + QStringLiteral("_watchonly"), m_target + QStringLiteral("_solvables")};
            QCOMPARE(model.companions(), expected);
            m_companions = expected;
            for (const auto& companion : expected) {
                const auto occurrences = [&] {
                    int count{0};
                    for (int row = 0; row < manager.catalog()->rowCount(); ++row) {
                        const auto index = manager.catalog()->index(row);
                        if (manager.catalog()->data(index, WalletListModel::NameRole).toString() == companion &&
                            manager.catalog()->data(index, WalletListModel::LoadedRole).toBool()) ++count;
                    }
                    return count;
                };
                QTRY_COMPARE(occurrences(), 1);
            }
            // Queued load events must not steal selection from the primary result.
            QCoreApplication::processEvents();
            QCOMPARE(manager.selectedWallet()->overview()->name(), m_target);
        } else {
            QVERIFY(model.companions().isEmpty());
        }
    }
    void cleanup()
    {
        if (m_target.isEmpty()) return;
        auto& manager = *m_app.walletManager();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 20'000);
        manager.closeWallet(m_target);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 20'000);
        for (const auto& name : m_companions) {
            manager.closeWallet(name);
            QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 20'000);
        }
        m_companions.clear();
        m_target.clear();
    }
private:
    BitcoinQmlApplication& m_app;
    QString m_target;
    QStringList m_companions;
};
BITCOINQML_REGISTER_INTEGRATION_TEST(WalletImportTests)

#include <test_wallet_import.moc>
