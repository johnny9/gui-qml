// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/bumpfeemodel.h>
#include <qml/wallet/feepolicy.h>
#include <qml/wallet/walletpassphrase.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/walletunlockcontext.h>
#include <qml/bitcoinunits.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <univalue.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <QPointer>
#include <algorithm>

namespace {
QString CoreErrors(const std::vector<bilingual_str>& errors)
{
    QStringList messages;
    for (const auto& error : errors) messages.push_back(QString::fromStdString(error.translated));
    return messages.join('\n');
}
}

BumpFeeModel::BumpFeeModel(WalletSession& session, interfaces::Node& node, QObject* parent)
    : QObject(parent), m_session(session), m_node(node), m_review(this), m_session_available(session.available())
{
    connect(&session, &WalletSession::invalidated, this, [this] { m_session_available = false; clear(); });
    connect(&session, &WalletSession::actionBusyChanged, this, &BumpFeeModel::changed);
    connect(&session, &WalletSession::activityChanged, this, &BumpFeeModel::checkEligibility);
    connect(&m_review, &TransactionReviewModel::changed, this, &BumpFeeModel::changed);
}

bool BumpFeeModel::busy() const { return m_pending || m_session.actionBusy(); }
bool BumpFeeModel::canSubmit() const
{
    const auto& snapshot = m_review.snapshot();
    return eligible() && !busy() && !m_recorded && m_candidate && snapshot &&
        snapshot->session_id == m_session.id() && snapshot->session_generation == m_session.generation() && snapshot->revision == m_revision &&
        snapshot->transaction->GetWitnessHash() == MakeTransactionRef(*m_candidate)->GetWitnessHash();
}

QString BumpFeeModel::feeIncrease() const
{
    const auto& snapshot = m_review.snapshot();
    if (!snapshot || !snapshot->previous_fee) return {};
    const auto unit = QmlBitcoinUnits::fromDisplayUnit(m_review.displayUnit());
    return QmlBitcoinUnits::format(unit, snapshot->fee - *snapshot->previous_fee) + " " + QmlBitcoinUnits::label(unit);
}

void BumpFeeModel::clear()
{
    ++m_revision;
    m_original.reset();
    m_candidate.reset();
    m_pending = m_eligible = m_recorded = m_accepted = false;
    m_error.clear();
    m_replacement_id.clear();
    m_review.clear();
    Q_EMIT changed();
}

void BumpFeeModel::discardReview()
{
    if (busy()) return;
    ++m_revision;
    m_candidate.reset();
    m_review.clear();
    Q_EMIT changed();
}

void BumpFeeModel::inspect(const QString& transaction_id)
{
    if (!m_session.available() || busy()) return;
    clear();
    m_original = Txid::FromHex(transaction_id.toStdString());
    if (!m_original) { m_error = tr("Invalid transaction ID."); Q_EMIT changed(); return; }
    checkEligibility();
}

void BumpFeeModel::checkEligibility()
{
    if (!m_original || !m_session.available() || busy() || m_recorded) return;
    const auto revision = m_revision;
    const auto eligible = std::make_shared<bool>(false);
    const QPointer<BumpFeeModel> self(this);
    m_pending = true;
    const bool queued = m_session.runRead([original = *m_original, eligible](interfaces::Wallet& wallet) {
        *eligible = !wallet.privateKeysDisabled() && !wallet.hasExternalSigner() && wallet.transactionCanBeBumped(original);
        return WalletOperationResult{};
    }, [self, revision, eligible](WalletOperationResult result) {
        if (!self || revision != self->m_revision) return;
        self->m_pending = false;
        self->m_eligible = result.code == WalletOperationResult::Success && *eligible;
        if (!self->m_eligible) {
            ++self->m_revision;
            self->m_candidate.reset();
            self->m_review.clear();
            self->m_error = self->tr("This transaction is no longer eligible for a fee increase.");
        }
        Q_EMIT self->changed();
    });
    if (!queued) m_pending = false;
    Q_EMIT changed();
}

