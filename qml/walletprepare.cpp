// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletprepare.h>
#include <qml/wallettransaction.h>
#include <qml/psbtwork.h>
#include <key_io.h>
#include <wallet/wallet.h> // Public CRecipient value required by interfaces::Wallet.

namespace walletprepare {
Result ExecuteInto(interfaces::Wallet& wallet, Request request, std::shared_ptr<Result> owner)
{
    if (!owner) return Result{.error = Error::Failed};
    *owner = Execute(wallet, std::move(request));
    Result result = *owner;
    result.lifetime = owner;
    if (result.review) result.review = std::shared_ptr<const walletreview::Snapshot>{owner, result.review.get()};
    return result;
}

Result Execute(interfaces::Wallet& wallet, Request request)
{
    Result result;
    bool relock{false};
    const auto finish = [&] {
        if (std::exchange(relock, false)) {
            try { result.relock_failed = !wallet.lock(); }
            catch (...) { result.relock_failed = true; }
            if (result.relock_failed) { result.error = Error::RelockFailed; result.review.reset(); }
        }
        request.passphrase.reset();
        return result;
    };
    try {
        if (request.captured_passphrase) {
            request.passphrase = secretinput::Materialize(request.captured_passphrase);
            request.captured_passphrase.reset();
        }
        if (request.draft) {
            if (!request.draft->all_valid) { result.error = Error::Invalid; return finish(); }
            const auto maximum = request.draft->positions.find(request.maximum_token);
            request.remainder = maximum == request.draft->positions.end() ? -1 : static_cast<int>(maximum->second);
            for (const auto& recipient : request.draft->recipients) {
                request.recipients.append({recipient.address, recipient.label, recipient.amount, recipient.payment_request});
            }
        }
        if (request.selected) for (const auto& outpoint : *request.selected) request.control.Select(outpoint);
        request.control.m_allow_other_inputs = !request.control.HasSelected();
        if (request.recipients.empty()) { result.error = Error::Invalid; return finish(); }
        std::vector<wallet::CRecipient> recipients;
        recipients.reserve(request.recipients.size());
        CAmount total{0};
        for (int i = 0; i < request.recipients.size(); ++i) {
            const auto& recipient = request.recipients[i];
            const auto destination = DecodeDestination(recipient.address.toStdString());
            if (!IsValidDestination(destination) || !MoneyRange(recipient.amount) ||
                (i != request.remainder && recipient.amount <= 0)) { result.error = Error::Invalid; return finish(); }
            if (!MoneyRange(total + recipient.amount)) { result.error = Error::Invalid; return finish(); }
            total += recipient.amount;
            recipients.push_back({destination, recipient.amount, false});
        }
        // A captured acknowledged balance is an early refusal, not authority
        // to spend. Core still validates the original inputs at construction.
        if (request.remainder < 0 && request.available_balance && total > *request.available_balance) {
            result.error = request.control.HasSelected() ? Error::InsufficientSelectedBalance : Error::InsufficientBalance;
            return finish();
        }
        const bool sign = !wallet.privateKeysDisabled();
        if (sign && wallet.isCrypted() && wallet.isLocked()) {
            if (!request.passphrase) { result.error = Error::NeedsUnlock; return finish(); }
            // An exception after a successful unlock must still attempt relock.
            relock = true;
            if (!wallet.unlock(*request.passphrase)) { relock = false; result.error = Error::IncorrectPassphrase; return finish(); }
        }
        request.passphrase.reset();
        const auto created = wallettransaction::Create(wallet, recipients, request.control, request.remainder, sign);
        if (!created || !created->tx) {
            result.error = Error::Core;
            if (!created) {
                const auto message = util::ErrorString(created);
                result.message = QString::fromStdString(message.translated.empty() ? message.original : message.translated).left(4096);
            }
            return finish();
        }
        auto review = std::make_shared<walletreview::Snapshot>();
        review->transaction = created->tx;
        review->fee = created->fee;
        review->proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Prepared(*created->tx, created->fee));
        if (review->proof->state != psbtfee::State::Known || !MoneyRange(created->fee)) {
            result.error = Error::Invalid; return finish();
        }
        review->can_send = sign || wallet.hasExternalSigner();
        const auto sweeps = created->change_pos ? std::optional<bool>{false}
            : wallettransaction::Sweeps(wallet, *created->tx, request.control);
        if (!sweeps) {
            result.error = Error::SweepUnknown;
            result.message = QStringLiteral("The wallet's remaining spendable balance could not be confirmed. Refresh the wallet and prepare again.");
            return finish();
        }
        review->sweeps = *sweeps;
        review->target_blocks = request.target_blocks;
        review->display_unit = request.display_unit;
        review->fee_rate = request.fee_rate;
        std::vector<bool> used(created->tx->vout.size(), false);
        if (created->change_pos && *created->change_pos < used.size()) used[*created->change_pos] = true;
        for (int row = 0; row < request.recipients.size(); ++row) {
            const auto& recipient = request.recipients[row];
            const auto script = GetScriptForDestination(recipients[row].dest);
            size_t output{0};
            for (; output < created->tx->vout.size(); ++output) {
                if (!used[output] && created->tx->vout[output].scriptPubKey == script &&
                    (row == request.remainder || created->tx->vout[output].nValue == recipient.amount)) break;
            }
            if (output == created->tx->vout.size()) { result.error = Error::Invalid; return finish(); }
            used[output] = true;
            const auto amount = created->tx->vout[output].nValue;
            if (!MoneyRange(amount) || !MoneyRange(review->amount + amount)) { result.error = Error::Invalid; return finish(); }
            review->amount += amount;
            review->recipients.append({recipient.address, recipient.label, amount, recipient.payment_request});
            review->destinations.insert(recipient.address);
            if (!recipient.label.trimmed().isEmpty()) review->notes.insert(recipient.address, recipient.label);
            if (row == request.remainder) review->maximum = amount;
        }
        review->presentation = walletreview::FormatRecipients(review->recipients, request.display_unit);
        review->flow = psbtwork::CollectReviewFlow(wallet, review->transaction, {}, review->proof, review->destinations, request.display_unit, review->notes);
        result.review = std::move(review);
    } catch (...) { result.error = Error::Failed; }
    return finish();
}
} // namespace walletprepare
