// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/backendexecutor.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/admissionhookscheduler.h>
#include <test/mocks/threadrecordingscheduler.h>
#include <test/qt_test_registry.h>
#include <key_io.h>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QPersistentModelIndex>
#include <QScopeGuard>
#include <limits>
#include <stdexcept>

namespace {
QString Address(unsigned char seed = 4) { return QString::fromStdString(EncodeDestination(PKHash{uint160{std::vector<unsigned char>(20, seed)}})); }
struct Fixture {
    ManualRoutedExecution commands;
    std::deque<std::function<void()>> publications;
    std::unique_ptr<SendRecipientsListModel> recipients{std::make_unique<SendRecipientsListModel>()};
    bool fail_post{false};
    Fixture() {
        recipients->setOpening({}, {commands.scheduler, [this](std::function<void()> turn) {
            if (fail_post) throw std::runtime_error{"GUI transport unavailable"};
            publications.push_back(std::move(turn));
        }});
        finish();
    }
    ~Fixture() {
        if (recipients) recipients->closeAdmission();
        recipients.reset();
        // Independent bounded GUI retirement outlives the facade, including a
        // deliberately throwing primary transport and its real queued fallback.
        for (int turn{0}; turn < 1024 && (turn == 0 || commands.scheduler->pending()); ++turn) {
            finish(); QCoreApplication::processEvents(); commands.drain();
        }
    }
    bool publishOne() { if (publications.empty()) return false; auto turn = std::move(publications.front()); publications.pop_front(); turn(); return true; }
    void finish() { do { commands.drain(); while (publishOne()) {} } while (!commands.queuedIdle() || !commands.activeIdle()); }
    void fill(SendRecipient* recipient, unsigned char seed, CAmount amount) { recipient->setAddress(Address(seed)); recipient->amount()->setSatoshi(amount); }
};
}
class SendRecipientTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void validationAdmissionKeepsOriginalEditorIntent_data()
    {
        QTest::addColumn<int>("outcome");
        QTest::newRow("quiet") << 0; QTest::newRow("delete") << 1; QTest::newRow("newer-input") << 2;
    }
    void validationAdmissionKeepsOriginalEditorIntent()
    {
        QFETCH(int, outcome);
        ManualRoutedExecution env;
        auto scheduler = std::make_shared<AdmissionHookScheduler>(env.scheduler);
        std::deque<std::function<void()>> posts;
        auto recipient = std::make_unique<SendRecipient>(nullptr);
        const auto settle = [&] {
            for (int turn{0}; turn < 256 && (turn == 0 || !env.queuedIdle() || !posts.empty()); ++turn) {
                env.drain(); while (!posts.empty()) { auto work = std::move(posts.front()); posts.pop_front(); work(); }
            }
        };
        const auto cleanup = qScopeGuard([&] { scheduler->disarm(); if (recipient) recipient->closeAdmission(); recipient.reset(); settle(); });
        recipient->setOpening({}, {scheduler, [&](auto work) { posts.push_back(std::move(work)); }}); settle();
        recipient->setAddress(Address()); settle();
        QSignalSpy draft{recipient.get(), &SendRecipient::draftChanged};
        auto trigger = scheduler->armSubmit(execution::Lane::Domain, 1, [&] {
            if (outcome == 0) recipient->closeAdmission();
            else if (outcome == 1) recipient.reset();
            else recipient->amount()->setSatoshi(9000);
        });
        recipient->amount()->setSatoshi(7000);
        QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
        // No original admission continuation may emit after quiet/deletion or
        // duplicate the distinct newer input's draft notification.
        QCOMPARE(draft.count(), outcome == 2 ? 1 : 0);
        settle();
        if (outcome == 2) {
            QVERIFY(recipient->isValid()); QVERIFY(recipient->validationSnapshot());
            QCOMPARE(recipient->validationSnapshot()->amount, CAmount{9000});
        } else if (outcome == 0) { QVERIFY(recipient); QVERIFY(!recipient->validationPending()); }
        else QVERIFY(!recipient);
        scheduler->disarm(); if (recipient) recipient->closeAdmission(); recipient.reset(); settle();
        QCOMPARE(env.scheduler->pending(), size_t{0});
    }
    void cleanupAdmissionKeepsOriginalValidationOwner_data()
    {
        QTest::addColumn<int>("outcome");
        QTest::newRow("quiet") << 0;
        QTest::newRow("delete") << 1;
        QTest::newRow("same-port-successor") << 2;
    }
    void cleanupAdmissionKeepsOriginalValidationOwner()
    {
        QFETCH(int, outcome);
        ManualRoutedExecution env;
        auto scheduler = std::make_shared<AdmissionHookScheduler>(env.scheduler);
        std::deque<std::function<void()>> old_posts, new_posts;
        size_t old_posted{0}, new_posted{0};
        auto recipient = std::make_unique<SendRecipient>(nullptr);
        const auto identity = recipient->identity();
        const auto settle = [&] {
            for (int turn = 0; turn < 128 && (turn == 0 || !env.queuedIdle() || !old_posts.empty() || !new_posts.empty()); ++turn) {
                env.drain();
                for (auto* queue : {&old_posts, &new_posts}) while (!queue->empty()) {
                    auto work = std::move(queue->front()); queue->pop_front(); work();
                }
            }
        };
        const auto cleanup = qScopeGuard([&] { scheduler->disarm(); if (recipient) recipient->closeAdmission(); recipient.reset(); settle(); });
        const auto trigger = scheduler->armCleanup(1, [&] {
            if (outcome == 0) recipient->closeAdmission();
            else if (outcome == 1) recipient.reset();
            else recipient->setOpening({}, {scheduler, [&](auto turn) { ++new_posted; new_posts.push_back(std::move(turn)); }});
        });
        recipient->setOpening({}, {scheduler, [&](auto turn) { ++old_posted; old_posts.push_back(std::move(turn)); }});
        QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
        if (outcome == 2) {
            recipient->setAddress(Address()); recipient->amount()->setSatoshi(7'000); settle();
            QVERIFY(recipient->isValid()); QVERIFY(recipient->validationSnapshot());
            QCOMPARE(recipient->validationSnapshot()->token, identity);
            QCOMPARE(recipient->validationSnapshot()->amount, CAmount{7'000});
            QCOMPARE(old_posted, size_t{0}); QVERIFY(new_posted > 0);
        } else if (outcome == 0) { QVERIFY(recipient); QVERIFY(!recipient->validationPending()); }
        else QVERIFY(!recipient);
        scheduler->disarm(); if (recipient) recipient->closeAdmission(); recipient.reset(); settle();
        QCOMPARE(env.scheduler->pending(), size_t{0});
    }
    void quietCloseDuringQtMutation_data() {
        QTest::addColumn<int>("mutation"); QTest::addColumn<bool>("about");
        for (int mutation{0}; mutation < 3; ++mutation)
            for (bool about : {false, true}) QTest::newRow(qPrintable(QString::number(mutation) + (about ? "-about" : "-ended"))) << mutation << about;
    }
    void quietCloseDuringQtMutation() {
        QFETCH(int, mutation); QFETCH(bool, about); Fixture f; f.recipients->add(); f.finish();
        auto* model = f.recipients.get(); bool closed{false}; int at_end{-1};
        const auto close = [&] {
            const auto rows = model->rowCount(); model->closeAdmission();
            QCOMPARE(model->rowCount(), rows); closed = true;
        };
        if (mutation == 0) {
            connect(model, about ? &QAbstractItemModel::rowsAboutToBeInserted : &QAbstractItemModel::rowsInserted, this, close);
            connect(model, &QAbstractItemModel::rowsInserted, this, [&] { at_end = model->rowCount(); }); model->add();
        } else if (mutation == 1) {
            connect(model, about ? &QAbstractItemModel::rowsAboutToBeRemoved : &QAbstractItemModel::rowsRemoved, this, close);
            connect(model, &QAbstractItemModel::rowsRemoved, this, [&] { at_end = model->rowCount(); }); model->removeAt(1);
        } else {
            connect(model, about ? &QAbstractItemModel::modelAboutToBeReset : &QAbstractItemModel::modelReset, this, close);
            connect(model, &QAbstractItemModel::modelReset, this, [&] { at_end = model->rowCount(); }); model->clear();
        }
        QVERIFY(closed); QCOMPARE(at_end, mutation == 0 ? 3 : mutation == 1 ? 1 : about ? 2 : 1);
        QCOMPARE(model->rowCount(), 0);
        f.recipients.reset(); f.finish(); QCOMPARE(f.commands.scheduler->pending(), size_t{0});
    }
    void quietCloseFencesAllEditorsThenRetiresBoundedBatchesWithoutFacade() {
        Fixture f;
        for (int i{0}; i < 96; ++i) f.recipients->add();
        f.finish();
        int destroyed{0};
        const auto count = f.recipients->count(); QCOMPARE(count, 97);
        const auto rows = f.recipients->recipients();
        QPointer<SendRecipient> first{rows.first()};
        auto* amount = first->amount(); amount->setSatoshi(4000); f.finish();
        const auto amount_text = amount->inputText(); const auto unit = amount->unit();
        const auto input_revision = amount->inputRevision(); const auto operation = amount->inputOperationId();
        QSignalSpy amount_signals{amount, &BitcoinAmount::amountChanged};
        QSignalSpy input_signals{amount, &BitcoinAmount::inputChanged};
        QSignalSpy operation_signals{amount, &BitcoinAmount::inputOperationChanged};
        for (auto* recipient : rows)
            connect(recipient, &QObject::destroyed, this, [&] { ++destroyed; });
        f.recipients->closeAdmission();
        QCOMPARE(f.recipients->count(), 0); QVERIFY(first);
        const auto address = first->address()->address();
        first->setAddress(Address(9));
        QCOMPARE(first->address()->address(), address);
        QCOMPARE(first->address()->pasteAddress(QString(100000, QLatin1Char(' ')) + Address(9), 0, 0), -1);
        QVERIFY(!first->address()->normalizationPending());
        amount->setSatoshi(9000); amount->fromDisplay("5"); amount->setUnit(BitcoinAmount::Unit::SAT);
        amount->resetInput(); amount->format(); amount->restoreInput();
        QCOMPARE(amount->requestPaste("9", 0, 0), quint64{0});
        QCOMPARE(amount->satoshi(), qint64{4000}); QCOMPARE(amount->unit(), unit);
        QCOMPARE(amount->inputText(), amount_text); QCOMPARE(amount->inputRevision(), input_revision);
        QCOMPARE(amount->inputOperationId(), operation);
        QVERIFY(amount_signals.empty()); QVERIFY(input_signals.empty()); QVERIFY(operation_signals.empty());
        f.commands.scheduler->closeAdmission(); f.commands.drain();
        QCOMPARE(destroyed, 0); QVERIFY(f.commands.scheduler->pending() > 0);
        QVERIFY(f.publishOne()); QCOMPARE(destroyed, 32); QVERIFY(first);
        f.recipients.reset(); // Remaining GUI handoffs have an independent owner.
        f.finish();
        QCOMPARE(destroyed, count); QVERIFY(!first);
        QCOMPARE(f.commands.scheduler->pending(), size_t{0});
    }
    void validationAndAggregateDoNotBecomeReadyBeforeTheirGuiPublication() {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 1000);
        QVERIFY(recipient->validationPending()); QVERIFY(!recipient->isValid()); QVERIFY(f.recipients->validationPending());
        f.commands.drain(); QVERIFY(!recipient->isValid()); QVERIFY(!f.recipients->allValid());
        while (f.publishOne()) {}
        QVERIFY(recipient->isValid()); // The row result is now acknowledged.
        QVERIFY(f.recipients->validationPending()); QVERIFY(!f.recipients->draftSnapshot());
        f.finish(); QVERIFY(f.recipients->allValid());
        const auto snapshot = f.recipients->draftSnapshot(); QVERIFY(snapshot);
        QCOMPARE(snapshot->total, CAmount{1000}); QCOMPARE(snapshot->recipients.size(), size_t{1});
        QCOMPARE(snapshot->recipients[0].destination, DecodeDestination(Address().toStdString()));
    }
    void interimAggregateRetainsPendingRowsUntilTheirOriginalValidationPublishes() {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 1000);
        f.commands.drain(); QVERIFY(f.publications.size() >= 2);
        auto row_publication = std::move(f.publications.front()); f.publications.pop_front();
        while (f.publishOne()) {}
        QVERIFY(recipient->validationPending());
        QVERIFY(f.recipients->validationPending()); QVERIFY(!f.recipients->draftSnapshot());
        row_publication(); f.finish();
        QVERIFY(!f.recipients->validationPending()); QVERIFY(f.recipients->allValid());
        QCOMPARE(f.recipients->draftSnapshot()->pending_count, 0);
    }
    void staleDraftCannotAcquireAnEarlierValidDestination() {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 1000);
        f.commands.drain();
        recipient->setAddress("malformed");
        while (f.publishOne()) {}
        QVERIFY(!recipient->isValid()); QVERIFY(!f.recipients->allValid());
        f.finish(); QVERIFY(!recipient->addressError().isEmpty()); QVERIFY(!f.recipients->allValid());
    }
    void dustAndAmountDecisionsAreOwnedByDomain_data() {
        QTest::addColumn<qint64>("amount"); QTest::addColumn<QString>("error");
        QTest::newRow("dust") << qint64{1} << QStringLiteral("too small");
        QTest::newRow("zero") << qint64{0} << QStringLiteral("greater than zero");
        QTest::newRow("over-max") << qint64{MAX_MONEY + 1} << QStringLiteral("maximum limit");
    }
    void dustAndAmountDecisionsAreOwnedByDomain() {
        QFETCH(qint64, amount); QFETCH(QString, error);
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, amount); f.finish();
        QVERIFY(!recipient->isValid()); QVERIFY2(recipient->amountError().contains(error), qPrintable(recipient->amountError()));
        QVERIFY(!f.recipients->allValid());
    }
    void duplicateDestinationsAndTotalsUseTheImmutableDomainDraft() {
        Fixture f; f.fill(f.recipients->currentRecipient(), 4, 1000);
        f.recipients->add(); f.fill(f.recipients->currentRecipient(), 4, 2000); f.finish();
        QCOMPARE(f.recipients->totalAmountSatoshi(), qint64{3000}); QVERIFY(!f.recipients->allValid());
        QCOMPARE(f.recipients->validationError(), QStringLiteral("Recipient addresses must be unique."));
        const auto old = f.recipients->draftSnapshot(); QVERIFY(old);
        f.recipients->currentRecipient()->setAddress(Address(5)); f.finish();
        QVERIFY(f.recipients->allValid()); QCOMPARE(old->recipients[1].address, Address(4));
        QCOMPARE(f.recipients->draftSnapshot()->recipients[1].address, Address(5));
    }
    void changedEditorFieldsCannotReuseTheLastValidatedDraft() {
        Fixture f; auto* recipient=f.recipients->currentRecipient();f.fill(recipient,4,1000);f.finish();
        const auto original=f.recipients->draftSnapshot();QVERIFY(original);QVERIFY(original->all_valid);
        recipient->setLabel("new label");recipient->setMessage("new message");f.fill(recipient,5,2000);
        QVERIFY(recipient->validationPending());QVERIFY(!recipient->isValid());QVERIFY(!f.recipients->draftSnapshot());
        f.commands.drain();QVERIFY(recipient->validationPending());QVERIFY(!f.recipients->draftSnapshot());
        f.finish();const auto current=f.recipients->draftSnapshot();QVERIFY(current);QVERIFY(current->all_valid);
        QCOMPARE(current->recipients[0].address,Address(5));QCOMPARE(current->recipients[0].amount,CAmount{2000});
        QCOMPARE(current->recipients[0].label,QStringLiteral("new label"));QCOMPARE(current->recipients[0].message,QStringLiteral("new message"));
        QCOMPARE(original->recipients[0].address,Address(4));QCOMPARE(original->recipients[0].amount,CAmount{1000});
        QVERIFY(original->recipients[0].label.isEmpty());QVERIFY(original->recipients[0].message.isEmpty());
    }
    void aggregateRejectsMonetaryOverflowWithoutPublishingAZeroEstimate_data() {
        QTest::addColumn<qint64>("amount");
        QTest::newRow("monetary-range") << qint64{MAX_MONEY};
        QTest::newRow("signed-overflow") << std::numeric_limits<qint64>::max();
    }
    void aggregateRejectsMonetaryOverflowWithoutPublishingAZeroEstimate() {
        QFETCH(qint64, amount); Fixture f; f.fill(f.recipients->currentRecipient(), 4, amount);
        f.recipients->add(); f.fill(f.recipients->currentRecipient(), 5, amount); f.finish();
        const auto snapshot = f.recipients->draftSnapshot(); QVERIFY(snapshot);
        QVERIFY(!snapshot->amount_total_valid); QVERIFY(!snapshot->all_valid);
        QVERIFY(snapshot->total != 0); QCOMPARE(snapshot->total_text, QStringLiteral("—"));
        QVERIFY(!snapshot->error.isEmpty());
    }
    void manyRecipientsHaveNoCleanupSlotCapAndNativePresentationYields() {
        Fixture f; f.fill(f.recipients->currentRecipient(), 4, 1000);
        for (int i = 1; i < 65; ++i) { f.recipients->add(); f.fill(f.recipients->currentRecipient(), static_cast<unsigned char>(4 + i), 1000); }
        f.finish(); QVERIFY(f.recipients->allValid()); QCOMPARE(f.recipients->draftSnapshot()->recipients.size(), size_t{65});
        QPersistentModelIndex front{f.recipients->index(0)};
        const auto original = front.data(SendRecipientsListModel::RecipientRole);
        f.recipients->clearToFront();
        QCOMPARE(f.recipients->count(), 33); QVERIFY(f.recipients->validationPending());
        f.finish(); QCOMPARE(f.recipients->count(), 1); QVERIFY(front.isValid());
        QCOMPARE(front.data(SendRecipientsListModel::RecipientRole), original);
        QVERIFY(f.recipients->allValid()); QCOMPARE(f.recipients->totalAmountSatoshi(), qint64{1000});
    }
    void supersedingUnitIntentCannotBeReplacedByAnOlderQueuedChunk() {
        Fixture f;
        for (int i = 1; i < 65; ++i) f.recipients->add();
        f.finish(); f.recipients->setCurrentIndex(0);
        f.recipients->currentRecipient()->amount()->setUnit(BitcoinAmount::Unit::SAT);
        QCOMPARE(f.recipients->data(f.recipients->index(32), SendRecipientsListModel::AmountUnitLabelRole).toString(), QStringLiteral("BTC"));
        f.recipients->currentRecipient()->amount()->setUnit(BitcoinAmount::Unit::MilliBTC);
        f.finish();
        const auto rows = f.recipients->recipients();
        for (const auto* recipient : rows) QCOMPARE(recipient->amount()->unit(), BitcoinAmount::Unit::MilliBTC);
    }
    void removedRecipientHeldOutcomeCannotRejoinTheList() {
        Fixture f; f.fill(f.recipients->currentRecipient(), 4, 1000); f.finish();
        f.recipients->add(); auto* removed = f.recipients->currentRecipient(); f.fill(removed, 5, 2000);
        const auto token = removed->identity();
        f.commands.drain(); f.recipients->removeAt(1); f.finish();
        QCOMPARE(f.recipients->count(), 1); QCOMPARE(f.recipients->totalAmountSatoshi(), qint64{1000});
        QVERIFY(f.recipients->allValid()); QCOMPARE(f.recipients->draftSnapshot()->positions.count(token), size_t{0});
    }
    void rejectedDomainWorkNeverAdmitsTheLastValidDraft() {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 1000); f.finish();
        QVERIFY(f.recipients->allValid());
        f.commands.rejectLane(execution::Lane::Domain);
        recipient->setAddress(Address(5)); f.finish();
        QVERIFY(!recipient->isValid()); QVERIFY(!f.recipients->allValid()); QVERIFY(!recipient->addressError().isEmpty());
        recipient->retryPublications(); f.finish(); QVERIFY(recipient->isValid()); QVERIFY(f.recipients->allValid());
    }
    void lostGuiPublicationRetriesWithoutRepeatingFieldIntent() {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fail_post = true;
        f.fill(recipient, 4, 1000); f.commands.drain(); QVERIFY(!recipient->isValid());
        f.fail_post = false; f.recipients->retryPublications(); f.finish();
        QVERIFY(recipient->isValid()); QVERIFY(f.recipients->allValid());
        QCOMPARE(recipient->address()->address(), Address());
    }
    void quietCloseAndObserverLossDoNotPublishAcceptedValidation() {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 1000);
        QSignalSpy changed{recipient, &SendRecipient::isValidChanged};
        recipient->closeAdmission(); f.recipients->closeAdmission(); f.recipients.reset();
        f.commands.drain(); while (f.publishOne()) {}
        QCOMPARE(changed.count(), 0);
    }
    void realValidationUsesSharedDomainThread() {
        BackendExecutor executor;
        const auto recording = std::make_shared<ThreadRecordingScheduler>(executor.scheduler());
        {
            SendRecipient recipient{nullptr}; recipient.setOpening({}, {recording, {}});
            recipient.setAddress(Address()); recipient.amount()->setSatoshi(1000);
            QTRY_VERIFY(!recipient.validationPending()); QVERIFY(recipient.isValid());
            QVERIFY(recording->thread(execution::Lane::Domain));
            QVERIFY(recording->thread(execution::Lane::Domain) != QThread::currentThread());
            recipient.closeAdmission();
        }
        executor.shutdown(); QTRY_VERIFY(executor.isDrained());
    }
};
BITCOINQML_REGISTER_QT_TEST(SendRecipientTests)
#include <test_sendrecipient.moc>
