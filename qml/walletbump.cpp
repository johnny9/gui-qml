// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletbump.h>
#include <qml/walletopening.h>
#include <qml/walletsend.h>
#include <qml/models/signertransaction.h>
#include <qml/bitcoinunits.h>
#include <wallet/coincontrol.h>
#include <util/translation.h>
#include <QObject>
#include <algorithm>
#include <set>
#include <utility>

namespace walletbump {
namespace {
QString Message(const std::vector<bilingual_str>& errors)
{
    if (errors.empty()) return {};
    return QString::fromStdString(errors.front().translated.empty() ? errors.front().original : errors.front().translated).left(4096);
}
QString Fee(CAmount amount) { return QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, amount, false, QmlBitcoinUnits::SeparatorStyle::NEVER) + QStringLiteral(" BTC"); }
std::vector<COutPoint> Inputs(const CMutableTransaction& transaction)
{
    std::vector<COutPoint> inputs;
    inputs.reserve(transaction.vin.size());
    for (const auto& input : transaction.vin) inputs.push_back(input.prevout);
    return inputs;
}
std::vector<COutPoint> Inputs(const CTransaction& transaction)
{
    std::vector<COutPoint> inputs;
    inputs.reserve(transaction.vin.size());
    for (const auto& input : transaction.vin) inputs.push_back(input.prevout);
    return inputs;
}
Error CheckApprovedScope(interfaces::Wallet& wallet, const Prepared& review)
{
    if (!wallet.transactionCanBeBumped(review.original)) return Error::Ineligible;
    interfaces::WalletTxStatus status{};
    int blocks{0}; int64_t block_time{0};
    if (!wallet.tryGetTxStatus(review.original, status, blocks, block_time)) return Error::Unavailable;
    if (status.is_abandoned || status.depth_in_main_chain != 0) return Error::Ineligible;
    const auto original = wallet.getTx(review.original);
    if (!original || original->GetHash() != review.original
        || Inputs(*original) != review.original_inputs) return Error::Ineligible;

    const auto coins = wallet.getCoins(review.added_inputs);
    if (coins.size() != review.added_inputs.size()) return Error::Uncertain;
    for (const auto& coin : coins) {
        // Original inputs are intentionally already spent by the transaction
        // being replaced; only the newly introduced inputs are checked here.
        // Core's bump builder requires new inputs to be confirmed, so preserve
        // that public policy while rechecking spendability at commit time.
        if (coin.is_spent || coin.depth_in_main_chain < 1) return Error::Ineligible;
    }
    return Error::None;
}
void Observe(interfaces::Wallet& wallet, Commit& result)
{
    try {
        interfaces::WalletTxStatus status{}; bool in_mempool{false}; int num_blocks{0};
        std::vector<std::string> messages, payment_requests;
        const auto record = wallet.getWalletTxDetails(result.txid, status, messages, payment_requests, in_mempool, num_blocks);
        if (!record.tx || record.tx->GetHash() != result.txid) {
            result.record = Record::AbsentAtObservation;
            return;
        }
        result.record = Record::Recorded;
        if (status.depth_in_main_chain > 0) result.submission = Submission::Confirmed;
        else if (status.depth_in_main_chain < 0) result.submission = Submission::Conflicted;
        else if (status.is_abandoned) result.submission = Submission::Abandoned;
        else result.submission = in_mempool ? Submission::Mempool : Submission::NotInMempool;
    } catch (...) {
        result.record = Record::Unknown;
        result.submission = Submission::Unknown;
    }
}
} // namespace

