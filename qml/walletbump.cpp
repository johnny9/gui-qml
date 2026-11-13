// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletbump.h>
#include <qml/walletopening.h>
#include <qml/models/signertransaction.h>
#include <qml/bitcoinunits.h>
#include <wallet/coincontrol.h>
#include <util/translation.h>
#include <QObject>
#include <utility>

namespace walletbump {
namespace {
QString Message(const std::vector<bilingual_str>& errors)
{
    if (errors.empty()) return {};
    return QString::fromStdString(errors.front().translated.empty() ? errors.front().original : errors.front().translated).left(4096);
}
QString Fee(CAmount amount) { return QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, amount, false, QmlBitcoinUnits::SeparatorStyle::NEVER) + QStringLiteral(" BTC"); }
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
        wallet::CCoinControl control;
        control.m_signal_bip125_rbf = true;
        control.m_confirm_target = target;
        std::vector<bilingual_str> errors;
        if (!wallet.createBumpTransaction(original, control, errors, review->old_fee, review->new_fee, review->transaction)) {
            result.error = Error::Create; result.message = Message(errors); return result;
        }
        if (!MoneyRange(review->old_fee) || !MoneyRange(review->new_fee) || review->new_fee < review->old_fee) {
            result.error = Error::Invalid; return result;
        }
        review->old_fee_text = Fee(review->old_fee);
        review->new_fee_text = Fee(review->new_fee);
        review->increase_text = Fee(review->new_fee - review->old_fee);
        review->old_txid = QString::fromStdString(original.GetHex());
        result.review = std::move(review);
    } catch (...) { result.error = Error::Uncertain; }
    return result;
}

Commit Execute(interfaces::Wallet& wallet, const Prepared& review, std::optional<SecureString> passphrase, secretinput::Captured captured)
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
        if (!wallet.transactionCanBeBumped(review.original)) { result.error = Error::Ineligible; return finish(); }
        if (!wallet.privateKeysDisabled() && wallet.isCrypted() && wallet.isLocked()) {
            if (!passphrase) { result.error = Error::NeedsUnlock; return finish(); }
            relock = true;
            if (!wallet.unlock(*passphrase)) { relock = false; result.error = Error::IncorrectPassphrase; return finish(); }
        }
        CMutableTransaction signed_tx{review.transaction};
        if (!wallet.signBumpTransaction(signed_tx)) { result.error = Error::Sign; return finish(); }
        if (!MatchesUnsignedTransaction(review.transaction, signed_tx)) { result.error = Error::Changed; return finish(); }
        if (!wallet.transactionCanBeBumped(review.original)) { result.error = Error::Ineligible; return finish(); }
        // No further key operation is needed. Restore authorization before
        // commit, and never publish a successful spend with failed restoration.
        restore(); passphrase.reset();
        if (result.relock_failed) return result;
        std::vector<bilingual_str> errors;
        Txid txid;
        if (!wallet.commitBumpTransaction(review.original, std::move(signed_tx), errors, txid)) {
            result.error = Error::Commit; result.message = Message(errors); return result;
        }
        result.txid = txid;
        result.committed = true;
    } catch (const secretinput::Failure&) {
        result.error = Error::Invalid; result.message = QObject::tr("The wallet password could not be prepared. No fee bump was submitted.");
    } catch (...) { result.error = Error::Uncertain; }
    return finish();
}
} // namespace walletbump
