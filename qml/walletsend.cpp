// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletsend.h>
#include <qml/retainedvalue.h>
#include <qml/models/signertransaction.h>

#include <QScopeGuard>

namespace walletsend {
namespace {
bool SameTransaction(const CTransaction& reviewed, const CMutableTransaction& candidate)
{
    CMutableTransaction original{reviewed}, returned{candidate};
    for (auto* transaction : {&original, &returned}) for (auto& input : transaction->vin) {
        input.scriptSig.clear(); input.scriptWitness.SetNull();
    }
    return original.GetHash() == returned.GetHash();
}
Submission ObserveSubmission(interfaces::Wallet& wallet, const CTransaction& transaction)
{
    try {
        interfaces::WalletTxStatus status{}; bool in_mempool{false}; int num_blocks{0};
        std::vector<std::string> messages, payment_requests;
        const auto record = wallet.getWalletTxDetails(transaction.GetHash(), status, messages, payment_requests, in_mempool, num_blocks);
        if (!record.tx || record.tx->GetHash() != transaction.GetHash()) return Submission::Unknown;
        if (status.depth_in_main_chain > 0) return Submission::Confirmed;
        if (status.depth_in_main_chain < 0) return Submission::Conflicted;
        if (status.is_abandoned) return Submission::Abandoned;
        return in_mempool ? Submission::Mempool : Submission::NotInMempool;
    } catch (...) { return Submission::Unknown; }
}
Result Execute(interfaces::Wallet& wallet, Request request)
{
    Result result;
    bool relock{false};
    auto restore = [&] {
        if (!std::exchange(relock, false)) return;
        try { if (!wallet.lock()) result.relock_failed = true; }
        catch (...) { result.relock_failed = true; }
    };
    const auto cleanup = qScopeGuard([&] { restore(); });
    try {
        if (request.consent && request.consent->withdrawn()) { result.error = Error::Cancelled; return result; }
        if (request.captured_passphrase) {
            request.passphrase = secretinput::Materialize(request.captured_passphrase);
            request.captured_passphrase.reset();
        }
        CTransactionRef transaction = request.reviewed;
        if (request.psbt) {
            if (!request.proof || !psbtfee::MatchesReview(*request.psbt, *request.proof, request.approved_fee)) {
                result.error = Error::InvalidFee; return result;
            }
            PartiallySignedTransaction psbt{*request.psbt}; psbtfee::FillMissing(psbt, *request.proof);
            CMutableTransaction extracted;
            auto finalized = psbt;
            bool extracted_complete = FinalizeAndExtractPSBT(finalized, extracted);
            if (!extracted_complete && request.assessed_complete) {
                // The read-only assessment may have obtained redeem/witness
                // scripts from the public wallet. Export retains the original
                // document; recover that metadata only in this Native action.
                bool complete{false};
                result.signer_error = wallet.fillPSBT({.sign = false, .bip32_derivs = false}, nullptr, psbt, complete);
                if (result.signer_error) { result.error = Error::Signer; return result; }
                const auto returned = psbt.GetUnsignedTx();
                if (!returned || !SameTransaction(*request.reviewed, *returned)) { result.error = Error::ChangedTransaction; return result; }
                if (!psbtfee::MatchesReview(psbt, *request.proof, request.approved_fee)) { result.error = Error::InvalidFee; return result; }
                finalized = psbt;
                extracted_complete = FinalizeAndExtractPSBT(finalized, extracted);
                if (!extracted_complete) { result.error = Error::Incomplete; return result; }
            }
            if (!extracted_complete) {
                if (wallet.privateKeysDisabled() && !wallet.hasExternalSigner()) { result.error = Error::CannotSign; return result; }
                if (!wallet.privateKeysDisabled() && wallet.isCrypted() && wallet.isLocked()) {
                    if (!request.passphrase) { result.error = Error::NeedsUnlock; return result; }
                    relock = true;
                    if (!wallet.unlock(*request.passphrase)) { relock = false; result.error = Error::IncorrectPassphrase; return result; }
                }
                // Drop the submitted secret before a potentially blocking signer.
                request.passphrase.reset();
                bool complete{false}; size_t signed_inputs{0};
                result.signer_error = wallet.fillPSBT({.sign = true, .bip32_derivs = true}, &signed_inputs, psbt, complete);
                if (result.signer_error) { result.error = Error::Signer; restore(); return result; }
                const auto returned = psbt.GetUnsignedTx();
                if (!returned || !SameTransaction(*request.reviewed, *returned)) { result.error = Error::ChangedTransaction; restore(); return result; }
                if (!complete || !FinalizeAndExtractPSBT(psbt, extracted)) { result.error = Error::Incomplete; restore(); return result; }
                restore();
            }
            if (!SameTransaction(*request.reviewed, extracted)) { result.error = Error::ChangedTransaction; return result; }
            if (!psbtfee::MatchesReview(psbt, *request.proof, request.approved_fee)) { result.error = Error::InvalidFee; return result; }
            transaction = MakeTransactionRef(std::move(extracted));
        } else if (wallet.privateKeysDisabled() && !wallet.hasExternalSigner()) {
            result.error = Error::CannotSign; return result;
        }
        request.passphrase.reset();
        if (result.relock_failed) { result.error = Error::Uncertain; return result; }
        // Core retains the supplied shared_ptr in wallet history. Its owner
        // must not be an app cleanup lease: that would make session retirement
        // wait for a transaction whose destruction waits for wallet retirement.
        // Copy only on Native, before the original public commit boundary.
        transaction = MakeTransactionRef(*transaction);
        if (request.consent && !request.consent->claim()) { result.error = Error::Cancelled; return result; }
        wallet.commitTransaction(transaction, {});
        result.committed = std::move(transaction);
        // Submission is a subsequent observation, never proof of peer receipt.
        // A failed read must not erase the already confirmed commit outcome.
        result.submission = ObserveSubmission(wallet, *result.committed);
    } catch (const secretinput::Failure&) { restore(); result.error = Error::Rejected; }
    catch (...) { restore(); result.error = Error::Uncertain; }
    return result;
}
} // namespace

execution::Admission Operation::start(Request request, std::shared_ptr<const walletopening::Session::Action> action)
{
    {
        std::lock_guard lock{m_mutex};
        if (std::exchange(m_started, true) || !request.reviewed) return {{}, execution::Rejection::Invalid};
    }
    if (action && action->identity() != m_opening->identity()) return {{}, execution::Rejection::Invalid};
    if (!action) action = m_opening->startAction();
    if (!action) { complete(Result{.error = Error::Rejected}); return {{}, execution::Rejection::Closed}; }
    auto native_result = walletopening::RetainValues<CTransactionRef>(m_opening, [] { return std::make_unique<CTransactionRef>(); });
    if (!native_result) { complete(Result{.error = Error::Rejected}); return {{}, execution::Rejection::Capacity}; }
    const auto self = shared_from_this();
    // Qt's implicitly shared map is captured without walking the GUI's data.
    // Notes are validated by their own bounded Domain command after commit.
    const auto notes = request.notes;
    const size_t bytes = sizeof(Request) + (request.passphrase ? request.passphrase->capacity() : 0)
        + secretinput::Bytes(request.captured_passphrase);
    const auto admitted = m_opening->perform(action, [request = std::move(request), native_result](interfaces::Wallet& wallet) mutable {
        auto result = Execute(wallet, std::move(request));
        if (result.committed) {
            *native_result = result.committed;
            result.committed = CTransactionRef{native_result, native_result->get()};
        }
        return result;
    }, [self, action, notes](Result result) {
        result.notes_requested = !notes.isEmpty();
        if (result.committed && result.notes_requested) {
            if (self->m_notes) {
                try {
                    auto accepted = self->m_notes->submitSentLabels(action, result.committed->GetHash(), notes,
                        [notify = self->m_notify, weak = std::weak_ptr<Operation>{self}] {
                            try { notify(Notice::Notes); }
                            catch (...) { if (const auto owner = weak.lock()) owner->publicationFailed(); }
                        });
                    result.notes = std::move(accepted.operation); result.notes_rejection = accepted.rejection;
                } catch (...) { result.notes_rejection = execution::Rejection::Transport; }
            } else result.notes_rejection = execution::Rejection::Closed;
        }
        self->complete(std::move(result));
    }, [self](std::exception_ptr error) {
        try { std::rethrow_exception(error); }
        catch (const execution::NotStarted&) { self->complete(Result{.error = Error::Rejected}); }
        catch (...) { self->complete(Result{.error = Error::Uncertain}); }
    }, bytes);
    if (!admitted) complete(Result{.error = Error::Rejected});
    return admitted;
}

std::optional<Result> Operation::result() const
{
    std::lock_guard lock{m_mutex}; return m_result;
}
void Operation::complete(Result result)
{
    { std::lock_guard lock{m_mutex}; if (m_result) return; result.publication_failed |= m_publication_failed; m_result = std::move(result); }
    try { m_notify(Notice::Finished); }
    catch (...) { publicationFailed(); }
}
void Operation::publicationFailed()
{
    std::lock_guard lock{m_mutex}; m_publication_failed = true;
    if (m_result) m_result->publication_failed = true;
}
} // namespace walletsend