Preparation Prepare(interfaces::Wallet& wallet, const QString& txid, unsigned int target,
                    const std::shared_ptr<walletopening::Session>& opening)
{
    Preparation result;
    try {
        const auto parsed = txid.size() == 64 ? uint256::FromHex(txid.toStdString()) : std::nullopt;
        if (!parsed) { result.error = Error::InvalidId; return result; }
        if (target < 1 || target > 1008) { result.error = Error::Invalid; return result; }
        const auto original = Txid::FromUint256(*parsed);
        if (!wallet.transactionCanBeBumped(original)) { result.error = Error::Ineligible; return result; }
        // A transaction review is backend data, potentially much larger than
        // its GUI fee strings. Reserve cleanup before constructing it so the
        // last facade/command release never tears transaction vectors down on GUI.
        struct Storage { std::unique_ptr<Prepared> value; };
        const auto storage = std::make_shared<Storage>();
        auto held = opening->holdValues([storage] { storage->value.reset(); });
        if (!held) { result.error = Error::Uncertain; return result; }
        const auto lease = std::make_shared<execution::ResourceLease>(std::move(*held.lease));
        storage->value = std::make_unique<Prepared>();
        auto review = std::shared_ptr<Prepared>{lease, storage->value.get()};
        review->original = original;
        review->target_blocks = target;
        const auto original_transaction = wallet.getTx(original);
        if (!original_transaction || original_transaction->GetHash().ToUint256() != *parsed) {
            result.error = Error::Ineligible; return result;
        }
        review->original_inputs = Inputs(*original_transaction);
        wallet::CCoinControl control;
        control.m_signal_bip125_rbf = true;
        control.m_confirm_target = target;
        // Preserve the original transaction's absolute and relative timing
        // through the public builder controls, before capturing a new review.
        control.m_version = original_transaction->version;
        control.m_locktime = original_transaction->nLockTime;
        for (const auto& input : original_transaction->vin)
            control.Select(input.prevout).SetSequence(input.nSequence);
        std::vector<bilingual_str> errors;
        if (!wallet.createBumpTransaction(original, control, errors, review->old_fee, review->new_fee, review->transaction)) {
            result.error = Error::Create; result.message = Message(errors); return result;
        }
        review->approved_inputs = Inputs(review->transaction);
        const std::set<COutPoint> original_inputs(review->original_inputs.begin(), review->original_inputs.end());
        const std::set<COutPoint> approved_inputs(review->approved_inputs.begin(), review->approved_inputs.end());
        if (original_inputs.size() != review->original_inputs.size()
            || approved_inputs.size() != review->approved_inputs.size()
            || !std::includes(approved_inputs.begin(), approved_inputs.end(), original_inputs.begin(), original_inputs.end())) {
            result.error = Error::Changed; return result;
        }
        for (const auto& input : review->approved_inputs) {
            if (!original_inputs.contains(input)) review->added_inputs.push_back(input);
        }
        if (!MoneyRange(review->old_fee) || !MoneyRange(review->new_fee) || review->new_fee < review->old_fee) {
            result.error = Error::Invalid; return result;
        }
        if (const auto scope_error = CheckApprovedScope(wallet, *review); scope_error != Error::None) {
            result.error = scope_error; return result;
        }
        review->old_fee_text = Fee(review->old_fee);
        review->new_fee_text = Fee(review->new_fee);
        review->increase_text = Fee(review->new_fee - review->old_fee);
        review->old_txid = QString::fromStdString(original.GetHex());
        result.review = std::move(review);
    } catch (...) { result.error = Error::Uncertain; }
    return result;
}

Commit Execute(interfaces::Wallet& wallet, const Prepared& review, std::optional<SecureString> passphrase,
               secretinput::Captured captured, const Services& services)
{
    Commit result;
    result.original = review.original;
    bool relock{false};
    const auto restore = [&] {
        if (!std::exchange(relock, false)) return;
        try { result.relock_failed = !wallet.lock(); } catch (...) { result.relock_failed = true; }
        if (result.relock_failed && !result.committed) result.error = Error::RelockFailed;
    };
    const auto finish = [&] { restore(); passphrase.reset(); return result; };
    try {
        if (captured) passphrase = secretinput::Materialize(captured);
        captured.reset();
        if (const auto scope_error = CheckApprovedScope(wallet, review); scope_error != Error::None) {
            result.error = scope_error; return finish();
        }
        if (!wallet.privateKeysDisabled() && wallet.isCrypted() && wallet.isLocked()) {
            if (!passphrase) { result.error = Error::NeedsUnlock; return finish(); }
            relock = true;
            if (!wallet.unlock(*passphrase)) { relock = false; result.error = Error::IncorrectPassphrase; return finish(); }
        }
        CMutableTransaction signed_tx{review.transaction};
        if (!wallet.signBumpTransaction(signed_tx)) { result.error = Error::Sign; return finish(); }
        if (!MatchesUnsignedTransaction(review.transaction, signed_tx)) { result.error = Error::Changed; return finish(); }
        if (wallet.hasExternalSigner() && !walletsend::VerifyExternalTransaction(wallet, CTransaction{signed_tx})) {
            result.error = Error::InvalidSignature; return finish();
        }
        if (const auto scope_error = CheckApprovedScope(wallet, review); scope_error != Error::None) {
            result.error = scope_error; return finish();
        }
        // No further key operation is needed. Restore authorization before
        // commit, and never publish a successful spend with failed restoration.
        restore(); passphrase.reset();
        if (result.relock_failed) return result;
        if (services.admission) {
            const auto exact = MakeTransactionRef(signed_tx);
            const auto decision = psbtpolicy::Evaluate(services.admission(exact, review.new_fee));
            if (decision.state == psbtpolicy::State::Blocked) {
                result.error = Error::Policy;
                result.message = decision.explanation;
                return result;
            }
        }
        std::vector<bilingual_str> errors;
        Txid txid;
        if (!wallet.commitBumpTransaction(review.original, std::move(signed_tx), errors, txid)) {
            result.error = Error::Commit; result.message = Message(errors); return result;
        }
        result.txid = txid;
        result.committed = true;
        Observe(wallet, result);
    } catch (const secretinput::Failure&) {
        result.error = Error::Invalid; result.message = QObject::tr("The wallet password could not be prepared. No fee bump was submitted.");
    } catch (...) { result.error = Error::Uncertain; }
    return finish();
}
} // namespace walletbump
