// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETSEND_H
#define BITCOIN_QML_WALLETSEND_H

#include <qml/models/psbtfee.h>
#include <qml/walletrequests.h>
#include <qml/secretinput.h>
#include <atomic>

namespace walletsend {
enum class Submission { Unknown, Mempool, Confirmed, NotInMempool, Conflicted, Abandoned };
enum class Notice { Finished, Notes };
enum class Error { None, Rejected, CannotSign, NeedsUnlock, IncorrectPassphrase, InvalidFee, ChangedTransaction, Incomplete, Signer, Uncertain, Cancelled };
/** Explicit review withdrawal may win before the irreversible commit boundary.
 * Observer destruction does not withdraw an already admitted action. */
class CommitConsent {
public:
    bool withdrawn() const { return m_state.load() == Withdrawn; }
    bool claim() { State expected{Awaiting}; return m_state.compare_exchange_strong(expected, Committing); }
    void withdraw() { State expected{Awaiting}; m_state.compare_exchange_strong(expected, Withdrawn); }
private:
    enum State { Awaiting, Committing, Withdrawn };
    std::atomic<State> m_state{Awaiting};
};
struct Request {
    CTransactionRef reviewed;
    std::shared_ptr<const PartiallySignedTransaction> psbt;
    // Original review proved completion using public read-only wallet metadata.
    // This permits metadata recovery, never new signatures or wallet unlock.
    bool assessed_complete{false};
    std::shared_ptr<const psbtfee::Analysis> proof;
    std::optional<CAmount> approved_fee;
    std::optional<SecureString> passphrase;
    secretinput::Captured captured_passphrase;
    std::shared_ptr<CommitConsent> consent;
    QMap<QString, QString> notes;
};
struct Result {
    Error error{Error::None};
    CTransactionRef committed{};
    Submission submission{Submission::Unknown};
    std::optional<common::PSBTError> signer_error{};
    bool relock_failed{false}, notes_requested{false}, publication_failed{false};
    std::shared_ptr<walletrequests::Operation> notes{};
    execution::Rejection notes_rejection{execution::Rejection::None};
};
/** One accepted reviewed transaction, with its original opening and result.
 * Neither the effect nor consuming its outcome depends on GUI delivery. */
class Operation : public std::enable_shared_from_this<Operation>
{
public:
    using Notify = std::function<void(Notice)>;
    Operation(std::shared_ptr<walletopening::Session> opening,
              std::shared_ptr<walletrequests::Store> notes, Notify notify)
        : m_opening(std::move(opening)), m_notes(std::move(notes)), m_notify(std::move(notify)) {}
    execution::Admission start(Request request, std::shared_ptr<const walletopening::Session::Action> action = {});
    std::optional<Result> result() const;
private:
    void complete(Result result);
    void publicationFailed();
    const std::shared_ptr<walletopening::Session> m_opening;
    const std::shared_ptr<walletrequests::Store> m_notes;
    const Notify m_notify;
    mutable std::mutex m_mutex;
    bool m_started{false}, m_publication_failed{false};
    std::optional<Result> m_result;
};
} // namespace walletsend

namespace walletsigning {
enum class Error { None, InvalidFee, ChangedTransaction, SignerNotFound, SignerFailed, Fill, Failed, Uncertain };
struct Request {
    CTransactionRef reviewed;
    std::shared_ptr<const PartiallySignedTransaction> document;
    std::shared_ptr<const psbtfee::Analysis> proof;
    std::optional<CAmount> approved_fee;
};
struct Result {
    std::shared_ptr<const void> lifetime{};
    Error error{Error::None};
    std::optional<common::PSBTError> fill_error{};
    std::shared_ptr<const PartiallySignedTransaction> document{};
    CTransactionRef signed_transaction{};
    bool partial{false};
};
Result Approve(interfaces::Wallet& wallet, const Request& request, std::shared_ptr<walletopening::Session> opening = {});
// Production reserves this output before admitting the device action, so
// accepted work does not need new allocation admission after shutdown closes.
Result ApproveInto(interfaces::Wallet& wallet, const Request& request, std::shared_ptr<Result> output);
} // namespace walletsigning

#endif // BITCOIN_QML_WALLETSEND_H
