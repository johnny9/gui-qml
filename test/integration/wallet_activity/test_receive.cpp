// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/mocks/messagebumptest.h>
#include <test/mocks/sendtest.h>
#include <test/mocks/preparationtest.h>
#include <test/mocks/receiverequesttest.h>
#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <qml/models/activityfilterproxymodel.h>
#include <qml/models/addresslistmodel.h>
#include <qml/walletrequests.h>

#include <common/args.h>
#include <key_io.h>
#include <util/string.h>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QSignalSpy>
#include <QLocale>
#include <QScopeGuard>
#include <QClipboard>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QPersistentModelIndex>
#include <qml/models/options_model.h>
#include <streams.h>

class ReceiveIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

    QObject* addressRow(const QString& address)
    {
        QList<QQuickItem*> pending{m_app.window()->contentItem()};
        while (!pending.isEmpty()) {
            auto* item = pending.takeLast();
            if (item->objectName() == "addressRow" && item->property("address").toString() == address) return item;
            pending.append(item->childItems());
        }
        return nullptr;
    }

    bool hasTransaction(WalletQmlModel& wallet, const QString& txid)
    {
        auto* activity = wallet.transactionActivityModel();
        for (int row{0}; row < activity->rowCount(); ++row) {
            if (activity->index(row, 0).data(TransactionActivityModel::TxidRole).toString() == txid) return true;
        }
        return false;
    }