void BumpFeeModel::prepare(const QString& custom_rate)
{
    if (!eligible() || busy() || !m_original || m_recorded) return;
    discardReview();
    std::optional<CAmount> rate;
    if (!custom_rate.isEmpty()) {
        rate = ParseCustomFeeRate(custom_rate);
        if (!rate) { m_error = tr("Enter a positive fee rate in sat/vB, with at most three decimals."); Q_EMIT changed(); return; }
    }
    ++m_revision;
    m_review.clear();
    m_candidate.reset();
    m_error.clear();
    m_pending = true;
    struct Result { CMutableTransaction transaction; CAmount old_fee{0}, new_fee{0}; std::vector<ReviewOutput> outputs; };
    const auto result = std::make_shared<Result>();
    const auto revision = m_revision;
    const QPointer<BumpFeeModel> self(this);
    const bool queued = m_session.runAction([original = *m_original, rate, result](interfaces::Wallet& wallet) {
        if (!wallet.transactionCanBeBumped(original)) return WalletOperationResult::failure(WalletOperationResult::Unavailable, tr("The original transaction changed. Reopen its details."));
        wallet::CCoinControl control;
        control.m_signal_bip125_rbf = true;
        control.m_confirm_target = 2;
        if (rate) control.m_feerate = CFeeRate(*rate);
        std::vector<bilingual_str> errors;
        if (!wallet.createBumpTransaction(original, control, errors, result->old_fee, result->new_fee, result->transaction))
            return WalletOperationResult::failure(WalletOperationResult::CoreError, CoreErrors(errors));
        for (const auto& output : result->transaction.vout) {
            CTxDestination destination;
            const QString address = ExtractDestination(output.scriptPubKey, destination) ? QString::fromStdString(EncodeDestination(destination)) : tr("Non-address output");
            result->outputs.push_back({address, output.nValue, false});
        }
        return WalletOperationResult{};
    }, [self, result, revision](WalletOperationResult operation) {
        if (!self || revision != self->m_revision) return;
        self->m_pending = false;
        self->m_error = operation.error;
        if (operation.code == WalletOperationResult::Success) {
            self->m_candidate = result->transaction;
            self->m_review.setSnapshot({self->m_session.id(), self->m_session.generation(), revision, MakeTransactionRef(result->transaction), result->new_fee, result->old_fee, result->outputs});
        }
        Q_EMIT self->changed();
    });
    if (!queued) { m_pending = false; m_error = tr("Another wallet action is running."); }
    Q_EMIT changed();
}

void BumpFeeModel::submit(const QString& passphrase)
{
    if (!canSubmit()) return;
    m_pending = true;
    m_error.clear();
    const auto revision = m_revision;
    struct Result { bool recorded{false}; QString transaction_id; };
    const auto result = std::make_shared<Result>();
    const QPointer<BumpFeeModel> self(this);
    const bool queued = m_session.runAction([original = *m_original, candidate = *m_candidate, secret = WalletPassphrase(passphrase), node = &m_node, result](interfaces::Wallet& wallet) mutable {
        if (!wallet.transactionCanBeBumped(original)) return WalletOperationResult::failure(WalletOperationResult::Unavailable, tr("The original transaction changed. Prepare a new fee increase."));
        const auto original_tx = wallet.getTx(original);
        if (!original_tx) return WalletOperationResult::failure(WalletOperationResult::Unavailable, tr("Original transaction unavailable."));
        // Original inputs are spent by the transaction being replaced. Any
        // additional input selected by Core must still be unspent.
        for (const auto& input : candidate.vin) {
            const bool original_input = std::ranges::any_of(original_tx->vin, [&](const auto& old) { return old.prevout == input.prevout; });
            if (!original_input) {
                if (wallet.isLockedCoin(input.prevout)) return WalletOperationResult::failure(WalletOperationResult::Unavailable, tr("A replacement input was locked. Prepare again."));
                UniValue args(UniValue::VARR);
                args.push_back(input.prevout.hash.GetHex());
                args.push_back(input.prevout.n);
                args.push_back(true);
                if (node->executeRpc("gettxout", args, "").isNull()) return WalletOperationResult::failure(WalletOperationResult::Unavailable, tr("Replacement inputs changed. Prepare again."));
            }
        }
        WalletUnlockContext unlock(wallet, secret);
        if (!unlock.valid()) return WalletOperationResult::failure(WalletOperationResult::InvalidInput, unlock.error());
        if (!wallet.signBumpTransaction(candidate)) return WalletOperationResult::failure(WalletOperationResult::CoreError, tr("Core could not sign the replacement."));
        std::vector<bilingual_str> errors;
        Txid replacement;
        if (!wallet.commitBumpTransaction(original, std::move(candidate), errors, replacement))
            return WalletOperationResult::failure(WalletOperationResult::CoreError, CoreErrors(errors));
        result->recorded = true;
        result->transaction_id = QString::fromStdString(replacement.GetHex());
        // Core owns ordinary wallet relay policy, including -walletbroadcast=0
        // and -blocksonly. A successful commit only guarantees a wallet record.
        interfaces::WalletTxStatus status{};
        std::vector<std::string> messages, requests;
        bool in_mempool{false};
        int blocks{0};
        wallet.getWalletTxDetails(replacement, status, messages, requests, in_mempool, blocks);
        if (!in_mempool && status.depth_in_main_chain <= 0)
            return WalletOperationResult::failure(WalletOperationResult::CoreError, tr("Replacement recorded in the wallet but not accepted into the mempool. Wallet broadcast policy remains unchanged."));
        return WalletOperationResult{};
    }, [self, revision, result](WalletOperationResult operation) {
        if (!self || revision != self->m_revision) return;
        self->m_pending = false;
        self->m_error = operation.error;
        self->m_recorded = result->recorded;
        self->m_replacement_id = result->transaction_id;
        self->m_accepted = operation.code == WalletOperationResult::Success;
        if (self->m_recorded || operation.code == WalletOperationResult::Unavailable) {
            ++self->m_revision;
            self->m_candidate.reset();
            self->m_review.clear();
            self->m_eligible = false;
        }
        Q_EMIT self->changed();
    });
    if (!queued) { m_pending = false; m_error = tr("Another wallet action is running."); }
    Q_EMIT changed();
}