namespace walletsigning {
static Result ExecuteApproval(interfaces::Wallet& wallet, const Request& request)
{
    Result result;
    if (!request.reviewed || !request.proof || !request.approved_fee) {
        result.error = Error::InvalidFee; return result;
    }
    if (!wallet.hasExternalSigner()) { result.error = Error::SignerNotFound; return result; }
    CMutableTransaction unsigned_tx{*request.reviewed};
    for (auto& input : unsigned_tx.vin) { input.scriptSig.clear(); input.scriptWitness.SetNull(); }
    PartiallySignedTransaction document = request.document ? *request.document
        : PartiallySignedTransaction{unsigned_tx, 2};
    if (!psbtfee::MatchesReview(document, *request.proof, request.approved_fee)) {
        result.error = Error::InvalidFee; return result;
    }
    psbtfee::FillMissing(document, *request.proof);
    const auto approved = std::make_shared<const PartiallySignedTransaction>(document);
    bool complete{false};
    try {
        auto error = wallet.fillPSBT({.sign = false, .bip32_derivs = true}, nullptr, document, complete);
        if (!error && !complete) error = wallet.fillPSBT({.sign = true, .bip32_derivs = true}, nullptr, document, complete);
        if (error) {
            const auto returned = document.GetUnsignedTx();
            if (returned && MatchesUnsignedTransaction(unsigned_tx, *returned) &&
                psbtfee::MatchesReview(document, *request.proof, request.approved_fee)) {
                result.document = std::make_shared<const PartiallySignedTransaction>(document);
                result.partial = true;
            }
            result.fill_error = error;
            result.error = *error == common::PSBTError::EXTERNAL_SIGNER_NOT_FOUND ? Error::SignerNotFound
                : *error == common::PSBTError::EXTERNAL_SIGNER_FAILED ? Error::SignerFailed : Error::Fill;
            return result;
        }
        const auto returned = document.GetUnsignedTx();
        if (!returned || !MatchesUnsignedTransaction(unsigned_tx, *returned)) {
            result.document = approved;
            result.error = Error::ChangedTransaction; return result;
        }
        CMutableTransaction signed_tx;
        const bool extracted = FinalizeAndExtractPSBT(document, signed_tx);
        if (!psbtfee::MatchesReview(document, *request.proof, request.approved_fee)) {
            result.document = approved;
            result.error = Error::InvalidFee; return result;
        }
        result.document = std::make_shared<const PartiallySignedTransaction>(std::move(document));
        result.partial = !extracted;
        if (extracted) result.signed_transaction = MakeTransactionRef(std::move(signed_tx));
    } catch (...) {
        result.error = Error::Uncertain;
        try {
            const auto returned = document.GetUnsignedTx();
            if (returned && MatchesUnsignedTransaction(unsigned_tx, *returned) &&
                psbtfee::MatchesReview(document, *request.proof, request.approved_fee)) {
                result.document = std::make_shared<const PartiallySignedTransaction>(document);
                result.partial = true;
            }
        } catch (...) {}
    }
    return result;
}
Result Approve(interfaces::Wallet& wallet, const Request& request, std::shared_ptr<walletopening::Session> opening)
{
    // Standalone backend tests may inspect native values entirely on their
    // controlled worker. Production reserves ownership before device execution.
    if (!opening) return ExecuteApproval(wallet, request);
    auto owner = walletopening::RetainValues<Result>(opening, [] { return std::make_unique<Result>(); });
    if (!owner) return Result{.error = Error::Failed};
    return ApproveInto(wallet, request, std::move(owner));
}
Result ApproveInto(interfaces::Wallet& wallet, const Request& request, std::shared_ptr<Result> owner)
{
    if (!owner) return Result{.error = Error::Failed};
    *owner = ExecuteApproval(wallet, request);
    Result result = *owner;
    result.lifetime = owner;
    if (result.document) result.document = std::shared_ptr<const PartiallySignedTransaction>{owner, result.document.get()};
    if (result.signed_transaction) result.signed_transaction = CTransactionRef{owner, result.signed_transaction.get()};
    return result;
}
} // namespace walletsigning
