// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bumptransactionmodel.h>
#include <qml/guipresentation.h>
#include <qml/secretinput.h>
#include <QPointer>

namespace {
QString ErrorMessage(walletbump::Error error)
{
    switch (error) {
    case walletbump::Error::None: return {};
    case walletbump::Error::Invalid: return BumpTransactionModel::tr("Invalid fee-bump request.");
    case walletbump::Error::InvalidId: return BumpTransactionModel::tr("Invalid transaction ID.");
    case walletbump::Error::Ineligible: return BumpTransactionModel::tr("Transaction can no longer be bumped.");
    case walletbump::Error::Create: return BumpTransactionModel::tr("Failed to create bump transaction.");
    case walletbump::Error::NeedsUnlock: return BumpTransactionModel::tr("Enter your wallet password to update this transaction.");
    case walletbump::Error::IncorrectPassphrase: return BumpTransactionModel::tr("The wallet password you entered was incorrect.");
    case walletbump::Error::Sign: return BumpTransactionModel::tr("Failed to sign transaction.");
    case walletbump::Error::Changed: return BumpTransactionModel::tr("Signer changed the reviewed replacement transaction.");
    case walletbump::Error::RelockFailed: return BumpTransactionModel::tr("The wallet lock could not be restored. Check its security state before retrying.");
    case walletbump::Error::Commit: return BumpTransactionModel::tr("Failed to commit transaction.");
    case walletbump::Error::Uncertain: return BumpTransactionModel::tr("The fee-bump outcome could not be confirmed. Check the wallet before retrying.");
    }
    return {};
}
} // namespace

