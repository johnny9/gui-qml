// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/walletsecurity.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>

#include <QScopeGuard>
#include <QSemaphore>
#include <QtTest/QtTest>

#include <stdexcept>

namespace {
using execution::Lane;
using walletsecurity::Error;
using walletsecurity::Kind;
struct Wallet final : StubWallet {
    bool privateKeysDisabled() override { return no_keys; }
    bool isCrypted() override { return encrypted; }
    bool isLocked() override { return locked; }
    bool encryptWallet(const SecureString& password) override
    {
        ++encryptions;
        if (password != SecureString{"replacement"}) return false;
        encrypted = locked = true;
        if (throw_after_effect) throw std::runtime_error("synthetic post-effect failure");
        return true;
    }
    util::Expected<void, wallet::WalletError> changeWalletPassphrase(const SecureString& current, const SecureString& replacement) override
    {
        ++changes;
        if (current != SecureString{"current"}) return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("incorrect")}};
        if (replacement != SecureString{"replacement"}) return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::GenericError, Untranslated("database failure")}};
        return {};
    }
    void remove() override { ++removals; }
    int encryptions{0}, changes{0}, removals{0};
    bool no_keys{false}, encrypted{false}, locked{false}, throw_after_effect{false};
};
struct Fixture {
    ManualRoutedExecution commands, retirement;
    std::shared_ptr<Wallet> wallet{std::make_shared<Wallet>()};
    std::shared_ptr<walletopening::Session> session{std::make_shared<walletopening::Session>(
        commands.scheduler, retirement.scheduler, std::make_shared<std::atomic_bool>(false), [] {})};
    Fixture() { session->start(wallet); commands.drain(); session->take(); }
    ~Fixture() { session->retire(); commands.drain(); retirement.drain(); }
};
}

class WalletSecurityTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void realWorkerKeepsGuiResponsiveAndReleasesInterfaceOffGui()
    {
        struct State {
            QSemaphore entered, release;
            std::atomic<int> metadata{0}, outcomes{0};
            std::atomic<bool> encrypted{false}, matching_secret{false};
            std::atomic<QThread*> effect_thread{nullptr}, outcome_thread{nullptr}, destruction_thread{nullptr};
        };
        struct RealWallet final : StubWallet {
            explicit RealWallet(std::shared_ptr<State> value) : state(std::move(value)) {}
            ~RealWallet() override { state->destruction_thread = QThread::currentThread(); }
            bool isCrypted() override { return state->encrypted.load(); }
            bool isLocked() override { return state->encrypted.load(); }
            bool encryptWallet(const SecureString& secret) override
            {
                state->effect_thread = QThread::currentThread();
                state->matching_secret = secret == SecureString(40'000, 'p');
                state->entered.release(); state->release.acquire();
                state->encrypted = true;
                return true;
            }
            const std::shared_ptr<State> state;
        };
        const auto state = std::make_shared<State>();
        const auto unblock = qScopeGuard([state] { state->release.release(); });
        BackendExecutor commands{execution::Limits::Bounded()}, retirement;
        auto native = std::make_shared<RealWallet>(state);
        auto session = std::make_shared<walletopening::Session>(commands.scheduler(), retirement.scheduler(),
            std::make_shared<std::atomic_bool>(false), [state] { ++state->metadata; });
        QVERIFY(session->start(native));
        QTRY_VERIFY(state->metadata.load() > 0);
        auto operation = std::make_shared<walletsecurity::Operation>(session, Kind::Encrypt, [state] {
            state->outcome_thread = QThread::currentThread(); ++state->outcomes;
        });
        auto captured = secretinput::Capture(session->scheduler(), QString(40'000, QChar{'p'})); QVERIFY(captured);
        QVERIFY(operation->start({Kind::Encrypt, {}, {}, {}, captured.input})); captured.input.reset();
        QTRY_VERIFY(state->entered.available() > 0);
        QVERIFY(state->entered.tryAcquire());
        QVERIFY(state->effect_thread.load() != qApp->thread());
        native.reset(); session->retire(); commands.shutdown(); retirement.shutdown();
        bool heartbeat{false};
        QMetaObject::invokeMethod(qApp, [&] { heartbeat = true; }, Qt::QueuedConnection);
        QTRY_VERIFY(heartbeat);
        QVERIFY(!retirement.isDrained());
        QCOMPARE(state->outcomes.load(), 0);
        QVERIFY(!state->destruction_thread.load());
        state->release.release();
        QTRY_VERIFY(commands.isDrained() && retirement.isDrained());
        QCOMPARE(state->outcomes.load(), 1);
        QVERIFY(state->matching_secret.load());
        QVERIFY(state->outcome_thread.load() != qApp->thread());
        QVERIFY(state->destruction_thread.load() && state->destruction_thread.load() != qApp->thread());
        QVERIFY(operation->result() && operation->result()->error == Error::None);
    }
    void securityOutcomeWaitsForCoreThenDomain()
    {
        Fixture f;
        int notices{0};
        auto operation = std::make_shared<walletsecurity::Operation>(f.session, Kind::Encrypt, [&] { ++notices; });
        QVERIFY(operation->start({Kind::Encrypt, {}, SecureString{"replacement"}}));
        QCOMPARE(f.wallet->encryptions, 0);
        QVERIFY(!operation->result());
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->encryptions, 1);
        QVERIFY(f.wallet->encrypted);
        QCOMPARE(notices, 0);
        QVERIFY(!operation->result());
        QVERIFY(f.commands.step(Lane::Domain));
        QCOMPARE(notices, 1);
        const auto result = operation->result();
        QVERIFY(result && result->error == Error::None && result->state_known && result->encrypted && result->locked);
    }
    void throwingPublicationKeepsActualOutcomeAndDoesNotStrandDomain()
    {
        Fixture f; const auto baseline = f.commands.scheduler->pending();
        bool fail{true}; int notices{0};
        auto operation = std::make_shared<walletsecurity::Operation>(f.session, Kind::Encrypt, [&] {
            ++notices; if (fail) throw std::runtime_error{"GUI post rejected"};
        });
        QVERIFY(operation->start({Kind::Encrypt, {}, SecureString{"replacement"}}));
        QVERIFY(f.commands.step(Lane::Core)); QCOMPARE(f.wallet->encryptions, 1); QVERIFY(!operation->result());
        QVERIFY(f.commands.step(Lane::Domain)); f.commands.drain();
        const auto result = operation->result(); QVERIFY(result); QCOMPARE(result->error, Error::None);
        QVERIFY(result->state_known); QVERIFY(result->encrypted); QVERIFY(result->publication_failed);
        QCOMPARE(f.commands.scheduler->pending(), baseline); QVERIFY(f.commands.activeIdle()); QVERIFY(f.commands.queuedIdle());
        fail = false; operation->retryPublication(); QCOMPARE(notices, 2); QCOMPARE(f.wallet->encryptions, 1);
        QVERIFY(!operation->result()->publication_failed); QCOMPARE(operation->result()->error, Error::None);
    }
    void validationAndTypedBackendErrors_data()
    {
        QTest::addColumn<int>("kind"); QTest::addColumn<QString>("current"); QTest::addColumn<QString>("replacement");
        QTest::addColumn<bool>("no_keys"); QTest::addColumn<int>("error"); QTest::addColumn<int>("calls");
        QTest::newRow("encrypt-empty") << int(Kind::Encrypt) << "" << "" << false << int(Error::MissingNew) << 0;
        QTest::newRow("change-both-empty") << int(Kind::ChangePassphrase) << "" << "" << false << int(Error::MissingCurrent) << 0;
        QTest::newRow("change-new-empty") << int(Kind::ChangePassphrase) << "current" << "" << false << int(Error::MissingNew) << 0;
        QTest::newRow("watch-only") << int(Kind::Encrypt) << "" << "replacement" << true << int(Error::NoPrivateKeys) << 0;
        QTest::newRow("wrong-current") << int(Kind::ChangePassphrase) << "wrong" << "replacement" << false << int(Error::IncorrectCurrent) << 1;
        QTest::newRow("database-error") << int(Kind::ChangePassphrase) << "current" << "db-failure" << false << int(Error::ChangeFailed) << 1;
        QTest::newRow("change-success") << int(Kind::ChangePassphrase) << "current" << "replacement" << false << int(Error::None) << 1;
        QTest::newRow("encrypt-failure") << int(Kind::Encrypt) << "" << "unaccepted" << false << int(Error::EncryptionFailed) << 1;
    }
    void validationAndTypedBackendErrors()
    {
        QFETCH(int, kind); QFETCH(QString, current); QFETCH(QString, replacement); QFETCH(bool, no_keys); QFETCH(int, error); QFETCH(int, calls);
        Fixture f; f.wallet->no_keys = no_keys;
        auto operation = std::make_shared<walletsecurity::Operation>(f.session, static_cast<Kind>(kind), [] {});
        const QByteArray old = current.toUtf8(), next = replacement.toUtf8();
        QVERIFY(operation->start({static_cast<Kind>(kind), SecureString{old.constData()}, SecureString{next.constData()}}));
        f.commands.drain();
        QVERIFY(operation->result());
        QCOMPARE(int(operation->result()->error), error);
        QCOMPARE(f.wallet->encryptions + f.wallet->changes, calls);
    }
    void duplicateStartNeverDuplicatesEffect()
    {
        Fixture f;
        auto operation = std::make_shared<walletsecurity::Operation>(f.session, Kind::Encrypt, [] {});
        QVERIFY(operation->start({Kind::Encrypt, {}, SecureString{"replacement"}}));
        QCOMPARE(operation->start({Kind::Encrypt, {}, SecureString{"replacement"}}).rejection, execution::Rejection::Invalid);
        f.commands.drain();
        QCOMPARE(f.wallet->encryptions, 1);
    }
    void acceptedSecurityEffectSurvivesCloseBeforeExecution()
    {
        Fixture f;
        auto operation = std::make_shared<walletsecurity::Operation>(f.session, Kind::Encrypt, [] {});
        QVERIFY(operation->start({Kind::Encrypt, {}, SecureString{"replacement"}}));
        f.session->retire(true);
        QVERIFY(!f.retirement.step(Lane::Cleanup));
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->encryptions, 1);
        QCOMPARE(f.wallet->removals, 0);
        QVERIFY(!f.retirement.step(Lane::Cleanup));
        QVERIFY(f.commands.step(Lane::Domain));
        QVERIFY(operation->result() && operation->result()->error == Error::None);
        f.retirement.drain();
        QCOMPARE(f.wallet->removals, 1);
    }
    void lostViewCannotAbandonAdmittedEffectOrOutcome()
    {
        Fixture f;
        int notifications{0};
        auto operation = std::make_shared<walletsecurity::Operation>(f.session, Kind::Encrypt, [&] { ++notifications; });
        const std::weak_ptr<walletsecurity::Operation> weak{operation};
        QVERIFY(operation->start({Kind::Encrypt, {}, SecureString{"replacement"}}));
        operation.reset();
        f.session->retire();
        QVERIFY(!weak.expired());
        f.commands.drain(); f.retirement.drain();
        QCOMPARE(f.wallet->encryptions, 1);
        QCOMPARE(notifications, 1);
        QVERIFY(weak.expired());
    }
    void uncertainPostEffectFailureIsRetainedAndNeverReplayed()
    {
        Fixture f; f.wallet->throw_after_effect = true;
        auto operation = std::make_shared<walletsecurity::Operation>(f.session, Kind::Encrypt, [] {});
        QVERIFY(operation->start({Kind::Encrypt, {}, SecureString{"replacement"}}));
        f.commands.drain();
        QVERIFY(f.wallet->encrypted);
        QVERIFY(operation->result() && operation->result()->error == Error::Uncertain);
        f.commands.drain();
        QCOMPARE(f.wallet->encryptions, 1);
    }
};

#include <test_walletsecurity.moc>
BITCOINQML_REGISTER_QT_TEST(WalletSecurityTests)
