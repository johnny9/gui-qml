// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/fileexportoperation.h>
#include <qml/psbtwork.h>
#include <chainparams.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/psbtfeefixture.h>
#include <test/qt_test_registry.h>

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <deque>

namespace {
struct Fixture {
    ManualRoutedExecution execution;
    std::deque<std::function<void()>> gui_posts;

    backup::Services services()
    {
        backup::Services result;
        result.scheduler = execution.scheduler;
        result.post = [this](std::function<void()> work) { gui_posts.push_back(std::move(work)); };
        result.coordinator = std::make_shared<backup::InstallCoordinator>();
        return result;
    }
    void deliverGui()
    {
        while (!gui_posts.empty()) {
            auto work = std::move(gui_posts.front());
            gui_posts.pop_front();
            work();
        }
    }
};
QByteArray ReadFile(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
}

class FileExportOperationTests : public QObject
{
    Q_OBJECT
    std::unique_ptr<ECC_Context> m_ecc;
private Q_SLOTS:
    void initTestCase()
    {
        SelectParams(ChainType::MAIN);
        m_ecc = std::make_unique<ECC_Context>();
    }

    void exactCapturedPsbtBytesUseApprovedFinalPath()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("captured.psbt"));
        const auto document = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
        const QByteArray expected = psbtwork::SerializeRaw(*document);
        QVERIFY(!expected.isEmpty());
        Fixture fixture;
        FileExportOperationModel operation{path, document, fixture.services()};

        QVERIFY(operation.start());
        QVERIFY(operation.pending());
        QVERIFY(!QFile::exists(path));
        fixture.execution.drain();
        fixture.deliverGui();

        QVERIFY(operation.saved());
        QVERIFY(!operation.pending());
        QCOMPARE(operation.finalPath(), path);
        QCOMPARE(ReadFile(path), expected);
        QVERIFY(operation.errorText().isEmpty());
    }

    void overwriteNeedsApprovalAndRechecksDestinationVersion()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("existing.psbt"));
        const QByteArray original{"previous reviewed export"};
        const QByteArray intervening{"concurrent replacement"};
        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), qint64(original.size()));
        }
        const auto document = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
        Fixture fixture;
        FileExportOperationModel operation{path, document, fixture.services()};

        QVERIFY(operation.start());
        fixture.execution.drain();
        fixture.deliverGui();
        QVERIFY(operation.pending());
        QVERIFY(operation.confirmationRequired());
        QVERIFY(operation.replacing());
        QCOMPARE(ReadFile(path), original);

        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(file.write(intervening), qint64(intervening.size()));
        }
        QVERIFY(operation.approve());
        fixture.execution.drain();
        fixture.deliverGui();

        QVERIFY(!operation.pending());
        QVERIFY(!operation.saved());
        QVERIFY(operation.errorText().contains(QStringLiteral("changed"), Qt::CaseInsensitive));
        QCOMPARE(ReadFile(path), intervening);
    }

    void refusalDoesNotReplaceExistingFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("existing.psbt"));
        const QByteArray original{"do not overwrite"};
        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), qint64(original.size()));
        }
        const auto document = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
        Fixture fixture;
        FileExportOperationModel operation{path, document, fixture.services()};

        QVERIFY(operation.start());
        fixture.execution.drain();
        fixture.deliverGui();
        QVERIFY(operation.confirmationRequired());
        QVERIFY(operation.refuse());
        fixture.execution.drain();
        fixture.deliverGui();

        QVERIFY(!operation.pending());
        QVERIFY(!operation.saved());
        QVERIFY(operation.errorText().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
        QCOMPARE(ReadFile(path), original);
    }
};

BITCOINQML_REGISTER_QT_TEST(FileExportOperationTests)

#include <test/test_fileexportoperation.moc>