BumpTransactionModel::BumpTransactionModel(interfaces::Wallet* wallet, QObject* parent) : QObject(parent), m_wallet(wallet) {}
void BumpTransactionModel::setOpening(std::shared_ptr<walletopening::Session> opening, execution::SnapshotServices services)
{
    if (m_closed || busy() || (m_opening && m_opening != opening)) return;
    m_opening = std::move(opening); m_services = std::move(services);
    if (!m_services.scheduler && m_opening) m_services.scheduler = m_opening->scheduler();
    if (!m_services.post) m_services.post = presentation::QueuedPost();
}
void BumpTransactionModel::closeAdmission()
{
    m_closed = true; m_state = Closed;
    // Accepted Native commands own their original effects and receipts. The
    // quiet observer must release idle prepared-review cleanup leases now.
    m_prepare_operation.reset(); m_commit_operation.reset(); m_review.reset();
    m_preparing = m_committing = false;
}
void BumpTransactionModel::retryPublications()
{
    if (m_closed) return;
    const QPointer<BumpTransactionModel> guard{this};
    if (m_preparing) publishPreparation(m_attempt_id, m_preparation_revision);
    if (!guard || m_closed) return;
    if (m_committing) publishCommit(m_attempt_id, m_commit_id);
}
void BumpTransactionModel::setSecurityStateChangedFn(std::function<void()> fn) { m_security_state_changed = std::move(fn); }
bool BumpTransactionModel::publish()
{
    const QPointer<BumpTransactionModel> observer{this};
    Q_EMIT needsUnlockChanged(); if (!observer) return false;
    Q_EMIT resultChanged(); if (!observer) return false;
    Q_EMIT stateChanged(); return bool(observer);
}
void BumpTransactionModel::clearPresentation()
{
    m_old_fee.clear(); m_new_fee.clear(); m_increase.clear(); m_old_txid.clear(); m_new_txid.clear(); m_error.clear(); m_unlock_error.clear(); m_needs_unlock = false;
}
quint64 BumpTransactionModel::requestPrepareFeeBump(const QString& txid, unsigned int target_blocks)
{
    if (m_closed || busy()) return 0;
    if (!m_opening || !m_opening->available() || !m_services.scheduler) {
        m_error = tr("No wallet available."); m_state = Failed; publish(); return 0;
    }
    const auto action = m_opening->startAction();
    if (!action) return 0;
    const auto attempt = m_attempt_id + 1, revision = m_view_revision + 1;
    const QPointer<BumpTransactionModel> observer{this};
    auto operation = std::make_shared<execution::OwnedCommand<walletbump::Preparation>>(m_services.scheduler,
        [post = m_services.post, observer, attempt, revision] { post([observer, attempt, revision] { if (observer) observer->publishPreparation(attempt, revision); }); });
    if (!operation->startWith([opening = m_opening, action, txid, target_blocks](auto done, auto failed) {
        return opening->perform(action, [opening, txid, target_blocks](interfaces::Wallet& wallet) { return walletbump::Prepare(wallet, txid, target_blocks, opening); },
            std::move(done), std::move(failed));
    })) { m_error = tr("The fee-bump preparation could not be queued."); m_state = Failed; publish(); return 0; }
    m_attempt_id = attempt; m_view_revision = revision; m_preparation_revision = revision;
    m_prepare_operation = std::move(operation); m_review.reset();
    clearPresentation(); m_preparing = true; m_state = Preparing;
    publish(); return attempt;
}
void BumpTransactionModel::publishPreparation(quint64 attempt, quint64 revision)
{
    if (!m_preparing || attempt != m_attempt_id || !m_prepare_operation) return;
    const auto result = m_prepare_operation->result();
    if (!result) return;
    m_preparing = false;
    if (m_closed) return;
    if (revision != m_view_revision) { m_state = Idle; publish(); return; }
    const auto value = result->value.value_or(walletbump::Preparation{});
    if (result->error || value.error != walletbump::Error::None || !value.review) {
        const auto error = result->error ? walletbump::Error::Uncertain : value.error;
        m_error = value.message.isEmpty() ? ErrorMessage(error) : value.message;
        m_state = error == walletbump::Error::Uncertain ? Uncertain : Failed;
    } else {
        m_review = value.review;
        m_old_fee = m_review->old_fee_text; m_new_fee = m_review->new_fee_text;
        m_increase = m_review->increase_text; m_old_txid = m_review->old_txid;
        m_state = NeedsConfirmation;
    }
    publish();
}
quint64 BumpTransactionModel::requestConfirmFeeBump() { return confirm({}); }
quint64 BumpTransactionModel::requestConfirmFeeBumpWithPassphrase(const QString& passphrase) {
    if (m_closed || busy() || m_state != NeedsConfirmation) return 0;
    const auto captured = secretinput::Capture(m_opening ? m_opening->scheduler() : nullptr, passphrase);
    if (!captured) { m_error = tr("The password could not be queued within the available capacity."); publish(); return 0; }
    return confirm({}, captured.input);
}
quint64 BumpTransactionModel::confirm(std::optional<SecureString> passphrase, secretinput::Captured captured)
{
    if (m_closed || busy() || m_state != NeedsConfirmation || !m_review || !m_opening || !m_opening->available()) return 0;
    const auto action = m_opening->startAction();
    if (!action) return 0;
    const auto attempt = m_attempt_id, commit = m_commit_id + 1;
    const QPointer<BumpTransactionModel> observer{this};
    auto secret = std::make_shared<std::optional<SecureString>>(std::move(passphrase));
    auto operation = std::make_shared<execution::OwnedCommand<walletbump::Commit>>(m_services.scheduler,
        [post = m_services.post, observer, attempt, commit] { post([observer, attempt, commit] { if (observer) observer->publishCommit(attempt, commit); }); });
    const auto bytes = 4096 + secretinput::Bytes(captured) + (secret->has_value() ? secret->value().capacity() : 0);
    if (!operation->startWith([opening = m_opening, action, review = m_review, secret, captured, bytes](auto done, auto failed) {
        return opening->perform(action, [review, secret, captured](interfaces::Wallet& wallet) { return walletbump::Execute(wallet, *review, std::move(*secret), captured); },
            std::move(done), std::move(failed), bytes);
    })) { m_error = tr("The fee-bump commit could not be queued."); publish(); return 0; }
    m_commit_id = commit;
    m_commit_operation = std::move(operation); m_committing = true;
    m_needs_unlock = false; m_error.clear(); m_unlock_error.clear(); m_state = Committing;
    publish(); return commit;
}
void BumpTransactionModel::publishCommit(quint64 attempt, quint64 commit)
{
    if (!m_committing || attempt != m_attempt_id || commit != m_commit_id || !m_commit_operation) return;
    const auto result = m_commit_operation->result();
    if (!result) return;
    m_committing = false;
    if (m_closed) return;
    auto value = result->value.value_or(walletbump::Commit{});
    if (result->error) value.error = walletbump::Error::Uncertain;
    m_needs_unlock = value.error == walletbump::Error::NeedsUnlock || value.error == walletbump::Error::IncorrectPassphrase;
    m_unlock_error = value.error == walletbump::Error::IncorrectPassphrase ? ErrorMessage(value.error) : QString{};
    if (value.committed) { m_new_txid = QString::fromStdString(value.txid.GetHex()); m_state = Succeeded; m_error.clear(); }
    else {
        m_error = value.message.isEmpty() ? ErrorMessage(value.error) : value.message;
        m_state = m_needs_unlock ? NeedsConfirmation : value.error == walletbump::Error::Uncertain || value.relock_failed ? Uncertain : Failed;
    }
    if (publish() && m_opening && m_opening->available() && m_security_state_changed) m_security_state_changed();
}
std::optional<walletbump::Commit> BumpTransactionModel::commitResult() const
{
    if (!m_commit_operation) return {};
    const auto result = m_commit_operation->result();
    return result ? result->value : std::nullopt;
}
void BumpTransactionModel::reset()
{
    if (m_closed) return;
    ++m_view_revision;
    // A view close invalidates presentation; an accepted commit keeps its
    // original review and must finish exactly once before another attempt.
    if (m_committing) return;
    clearPresentation();
    m_state = m_preparing ? Discarding : Idle;
    if (!m_preparing) m_review.reset();
    publish();
}