public:
    explicit ReceiveIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void sentNotesPreserveReceivingPurposeAndPublicInvoiceAfterRealCommit()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "sent-notes"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto name = wallet->name(), address = qmltest::NewAddress(m_app, name);
        QmlRecentRequestEntry entry; entry.id = 7; entry.date = QDateTime::fromSecsSinceEpoch(1'700'000'000);
        entry.recipient.address = address.toStdString(); entry.recipient.amount = 1234;
        entry.recipient.label = "public invoice"; entry.recipient.message = "public message"; entry.recipient.noteSelf = "private invoice note";
        const auto original = walletrequests::Encode(entry);
        QVERIFY(qmltest::Call(m_app, [name, address, original](interfaces::Node* node) {
            for (auto& candidate : node->walletLoader().getWallets()) if (candidate->getWalletName() == name.toStdString())
                return candidate->setAddressReceiveRequest(DecodeDestination(address.toStdString()), "7", original);
            return false;
        }));
        auto* recipient = wallet->sendRecipientList()->currentRecipient(); QVERIFY(recipient);
        recipient->address()->setAddress(address, 2); recipient->amount()->setSatoshi(100'000); recipient->setLabel("outgoing note");
        QTRY_VERIFY(!wallet->feeEstimatePending()); QVERIFY(AwaitPreparation(*wallet));
        QVERIFY(AwaitCommittedSend(*wallet));
        QTRY_VERIFY(!wallet->recipientLabelsPending()); QVERIFY2(wallet->recipientLabelsError().isEmpty(), qPrintable(wallet->recipientLabelsError()));
        const auto txid = wallet->recipientLabelsTransactionId(); QVERIFY(!txid.isEmpty());
        const auto mempool = qmltest::Rpc(m_app, "getrawmempool"); bool broadcast{false};
        for (const auto& id : mempool.getValues()) if (id.get_str() == txid.toStdString()) broadcast = true;
        QVERIFY(broadcast);
        QVERIFY(qmltest::Call(m_app, [name, address, original](interfaces::Node* node) {
            for (auto& candidate : node->walletLoader().getWallets()) {
                if (candidate->getWalletName() != name.toStdString()) continue;
                std::string label; wallet::AddressPurpose purpose;
                return candidate->getAddress(DecodeDestination(address.toStdString()), &label, &purpose) &&
                    label == "outgoing note" && purpose == wallet::AddressPurpose::RECEIVE &&
                    candidate->getAddressReceiveRequests() == std::vector<std::string>{original};
            }
            return false;
        }));
    }

    void committedSendReportsItsActualMempoolObservation_data()
    {
        QTest::addColumn<int>("scenario");
        QTest::newRow("accepted") << 0; QTest::newRow("broadcast-disabled") << 1; QTest::newRow("mempool-rejected") << 2;
    }
    void committedSendReportsItsActualMempoolObservation()
    {
        QFETCH(int, scenario);
        auto* wallet = qmltest::CreateWallet(m_app, "submission"); QVERIFY(wallet); QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto name = wallet->name(), address = qmltest::NewAddress(m_app, name);
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        wallet->setCustomFeeEnabled(true); wallet->setCustomFeeRate("1");
        auto* recipient = wallet->sendRecipientList()->currentRecipient(); QVERIFY(recipient);
        recipient->address()->setAddress(address, 2); recipient->amount()->setSatoshi(100'000);
        QVERIFY(AwaitPreparation(*wallet)); const auto approved = wallet->currentTransaction()->getWtx(); QVERIFY(approved);
        QString competing;
        if (scenario == 2) {
            UniValue inputs{UniValue::VARR};
            for (const auto& input : approved->vin) {
                UniValue coin{UniValue::VOBJ}; coin.pushKV("txid", input.prevout.hash.GetHex()); coin.pushKV("vout", input.prevout.n); inputs.push_back(coin);
            }
            const auto conflict = qmltest::Rpc(m_app, "sendall", qmltest::Json("{\"recipients\":[\"" + address.toStdString() + "\"],\"fee_rate\":20,\"options\":{\"inputs\":" + inputs.write() + "}}"), name);
            competing = QString::fromStdString(conflict.find_value("txid").get_str()); QVERIFY(!competing.isEmpty());
        }
        auto* page = qmltest::Find(m_app, "sendPage"); QVERIFY(page); QVERIFY(QMetaObject::invokeMethod(page, "openTransactionReview"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewSendButton"));
        QTRY_COMPARE(wallet->sendState(), WalletQmlModel::SendState::SendCommitted);
        QVERIFY(qmltest::WaitForVisible(m_app, "sendCompletePage"));
        auto* complete = qmltest::Find(m_app, "sendCompletePage"); QVERIFY(complete);
        const auto expected = scenario == 0 ? WalletQmlModel::SendSubmission::SubmissionMempool : WalletQmlModel::SendSubmission::SubmissionNotInMempool;
        QCOMPARE(wallet->sendSubmission(), expected); QCOMPARE(complete->property("submissionState").toInt(), static_cast<int>(expected));
        QCOMPARE(complete->property("titleText").toString(), scenario == 0 ? QString{"Transaction submitted"} : QString{"Transaction saved"});
        if (scenario != 0) QVERIFY(complete->property("descriptionText").toString().contains("not in the mempool"));
        const auto txid = QString::fromStdString(approved->GetHash().GetHex()); QCOMPARE(wallet->sendResultTxid(), txid);
        const auto saved = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + txid.toStdString() + "\"]"), name);
        QCOMPARE(QString::fromStdString(saved.find_value("txid").get_str()), txid);
        const auto pool = qmltest::Rpc(m_app, "getrawmempool"); bool found{false}, competitor{false};
        for (const auto& id : pool.getValues()) { found |= id.get_str() == txid.toStdString(); competitor |= id.get_str() == competing.toStdString(); }
        QCOMPARE(found, scenario == 0); if (scenario == 2) QVERIFY(competitor);
    }

    void asynchronousSendKeepsTheCommittedIdentityAndPublicInvoice()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "async-send"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto name = wallet->name(), address = qmltest::NewAddress(m_app, name);
        QmlRecentRequestEntry entry; entry.id = 7; entry.date = QDateTime::fromSecsSinceEpoch(1'700'000'000);
        entry.recipient.address = address.toStdString(); entry.recipient.amount = 1234;
        entry.recipient.label = "public invoice"; entry.recipient.message = "public message"; entry.recipient.noteSelf = "private invoice note";
        const auto original = walletrequests::Encode(entry);
        QVERIFY(qmltest::Call(m_app, [name, address, original](interfaces::Node* node) {
            for (auto& candidate : node->walletLoader().getWallets()) if (candidate->getWalletName() == name.toStdString())
                return candidate->setAddressReceiveRequest(DecodeDestination(address.toStdString()), "7", original);
            return false;
        }));
        auto* recipient = wallet->sendRecipientList()->currentRecipient(); QVERIFY(recipient);
        recipient->address()->setAddress(address, 2); recipient->amount()->setSatoshi(100'000); recipient->setLabel("outgoing note");
        QTRY_VERIFY(!wallet->feeEstimatePending()); QVERIFY(AwaitPreparation(*wallet));
        wallet->requestSendTransaction();
        QTRY_COMPARE(wallet->sendState(), WalletQmlModel::SendState::SendCommitted);
        QTRY_VERIFY(!wallet->recipientLabelsPending()); QVERIFY2(wallet->recipientLabelsError().isEmpty(), qPrintable(wallet->recipientLabelsError()));
        const auto txid = wallet->recipientLabelsTransactionId(); QVERIFY(!txid.isEmpty());
        const auto mempool = qmltest::Rpc(m_app, "getrawmempool"); bool broadcast{false};
        for (const auto& id : mempool.getValues()) if (id.get_str() == txid.toStdString()) broadcast = true;
        QVERIFY(broadcast);
        QVERIFY(qmltest::Call(m_app, [name, address, original](interfaces::Node* node) {
            for (auto& candidate : node->walletLoader().getWallets()) {
                if (candidate->getWalletName() != name.toStdString()) continue;
                std::string label; wallet::AddressPurpose purpose;
                return candidate->getAddress(DecodeDestination(address.toStdString()), &label, &purpose) &&
                    label == "outgoing note" && purpose == wallet::AddressPurpose::RECEIVE &&
                    candidate->getAddressReceiveRequests() == std::vector<std::string>{original};
            }
            return false;
        }));
    }

    void orderedRequestOwnerPersistsPublicInterfaceEffectsAndNativeReceipts()
    {
        using namespace walletrequests;
        auto* wallet = qmltest::CreateWallet(m_app, "ordered-storage"); QVERIFY(wallet);
        const QString name = wallet->name();
        const auto address = qmltest::NewAddress(m_app, name);
        auto native = qmltest::Call(m_app, [name](interfaces::Node* node) -> std::shared_ptr<interfaces::Wallet> {
            for (auto& candidate : node->walletLoader().getWallets()) {
                if (candidate->getWalletName() == name.toStdString()) return std::move(candidate);
            }
            return {};
        });
        QVERIFY(native);
        BackendExecutor commands{execution::Limits::Bounded()}, retirement;
        auto opening = std::make_shared<walletopening::Session>(commands.scheduler(), retirement.scheduler(),
            std::make_shared<std::atomic_bool>(false), [] {});
        auto client = opening->start(native).borrower; QVERIFY(client);
        auto reader = std::make_shared<walletreceipts::Reader>(opening, [] {});
        auto store = std::make_shared<Store>(opening, [] {},
            [weak = std::weak_ptr<walletreceipts::Reader>{reader}](QSet<QString> addresses) {
                if (const auto owner = weak.lock()) owner->reload(std::move(addresses));
            }, QDateTime::currentDateTimeUtc, [weak = std::weak_ptr<walletreceipts::Reader>{reader}] {
                const auto owner = weak.lock(); return owner && owner->settled();
            });
        const auto cleanup = qScopeGuard([&] {
            store->close(); reader->close(); opening->retire(); client.reset(); native.reset();
            commands.shutdown(); retirement.shutdown();
            if (!QTest::qWaitFor([&] { return commands.isDrained() && retirement.isDrained(); }, 30'000))
                QTest::qFail("Request owner did not physically drain", __FILE__, __LINE__);
        });
        QVERIFY(reader->onSnapshot([weak = std::weak_ptr<Store>{store}](auto snapshot) {
            if (const auto owner = weak.lock()) owner->observed(snapshot);
        }));
        opening->onTransactions([weak = std::weak_ptr<walletreceipts::Reader>{reader}](uint64_t revision, auto changes, bool rescan) {
            if (const auto owner = weak.lock()) owner->notice(revision, std::move(changes), rescan);
        });
        Command reload; reload.kind = Kind::Reload;
        const auto initial = store->submit(reload); QVERIFY(initial);
        QTRY_VERIFY(initial.operation->result().has_value()); QTRY_VERIFY(reader->settled());
        Command create; create.kind = Kind::Create; create.require_unused = true;
        create.draft.address = address; create.draft.amount = 10'000;
        create.draft.label = "public label"; create.draft.message = "public message"; create.draft.note = "private note";
        const auto created = store->submit(create); QVERIFY(created);
        QTRY_VERIFY(created.operation->result().has_value());
        QVERIFY(created.operation->result()->request_saved);
        const auto saved = *created.operation->result()->record;
        QCOMPARE(saved.entry.recipient.amount, CAmount{10'000});
        QTRY_VERIFY(reader->settled());
        // A separately acquired public Wallet handle is the persistence oracle;
        // it does not inspect the actor's snapshot or Core wallet internals.
        const auto read = [&] {
            return qmltest::Call(m_app, [name](interfaces::Node* node) {
                for (auto& candidate : node->walletLoader().getWallets()) {
                    if (candidate->getWalletName() == name.toStdString()) return Decode(candidate->getAddressReceiveRequests()).entries;
                }
                return std::vector<QmlRecentRequestEntry>{};
            });
        };
        QCOMPARE(read().size(), size_t{1}); QCOMPARE(read().front().recipient.noteSelf, std::string{"private note"});
        Command label; label.kind = Kind::Label; label.address = address; label.label = "renamed private note";
        const auto renamed = store->submit(label); QVERIFY(renamed);
        QTRY_VERIFY(renamed.operation->result().has_value()); QCOMPARE(renamed.operation->result()->error, Error::None);
        QVERIFY(renamed.operation->result()->label_saved);
        QCOMPARE(read().front().recipient.noteSelf, std::string{"renamed private note"});
        QCOMPARE(read().front().recipient.label, std::string{"public label"});
        auto* payer = qmltest::CreateWallet(m_app, "storage-payer"); QVERIFY(payer); QVERIFY(qmltest::FundWallet(m_app, *payer));
        qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + address.toStdString() + "\",\"amount\":0.00001,\"fee_rate\":1}"), payer->name());
        QTRY_VERIFY_WITH_TIMEOUT(reader->settled() && read().front().payment_received, 30'000);
        const auto refreshed = store->submit(reload); QVERIFY(refreshed);
        QTRY_VERIFY(refreshed.operation->result().has_value()); QTRY_VERIFY(reader->settled());
        const auto publication = store->take(); QVERIFY(publication && publication->snapshot);
        const auto* paid = publication->snapshot->find(saved.entry.id); QVERIFY(paid && paid->entry.payment_received);
        auto edit = create; edit.kind = Kind::Edit; edit.id = paid->entry.id; edit.revision = paid->revision; edit.draft.amount = 20'000;
        const auto refused = store->submit(edit); QVERIFY(refused); QTRY_VERIFY(refused.operation->result().has_value());
        QCOMPARE(refused.operation->result()->error, Error::Paid);
        Command erase; erase.kind = Kind::Erase; erase.id = paid->entry.id; erase.revision = paid->revision;
        const auto erased = store->submit(erase); QVERIFY(erased); QTRY_VERIFY(erased.operation->result().has_value());
        QVERIFY(erased.operation->result()->erased); QVERIFY(read().empty());
    }
    void incrementalLabelPropagationPreservesUnrelatedCoreRecords()
    {
        using namespace walletrequests;
        auto* wallet = qmltest::CreateWallet(m_app, "label-batches"); QVERIFY(wallet);
        const auto name = wallet->name();
        const auto address = qmltest::NewAddress(m_app, name), other = qmltest::NewAddress(m_app, name);
        QVERIFY(qmltest::Call(m_app, [name, address, other](interfaces::Node* node) {
            for (auto& candidate : node->walletLoader().getWallets()) {
                if (candidate->getWalletName() != name.toStdString()) continue;
                for (int64_t id{1}; id <= 85; ++id) {
                    QmlRecentRequestEntry entry; entry.id = id; entry.date = QDateTime::fromSecsSinceEpoch(1'700'000'000);
                    entry.recipient.address = (id <= 65 ? address : other).toStdString();
                    entry.recipient.amount = id * 100; entry.recipient.label = "public"; entry.recipient.message = "message";
                    entry.recipient.noteSelf = "before"; entry.payment_received = id == 1;
                    if (!candidate->setAddressReceiveRequest(DecodeDestination(entry.recipient.address), util::ToString(id), Encode(entry))) return false;
                }
                return true;
            }
            return false;
        }));
        auto native = qmltest::Call(m_app, [name](interfaces::Node* node) -> std::shared_ptr<interfaces::Wallet> {
            for (auto& candidate : node->walletLoader().getWallets()) if (candidate->getWalletName() == name.toStdString()) return std::move(candidate);
            return {};
        }); QVERIFY(native);
        BackendExecutor commands{execution::Limits::Bounded()}, retirement;
        auto opening = std::make_shared<walletopening::Session>(commands.scheduler(), retirement.scheduler(), std::make_shared<std::atomic_bool>(false), [] {});
        auto client = opening->start(native).borrower; QVERIFY(client);
        auto reader = std::make_shared<walletreceipts::Reader>(opening, [] {});
        std::atomic<QThread*> encoded_on{nullptr}, prepared_on{nullptr};
        auto store = std::make_shared<Store>(opening, [] {}, [reader](QSet<QString> addresses) { reader->reload(std::move(addresses)); },
            QDateTime::currentDateTimeUtc, [reader] { return reader->settled(); }, Store::Release{}, ReservationStorage{},
            [&encoded_on](const QmlRecentRequestEntry& entry) { encoded_on = QThread::currentThread(); return Encode(entry); }, DecodeOne,
            [&prepared_on](Record row) { prepared_on = QThread::currentThread(); return FreezeRecord(std::move(row)); });
        const auto cleanup = qScopeGuard([&] {
            store->close(); reader->close(); opening->retire(); client.reset(); native.reset();
            commands.shutdown(); retirement.shutdown();
            if (!QTest::qWaitFor([&] { return commands.isDrained() && retirement.isDrained(); }, 30'000))
                QTest::qFail("Label owner did not physically drain", __FILE__, __LINE__);
        });
        QVERIFY(reader->onSnapshot([weak = std::weak_ptr<Store>{store}](auto snapshot) {
            if (const auto owner = weak.lock()) owner->observed(snapshot);
        }));
        Command reload; reload.kind = Kind::Reload; const auto loaded = store->submit(reload); QVERIFY(loaded);
        QTRY_VERIFY(loaded.operation->result()); QCOMPARE(loaded.operation->result()->error, Error::None);
        QTRY_VERIFY(store->presentationCurrent());
        Command label; label.kind = Kind::Label; label.address = address; label.label = "after";
        const auto changed = store->submit(label); QVERIFY(changed); QTRY_VERIFY(changed.operation->result());
        const auto result = *changed.operation->result(); QCOMPARE(result.error, Error::None); QVERIFY(result.label_saved);
        QCOMPARE(result.records_total, size_t{65}); QCOMPARE(result.records_saved, size_t{65});
        QVERIFY(encoded_on.load()); QVERIFY(prepared_on.load());
        QVERIFY(encoded_on.load() != QThread::currentThread()); QVERIFY(prepared_on.load() != QThread::currentThread());
        const auto persisted = qmltest::Call(m_app, [name](interfaces::Node* node) {
            for (auto& candidate : node->walletLoader().getWallets()) if (candidate->getWalletName() == name.toStdString()) return Decode(candidate->getAddressReceiveRequests()).entries;
            return std::vector<QmlRecentRequestEntry>{};
        });
        QCOMPARE(persisted.size(), size_t{85}); std::set<int64_t> seen;
        for (const auto& entry : persisted) {
            QVERIFY(entry.id >= 1 && entry.id <= 85); QVERIFY(seen.insert(entry.id).second);
            QCOMPARE(entry.recipient.address, (entry.id <= 65 ? address : other).toStdString());
            QCOMPARE(entry.recipient.noteSelf, entry.id <= 65 ? std::string{"after"} : std::string{"before"});
            QCOMPARE(entry.recipient.amount, entry.id * 100); QCOMPARE(entry.recipient.label, std::string{"public"});
            QCOMPARE(entry.recipient.message, std::string{"message"}); QCOMPARE(entry.payment_received, entry.id == 1);
        }
    }
    void addressPreferencePersistsThroughPublicSettingsPort()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "preference");
        QVERIFY(wallet); QTRY_VERIFY(wallet->capabilitiesKnown());
        QVERIFY(wallet->setDefaultReceiveAddressType("legacy"));
        QTRY_VERIFY(!wallet->property("receivePreferencePending").toBool());
        QCOMPARE(wallet->property("receivePreferenceError").toString(), QString{});
        QCOMPARE(wallet->defaultReceiveAddressType(), QString{"legacy"});
        fs::path path;
        QVERIFY(gArgs.GetSettingsPath(&path));
        QFile file{QString::fromStdString(fs::PathToString(path))};
        QVERIFY(file.open(QIODevice::ReadOnly));
        UniValue persisted;
        QVERIFY(persisted.read(file.readAll().toStdString()));
        QCOMPARE(persisted["qml_receive_address_types"]["preference"].get_str(), std::string{"legacy"});
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QPointer<WalletQmlModel> original{wallet};
        controller->closeWallet("preference"); QTRY_VERIFY(original.isNull());
        controller->setSelectedWallet("preference");
        auto* reopened = qmltest::WaitForWallet(m_app, "preference");
        QVERIFY(reopened); QTRY_VERIFY(reopened->capabilitiesKnown());
        QCOMPARE(reopened->defaultReceiveAddressType(), QString{"legacy"});
    }
    void addressPreferenceWriteReportsDisabledSettings()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "preference-disabled");
        QVERIFY(wallet); QTRY_VERIFY(wallet->capabilitiesKnown());
        QVERIFY(!gArgs.GetSettingsPath());
        QVERIFY(wallet->setDefaultReceiveAddressType("legacy"));
        QTRY_VERIFY(!wallet->property("receivePreferencePending").toBool());
        QVERIFY(!wallet->property("receivePreferenceError").toString().isEmpty());
        // Core keeps the effective value in memory even when persistence fails.
        // Present that value while retaining the failed persistence outcome.
        QCOMPARE(wallet->defaultReceiveAddressType(), QString{"legacy"});
        QVERIFY(wallet->available());
    }
    void localizedTypedRequestPersistsExactAmount_data()
    {
        QTest::addColumn<QString>("text"); QTest::addColumn<int>("unit"); QTest::addColumn<int>("producer");
        QTest::newRow("german-comma") << "0,02" << 0 << 0;
        QTest::newRow("dot-control") << "0.02" << 0 << 0;
        QTest::newRow("native-clipboard-comma") << "0,02" << 0 << 1;
        QTest::newRow("ime-commit-comma") << "0,02" << 0 << 2;
        QTest::newRow("milli-unit") << "20" << 1 << 0;
        QTest::newRow("micro-unit") << "20000" << 2 << 0;
        QTest::newRow("sat-unit") << "2000000" << 3 << 0;
    }

    void localizedTypedRequestPersistsExactAmount()
    {
        QFETCH(QString, text); QFETCH(int, unit); QFETCH(int, producer);
        const QLocale previous;
        const auto restore = qScopeGuard([previous] { QLocale::setDefault(previous); });
        QLocale::setDefault(QLocale("de_DE"));
        auto* wallet = qmltest::CreateWallet(m_app); QVERIFY(wallet);
        auto* options = qmltest::Model<OptionsQmlModel>(m_app, "optionsModel"); QVERIFY(options); options->setDisplayUnit(unit);
        QVERIFY(qmltest::Click(m_app, "receiveTabButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "requestPaymentPage"));
        QVERIFY(qmltest::SetText(m_app, "requestPaymentAmountInput", ""));
        auto* input = qmltest::Find(m_app, "requestPaymentAmountInput"); QVERIFY(input);
        QVERIFY(QMetaObject::invokeMethod(input, "forceActiveFocus"));
        if (producer == 1) {
            QGuiApplication::clipboard()->setText(text);
            QTest::keyClick(m_app.window(), Qt::Key_V, Qt::ControlModifier, 1);
        } else if (producer == 2) {
            const auto before_preedit = wallet->currentPaymentRequest()->amount()->satoshi();
            QInputMethodEvent preedit(QStringLiteral("0,"), {});
            QCoreApplication::sendEvent(input, &preedit);
            QCOMPARE(input->property("preeditText").toString(), QString("0,"));
            QCOMPARE(wallet->currentPaymentRequest()->amount()->satoshi(), before_preedit);
            QInputMethodEvent committed;
            committed.setCommitString(text);
            QCoreApplication::sendEvent(input, &committed);
            QVERIFY(committed.isAccepted());
        } else {
            for (QChar key : text) QTest::keyClick(m_app.window(), static_cast<Qt::Key>(key.unicode()), Qt::NoModifier, 1);
        }
        const QString entered = input->property("text").toString();
        QVERIFY(qmltest::Click(m_app, "requestPaymentGenerateButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "paymentRequestModal"));
        const auto blobs = qmltest::Call(m_app, [backend = wallet->wallet()](interfaces::Node*) { return backend->getAddressReceiveRequests(); }); QCOMPARE(blobs.size(), size_t{1});
        std::vector<uint8_t> bytes(blobs[0].begin(), blobs[0].end()); DataStream record{bytes};
        int version, recipient_version; int64_t id; uint32_t timestamp; std::string address, label; CAmount persisted;
        record >> version >> id >> timestamp >> recipient_version >> address >> label >> persisted;
        QCOMPARE(version, 1); QCOMPARE(recipient_version, 1); QVERIFY(id > 0); QVERIFY(timestamp > 0); QVERIFY(!address.empty());
        const QString uri = qmltest::Find(m_app, "requestPaymentQRImage")->property("code").toString();
        qInfo() << "Typed Receive amount oracle" << text << "accepted text" << entered << "Core stored sats" << persisted << "QR payload" << uri;
        QVERIFY(qmltest::Click(m_app, "paymentRequestModalClose"));
        wallet->reloadReceiveRequests(); QCOMPARE(wallet->receiveRequests()->count(), 1);
        QCOMPARE(persisted, CAmount{2'000'000});
        QVERIFY(uri.contains("amount=0.02000000"));
    }

    void privateNotesSyncWithoutChangingPublicPaymentMetadata()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        auto* request = wallet->currentPaymentRequest();
        request->amount()->setSatoshi(10'000);
        request->setLabel("Alice");
        request->setMessage("pizza");
        request->setNoteSelf("Private lunch note");
        QVERIFY(AwaitReceiveRequest(wallet->commitPaymentRequest()));
        const auto id = request->id();
        const auto address = request->address();
        const auto uri = request->qrPayload();
        QVERIFY(!id.isEmpty());
        QVERIFY(!address.isEmpty());
        QVERIFY(!uri.contains("Private"));
        QCOMPARE(wallet->receiveRequests()->count(), 1);
        UniValue address_params{UniValue::VARR};
        address_params.push_back(address.toStdString());
        const auto address_info = qmltest::Rpc(m_app, "getaddressinfo", address_params, wallet->name());
        QCOMPARE(QString::fromStdString(address_info["labels"][0].get_str()), QStringLiteral("Private lunch note"));
        auto* addresses = wallet->addressListModel();
        const auto find_address = [addresses, address] {
            for (int row{0}; row < addresses->rowCount(); ++row) {
                if (addresses->addressAt(row) == address) return addresses->index(row);
            }
            return QModelIndex{};
        };
        QTRY_VERIFY(!addresses->refreshPending() && find_address().isValid());
        const QPersistentModelIndex original_row{find_address()};
        QCOMPARE(original_row.data(AddressListModel::LabelRole).toString(), QStringLiteral("Private lunch note"));
        QSignalSpy changed{wallet->receiveRequests(), &QAbstractItemModel::modelReset};
        QVERIFY(AwaitReceiveRequest(wallet->addressListModel()->setAddressLabel(address, "Renamed on Addresses page")));
        QTRY_VERIFY(!changed.isEmpty());
        QTRY_VERIFY(!addresses->refreshPending() && original_row.isValid() &&
            original_row.data(AddressListModel::LabelRole).toString() == QStringLiteral("Renamed on Addresses page"));
        const auto renamed_address_info = qmltest::Rpc(m_app, "getaddressinfo", address_params, wallet->name());
        QCOMPARE(QString::fromStdString(renamed_address_info["labels"][0].get_str()), QStringLiteral("Renamed on Addresses page"));
        QVERIFY(wallet->loadPaymentRequestDetail(id));
        QCOMPARE(wallet->detailPaymentRequest()->noteSelf(), QStringLiteral("Renamed on Addresses page"));
        QCOMPARE(wallet->detailPaymentRequest()->label(), QStringLiteral("Alice"));
        QCOMPARE(wallet->detailPaymentRequest()->message(), QStringLiteral("pizza"));
        QCOMPARE(wallet->detailPaymentRequest()->qrPayload(), uri);
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));
        QVERIFY(qmltest::SetText(m_app, "activitySearchField", "Renamed on Addresses page"));
        auto* filter = qobject_cast<ActivityFilterProxyModel*>(qmltest::Find(m_app, "activityFilterProxyModel"));
        QVERIFY(filter);
        QTRY_COMPARE(filter->rowCount(), 1);
        QCOMPARE(filter->data(filter->index(0, 0), TransactionActivityModel::LabelRole).toString(), QStringLiteral("Renamed on Addresses page"));
        wallet->reloadReceiveRequests();
        QTRY_VERIFY(!wallet->property("receiveRequestsBusy").toBool());
        QVERIFY(wallet->loadPaymentRequestDetail(id));
        QCOMPARE(wallet->detailPaymentRequest()->noteSelf(), QStringLiteral("Renamed on Addresses page"));
        QCOMPARE(wallet->detailPaymentRequest()->qrPayload(), uri);
        QVERIFY(AwaitReceiveRequest(wallet->removeReceiveRequest(id)));
        QCOMPARE(wallet->receiveRequests()->count(), 0);
        QVERIFY(!wallet->loadPaymentRequestDetail(id));
    }

    void receivingAddressCanBeReusedOrReplaced()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        QVERIFY(AwaitReceiveRequest(wallet->ensureReceivingAddress()));
        const auto address = wallet->receivingAddress()->address();
        QVERIFY(!address.isEmpty());
        QVERIFY(AwaitReceiveRequest(wallet->ensureReceivingAddress()));
        QCOMPARE(wallet->receivingAddress()->address(), address);
        QVERIFY(AwaitReceiveRequest(wallet->ensureReceivingAddress(true)));
        QVERIFY(wallet->receivingAddress()->address() != address);
        const auto types = wallet->availableReceiveAddressTypes();
        QVERIFY(!types.isEmpty());
        QVERIFY(AwaitReceiveRequest(wallet->ensureReceivingAddress(true, "legacy")));
        const auto legacy = wallet->receivingAddress()->address();
        QVERIFY(legacy.startsWith('m') || legacy.startsWith('n'));
    }

    void requestImageExportPreservesTheSavedCoreRequestAndOriginalPath()
    {
        auto* wallet = qmltest::CreateWallet(m_app); QVERIFY(wallet);
        auto* draft = wallet->currentPaymentRequest();
        draft->setLabel("export identity"); draft->setMessage("public QR terms"); draft->amount()->setSatoshi(123);
        QVERIFY(AwaitReceiveRequest(wallet->commitPaymentRequest()));
        QTRY_VERIFY(!wallet->receiveRequestReconciliationPending());
        const auto request_id = draft->id(); const auto payload = draft->qrPayload(); const auto name = wallet->name();
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path = directory.path() + QStringLiteral("/original request.png");
        QImage image{64, 64, QImage::Format_ARGB32}; image.fill(Qt::blue);
        std::unique_ptr<ImageExportOperation> operation{wallet->savePaymentRequestImage(draft, image, QUrl::fromLocalFile(path))};
        QVERIFY(operation); QTRY_VERIFY(!operation->pending()); QVERIFY(operation->saved());
        QCOMPARE(operation->path(), QUrl::fromLocalFile(path)); QCOMPARE(QImage{path}.pixel(0, 0), image.pixel(0, 0));
        const auto entries = qmltest::Call(m_app, [name](interfaces::Node* node) {
            for (auto& handle : node->walletLoader().getWallets()) {
                if (handle->getWalletName() == name.toStdString()) return walletrequests::Decode(handle->getAddressReceiveRequests()).entries;
            }
            return std::vector<QmlRecentRequestEntry>{};
        });
        QCOMPARE(entries.size(), size_t{1}); QCOMPARE(QString::number(entries.front().id), request_id);
        QCOMPARE(entries.front().recipient.amount, CAmount{123});
        QCOMPARE(QString::fromStdString(entries.front().recipient.message), QStringLiteral("public QR terms"));
        QCOMPARE(draft->qrPayload(), payload);
    }

    void templateResetGeneratesAFreshAddressThroughThePublicWallet()
    {
        auto* wallet = qmltest::CreateWallet(m_app); QVERIFY(wallet);
        auto* draft = wallet->currentPaymentRequest();
        draft->setLabel("repeat invoice"); draft->setMessage("public terms"); draft->setNoteSelf("private context");
        draft->amount()->setSatoshi(12'345); draft->setAddressType("legacy");
        QVERIFY(AwaitReceiveRequest(wallet->commitPaymentRequest()));
        const auto id = draft->id(); const auto saved_address = draft->address();
        QVERIFY(AwaitReceiveRequest(wallet->ensureReceivingAddress()));
        const auto unused_address = wallet->receivingAddress()->address();
        wallet->usePaymentRequestAsTemplate(id);
        QVERIFY(draft->id().isEmpty()); QVERIFY(draft->address().isEmpty());
        QCOMPARE(draft->label(), QStringLiteral("repeat invoice"));
        QCOMPARE(draft->message(), QStringLiteral("public terms"));
        QCOMPARE(draft->noteSelf(), QStringLiteral("private context"));
        QCOMPARE(draft->amount()->satoshi(), CAmount{12'345});
        QCOMPARE(draft->addressType(), QStringLiteral("legacy"));
        QVERIFY(AwaitReceiveRequest(wallet->ensureReceivingAddress(false, draft->addressType())));
        const auto fresh = wallet->receivingAddress()->address();
        QVERIFY(fresh != saved_address && fresh != unused_address);
        const auto info = qmltest::Rpc(m_app, "getaddressinfo", qmltest::Json("[\"" + fresh.toStdString() + "\"]"), wallet->name());
        QVERIFY(info.find_value("ismine").get_bool()); QVERIFY(!info.find_value("iswitness").get_bool());
        QCOMPARE(wallet->receiveRequests()->count(), 1); QVERIFY(draft->id().isEmpty());
    }

    void signingUnlocksOnlyForTheOperation()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name(), "legacy");
        QVERIFY(wallet->encryptWallet("signing password"));
        QTRY_COMPARE(wallet->securityState(), WalletQmlModel::SecurityState::SecuritySucceeded);
        auto* signer = wallet->signVerifyMessageModel();
        QVERIFY(!AwaitMessageSigning(*signer, address, "integration message"));
        QVERIFY(signer->signingNeedsUnlock());
        QVERIFY(!AwaitMessageSigning(*signer, address, "integration message", "incorrect"));
        QVERIFY(AwaitMessageSigning(*signer, address, "integration message", "signing password"));
        const auto signature = signer->signature();
        QVERIFY(!signature.isEmpty());
        QVERIFY(AwaitMessageVerification(*signer, address, "integration message", signature));
        QVERIFY(!AwaitMessageVerification(*signer, address, "changed message", signature));
        QVERIFY(wallet->isLocked());
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);
    }

    void receivingUnlockCanBeCancelledRetriedAndRelocks()
    {
        auto* wallet = qmltest::CreateLockedHardenedWallet(m_app, "receive_unlock", "receiving password");
        QVERIFY(wallet);
        const auto info = qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name());
        const auto cached_keys = info.find_value("keypoolsize").getInt<int>();
        for (int i{0}; i < cached_keys; ++i) qmltest::NewAddress(m_app, wallet->name());
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("keypoolsize").getInt<int>(), 0);
        QVERIFY(wallet->receivingAddress()->address().isEmpty());

        // Cancelling address creation must leave both Core and request history unchanged.
        QVERIFY(qmltest::Click(m_app, "receiveTabButton"));
        auto* popup = qmltest::WaitForVisible(m_app, "receivePassphrasePopup");
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(qmltest::SetText(m_app, "receivePassphraseField", "cancelled password"));
        QVERIFY(qmltest::Click(m_app, "receivePassphraseCancelButton"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(qmltest::Find(m_app, "receivePassphraseField")->property("text").toString(), QString{});
        QVERIFY(wallet->receivingAddress()->address().isEmpty());
        QCOMPARE(wallet->receiveRequests()->count(), 0);
        QVERIFY(wallet->isLocked());
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);

        // Reopening Receive retries the exhausted keypool, with a visible password error.
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));
        QVERIFY(qmltest::Click(m_app, "receiveTabButton"));
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(qmltest::SetText(m_app, "receivePassphraseField", "wrong password"));
        QVERIFY(qmltest::Click(m_app, "receivePassphraseConfirmButton"));
        auto* error = qmltest::WaitForVisible(m_app, "receivePassphraseError");
        QVERIFY(error);
        QVERIFY(error->property("text").toString().contains("incorrect"));
        QVERIFY(popup->property("opened").toBool());
        QCOMPARE(qmltest::Find(m_app, "receivePassphraseField")->property("text").toString(), QString{});
        QVERIFY(wallet->receivingAddress()->address().isEmpty());
        QVERIFY(wallet->isLocked());
        QCOMPARE(wallet->receiveRequests()->count(), 0);
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);

        // A correct password generates the requested address and immediately relocks.
        QVERIFY(qmltest::SetText(m_app, "receivePassphraseField", "receiving password"));
        QVERIFY(qmltest::Click(m_app, "receivePassphraseConfirmButton"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        const auto address = wallet->receivingAddress()->address();
        QVERIFY(!address.isEmpty());
        QTRY_VERIFY(qmltest::Find(m_app, "requestPaymentGenerateButton")->property("enabled").toBool());
        const auto address_info = qmltest::Rpc(m_app, "getaddressinfo", qmltest::Json("[\"" + address.toStdString() + "\"]"), wallet->name());
        QVERIFY(address_info.find_value("ismine").get_bool());
        QCOMPARE(address_info.find_value("witness_version").getInt<int>(), 0);
        QVERIFY(wallet->isLocked());
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);
        QCOMPARE(wallet->receiveRequests()->count(), 0);
        QVERIFY(wallet->currentPaymentRequest()->id().isEmpty());

        // The next-address request must survive another unlock, without reusing this address.
        QVERIFY(QMetaObject::invokeMethod(qmltest::Find(m_app, "requestPaymentPage"), "ensureReceivingAddress",
                                         Q_ARG(QVariant, true), Q_ARG(QVariant, QStringLiteral("bech32"))));
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(qmltest::SetText(m_app, "receivePassphraseField", "receiving password"));
        QVERIFY(qmltest::Click(m_app, "receivePassphraseConfirmButton"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(!wallet->receivingAddress()->address().isEmpty());
        QVERIFY(wallet->receivingAddress()->address() != address);
        QVERIFY(wallet->isLocked());
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);
        QCOMPARE(wallet->receiveRequests()->count(), 0);
    }

    void addressDetailsCreateAndUpdateTheSamePaymentRequest()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        const auto address = QString::fromStdString(qmltest::Rpc(m_app, "getnewaddress", qmltest::Json("[\"invoice 1024\"]"), wallet->name()).get_str());
        QVERIFY(qmltest::SelectSettings(m_app, "wallet"));
        QVERIFY(qmltest::Click(m_app, "walletAddressesRow"));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressListPage"));
        QTRY_VERIFY(addressRow(address));
        QVERIFY(qmltest::Click(qmltest::Find(addressRow(address), "addressRowDetailsButton")));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressDetailsPopup"));
        auto* page = qmltest::Find(m_app, "addressListPage"); QVERIFY(page);
        QSignalSpy receive_requested{page, SIGNAL(receiveRequested())};
        QVERIFY(qmltest::Click(m_app, "addressDetailsCreatePaymentRequestButton"));
        QTRY_VERIFY(page->property("receiveOperation").value<QObject*>());
        auto* operation = page->property("receiveOperation").value<QObject*>();
        QTRY_VERIFY(!operation->property("pending").toBool());
        QTRY_COMPARE(receive_requested.count(), 1);
        QVERIFY(qmltest::WaitForVisible(m_app, "requestPaymentPage"));
        QCOMPARE(wallet->receivingAddress()->address(), address);
        QCOMPARE(qmltest::Find(m_app, "requestPaymentNoteRow")->property("value").toString(), QStringLiteral("invoice 1024"));
        QVERIFY(qmltest::SetText(m_app, "requestPaymentAmountInput", "0.25"));
        QVERIFY(QMetaObject::invokeMethod(qmltest::Find(m_app, "requestPaymentAmountInput"), "editingFinished"));
        QVERIFY(qmltest::Click(m_app, "requestPaymentGenerateButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "paymentRequestModal"));
        QCOMPARE(wallet->receiveRequests()->count(), 1);
        QVERIFY(qmltest::Click(m_app, "paymentRequestModalClose"));

        // The Addresses stack is retained, and its action now views the saved request.
        QVERIFY(qmltest::Click(m_app, "desktopWalletSettingsTabButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressListPage"));
        QTRY_VERIFY(addressRow(address));
        QVERIFY(qmltest::Click(qmltest::Find(addressRow(address), "addressRowDetailsButton")));
        auto* action = qmltest::WaitForVisible(m_app, "addressDetailsCreatePaymentRequestButton");
        QVERIFY(action);
        QCOMPARE(action->property("text").toString(), QStringLiteral("View payment request"));
        QVERIFY(qmltest::Click(action));
        QVERIFY(qmltest::WaitForVisible(m_app, "paymentRequestModal"));
        QVERIFY(qmltest::SetText(m_app, "requestPaymentAmountInput", "0.5"));
        QVERIFY(QMetaObject::invokeMethod(qmltest::Find(m_app, "requestPaymentAmountInput"), "editingFinished"));
        QCOMPARE(wallet->receiveRequests()->count(), 1);
        QVERIFY(qmltest::Click(m_app, "requestPaymentUpdateButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "requestPaymentCopyButton"));
        QCOMPARE(wallet->receiveRequests()->count(), 1);
        QCOMPARE(qmltest::Find(m_app, "requestPaymentAddressText")->property("address").toString(), address);
        QVERIFY(qmltest::Click(m_app, "paymentRequestModalClose"));
        QVERIFY(qmltest::Click(m_app, "desktopWalletSettingsTabButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressListPage"));
    }

    void replacementAndConfirmedPaymentsDoNotDoubleCountReceipts()
    {
        auto* payer = qmltest::CreateWallet(m_app, "payer");
        QVERIFY(payer);
        QVERIFY(qmltest::FundWallet(m_app, *payer));
        auto* receiver = qmltest::CreateWallet(m_app, "receiver");
        QVERIFY(receiver);
        auto* request = receiver->currentPaymentRequest();
        request->amount()->setSatoshi(10'000);
        request->setMessage("pizza");
        request->setNoteSelf("Private lunch note");
        QVERIFY(AwaitReceiveRequest(receiver->commitPaymentRequest()));
        const auto id = request->id();
        const auto address = request->address();
        QVERIFY(receiver->loadPaymentRequestDetail(id));
        auto* detail = receiver->detailPaymentRequest();

        const auto original = qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + address.toStdString() + "\",\"amount\":0.00001,\"fee_rate\":1}"), payer->name()).get_str();
        QTRY_VERIFY_WITH_TIMEOUT(detail->paymentReceived(), 30'000);
        QTRY_COMPARE(detail->receivedAmountSatoshi(), 1'000);
        QVERIFY(detail->qrPayload().isEmpty());
        const auto replacement = qmltest::Rpc(m_app, "bumpfee", qmltest::Json("[\"" + original + "\",{\"fee_rate\":20}]"), payer->name());
        const auto replacement_id = QString::fromStdString(replacement.find_value("txid").get_str());
        QVERIFY(replacement_id != QString::fromStdString(original));
        QTRY_VERIFY_WITH_TIMEOUT(hasTransaction(*receiver, replacement_id), 30'000);
        QCOMPARE(detail->receivedAmountSatoshi(), 1'000);

        const auto mining_address = qmltest::NewAddress(m_app, payer->name());
        const auto mine = [&] {
            qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining_address.toStdString() + "\"]"));
            // Mining returns before wallet notifications and reconciliation finish.
            qmltest::Rpc(m_app, "syncwithvalidationinterfacequeue");
        };
        mine();
        QTRY_VERIFY_WITH_TIMEOUT(!receiver->receiveRequestReconciliationPending(), 30'000);
        QCOMPARE(detail->receivedAmountSatoshi(), 1'000);
        const auto second = QString::fromStdString(qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + address.toStdString() + "\",\"amount\":0.00002,\"fee_rate\":1}"), payer->name()).get_str());
        QTRY_VERIFY_WITH_TIMEOUT(hasTransaction(*receiver, second), 30'000);
        QTRY_COMPARE(detail->receivedAmountSatoshi(), 3'000);
        mine();
        QTRY_VERIFY_WITH_TIMEOUT(!receiver->receiveRequestReconciliationPending(), 30'000);
        QCOMPARE(detail->receivedAmountSatoshi(), 3'000);
        // Saves can also be refused while a queued transaction notification is
        // pending. Retry that busy result without changing the public metadata.
        QTRY_VERIFY_WITH_TIMEOUT(AwaitReceiveRequest(receiver->updatePaymentRequest(id, 10'000, {}, "pizza", "Partial payment received")), 30'000);
        QVERIFY(receiver->loadPaymentRequestDetail(id));
        QCOMPARE(detail->noteSelf(), QStringLiteral("Partial payment received"));
        QCOMPARE(detail->message(), QStringLiteral("pizza"));
        QCOMPARE(receiver->receiveRequests()->count(), 1);
        QVERIFY(detail->paymentReceived());
    }
};

std::unique_ptr<QObject> CreateReceiveTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<ReceiveIntegrationTests>(app);
}

QStringList ReceiveTestArguments(const QString& selector)
{
    if (selector == "committedSendReportsItsActualMempoolObservation:broadcast-disabled") return {"-walletbroadcast=0"};
    if (selector == "addressPreferenceWriteReportsDisabledSettings") return {"-nosettings"};
    return selector == "receivingUnlockCanBeCancelledRetriedAndRelocks" ? QStringList{"-keypool=1"} : QStringList{};
}

#include <test_receive.moc>
