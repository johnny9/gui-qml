
// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletqmlmodel.h>
#include <qml/backendexecutor.h>

#include <common/messages.h>
#include <qml/bitcoinamount.h>
#include <qml/models/addresslistmodel.h>
#include <qml/models/paymentrequest.h>
#include <qml/models/psbtqmlmodel.h>
#include <qml/models/receiverequestentry.h>
#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/signverifymessagemodel.h>
#include <qml/models/walletunlock.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <qml/models/transactionflow.h>
#include <qml/util.h>

#include <chainparams.h>
#include <common/types.h>
#include <consensus/amount.h>
#include <interfaces/chain.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <node/psbt.h>
#include <node/transaction.h>
#include <node/types.h>
#include <addresstype.h>
#include <outputtype.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <policy/truc_policy.h>
#include <primitives/transaction.h>
#include <psbt.h>
#include <qml/bitcoinunits.h>
#include <script/solver.h>
#include <support/allocators/secure.h>
#include <univalue.h>
#include <util/result.h>
#include <util/rbf.h>
#include <util/threadnames.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/fees.h>
#include <wallet/spend.h>
#include <wallet/wallet.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSettings>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <tuple>
#include <vector>

namespace {
constexpr unsigned int DEFAULT_STANDARD_FEE_TARGET{6};
constexpr int FEE_ESTIMATE_DEBOUNCE_MS{250};
constexpr unsigned int FEE_RATE_BASIS_VBYTES{1000};
constexpr std::array<unsigned int, 3> STANDARD_FEE_TARGETS{2, DEFAULT_STANDARD_FEE_TARGET, 10};
constexpr std::array<unsigned int, 7> CUSTOM_FEE_TARGETS{2, 3, 4, 6, 10, 25, 50};
const QRegularExpression CUSTOM_FEE_RATE_PATTERN{QStringLiteral(R"(^[0-9]+(?:\.[0-9]{0,3})?$)")};

// These values cross the worker boundary; they never retain a GUI model.
struct TransactionResult {
    CTransactionRef tx;
    std::optional<PartiallySignedTransaction> psbt;
    QVariantMap flow;
    CAmount fee{0};
    std::optional<unsigned int> change_pos;
    bool sweeps{false};
    bool needs_unlock{false};
    bool signer_not_found{false};
    QString error;
};

bool UnlockTransaction(interfaces::Wallet& wallet, std::optional<SecureString>& passphrase,
                       TransactionResult& result, bool& relock, const QString& prompt)
{
    relock = false;
    if (!wallet.privateKeysDisabled() && wallet.isCrypted() && wallet.isLocked() && !passphrase) {
        result.error = prompt;
        result.needs_unlock = true;
        return false;
    }
    if (!passphrase) return true;
    const auto unlocked = TryUnlockWithPassphrase(wallet, *passphrase);
    passphrase.reset();
    if (unlocked == WalletUnlockResult::IncorrectPassphrase) {
        result.error = WalletQmlModel::tr("The wallet password you entered was incorrect.");
        result.needs_unlock = true;
        return false;
    }
    relock = unlocked == WalletUnlockResult::UnlockedNowRelockRequired;
    return true;
}

QString ReadAddressLabel(interfaces::Wallet& wallet, const QString& address)
{
    std::string label;
    wallet.getAddress(DecodeDestination(address.toStdString()), &label, nullptr);
    return QString::fromStdString(label);
}

QVariantMap ReadTransactionFlow(interfaces::Wallet& wallet, const CTransactionRef& tx,
                               const PartiallySignedTransaction* psbt, const QSet<QString>& recipients)
{
    if (!tx) return {};
    interfaces::WalletTx preview{};
    preview.tx = tx;
    std::vector<std::optional<CTxOut>> prevouts;
    for (size_t i = 0; i < tx->vin.size(); ++i) {
        const auto& input = tx->vin[i];
        preview.txin_is_mine.push_back(wallet.txinIsMine(input));
        preview.debit += wallet.getDebit(input);
        const auto parent = wallet.getTx(input.prevout.hash);
        if (parent && input.prevout.n < parent->vout.size()) {
            prevouts.emplace_back(parent->vout[input.prevout.n]);
        } else if (psbt && i < psbt->inputs.size() && !psbt->inputs[i].witness_utxo.IsNull()) {
            prevouts.emplace_back(psbt->inputs[i].witness_utxo);
        } else if (psbt && i < psbt->inputs.size() && psbt->inputs[i].non_witness_utxo
                   && input.prevout.n < psbt->inputs[i].non_witness_utxo->vout.size()) {
            prevouts.emplace_back(psbt->inputs[i].non_witness_utxo->vout[input.prevout.n]);
        } else {
            prevouts.emplace_back(std::nullopt);
        }
    }
    for (const auto& output : tx->vout) {
        const bool owned = wallet.txoutIsMine(output);
        preview.txout_is_mine.push_back(owned);
        CTxDestination destination;
        const QString address = ExtractDestination(output.scriptPubKey, destination)
            ? QString::fromStdString(EncodeDestination(destination)) : QString{};
        preview.txout_is_change.push_back(owned && !recipients.contains(address));
    }
    QVariantMap flow = BuildTransactionFlow(preview, prevouts);
    QVariantList inputs = flow.value(QStringLiteral("inputs")).toList();
    for (auto& value : inputs) {
        auto entry = value.toMap();
        if (entry.value(QStringLiteral("ownership")).toString() == QStringLiteral("wallet")) {
            const auto label = ReadAddressLabel(wallet, entry.value(QStringLiteral("address")).toString());
            if (!label.isEmpty()) entry.insert(QStringLiteral("label"), label);
        }
        value = entry;
    }
    flow.insert(QStringLiteral("inputs"), inputs);
    return flow;
}

int FallbackFeeMultiplier(const unsigned int target)
{
    for (size_t i = 0; i < STANDARD_FEE_TARGETS.size(); ++i) {
        if (STANDARD_FEE_TARGETS[i] == target) {
            return static_cast<int>(STANDARD_FEE_TARGETS.size() - i);
        }
    }

    return 1;
}

QString FormatFeeEstimate(CAmount amount)
{
    BitcoinAmount bitcoin_amount;
    bitcoin_amount.setSatoshi(amount);
    return bitcoin_amount.displayWithUnit();
}

bool AmountPlusFeeExceedsBalance(const CAmount amount, const CAmount fee, const CAmount balance)
{
    if (amount > balance) {
        return true;
    }
    return fee > balance - amount;
}

void ClearTransactionInputScripts(CMutableTransaction& tx)
{
    for (CTxIn& input : tx.vin) {
        input.scriptSig.clear();
        input.scriptWitness.SetNull();
    }
}

void ApplySelectedInputsPolicy(wallet::CCoinControl& coin_control)
{
    // Manually selected coins should be the only wallet inputs used.
    coin_control.m_allow_other_inputs = !coin_control.HasSelected();
}

std::optional<CAmount> ParseCustomFeeRatePerKvB(const QString& custom_fee_rate)
{
    const QString trimmed = custom_fee_rate.trimmed();
    if (trimmed.isEmpty() || !CUSTOM_FEE_RATE_PATTERN.match(trimmed).hasMatch()) {
        return std::nullopt;
    }

    const QStringList parts = trimmed.split('.');
    bool whole_ok{false};
    const CAmount whole_part{parts.at(0).toLongLong(&whole_ok)};
    if (!whole_ok) {
        return std::nullopt;
    }

    QString fractional_part = parts.size() == 2 ? parts.at(1) : QString{};
    while (fractional_part.size() < 3) {
        fractional_part += QLatin1Char{'0'};
    }

    bool fractional_ok{false};
    const CAmount fractional_value{
        fractional_part.isEmpty() ? 0 : fractional_part.toLongLong(&fractional_ok)};
    if (!fractional_part.isEmpty() && !fractional_ok) {
        return std::nullopt;
    }

    const CAmount fee_rate_per_kvb = (whole_part * FEE_RATE_BASIS_VBYTES) + fractional_value;
    if (fee_rate_per_kvb <= 0) {
        return std::nullopt;
    }

    return fee_rate_per_kvb;
}

std::optional<CTxDestination> PreviewChangeDestination(const OutputType change_type)
{
    const uint160 dummy_key_hash{};
    switch (change_type) {
    case OutputType::BECH32M:
        return WitnessV1Taproot{XOnlyPubKey::NUMS_H};
    case OutputType::BECH32:
        return WitnessV0KeyHash{dummy_key_hash};
    case OutputType::P2SH_SEGWIT:
        return ScriptHash{GetScriptForDestination(WitnessV0KeyHash{dummy_key_hash})};
    case OutputType::LEGACY:
        return PKHash{dummy_key_hash};
    case OutputType::UNKNOWN:
        return std::nullopt;
    }
    return std::nullopt;
}

void ApplyPreviewChangeDestination(wallet::CCoinControl& coin_control, const OutputType change_type)
{
    if (const auto destination = PreviewChangeDestination(change_type)) {
        coin_control.destChange = *destination;
    }
}

void ApplyRegtestStaticFeeOverride(wallet::CCoinControl& coin_control)
{
    if (Params().GetChainType() != ChainType::REGTEST) {
        return;
    }

    // Regtest commonly runs without fee estimation, so use a fixed static fee
    // rate instead of target-based estimation.
    coin_control.m_confirm_target.reset();
    coin_control.m_feerate = CFeeRate{wallet::DEFAULT_TRANSACTION_MINFEE};
}

// Use the same available input set for automatic sendall and sweep detection.
// Clear manual selections because AvailableCoins otherwise omits those outputs.
std::vector<wallet::COutput> AvailableSendAllCoins(const wallet::CWallet& wallet, wallet::CCoinControl control)
    EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    control.UnSelectAll();
    wallet::CoinFilterParams filter;
    filter.min_amount = 0;
    filter.skip_locked = true;
    return wallet::AvailableCoins(wallet, &control, std::nullopt, filter).All();
}

bool TransactionSweepsAvailableFunds(interfaces::Wallet& wallet_interface, const CTransaction& tx, const wallet::CCoinControl& control)
{
    auto* wallet = wallet_interface.wallet();
    if (!wallet) return false;
    LOCK(wallet->cs_wallet);
    // Selecting every coin is not a sweep if funds return to the wallet.
    for (const auto& output : tx.vout) {
        if (wallet->IsMine(output)) return false;
    }
    const auto available = AvailableSendAllCoins(*wallet, control);
    return !available.empty() && std::all_of(available.begin(), available.end(), [&tx](const auto& coin) {
        return std::any_of(tx.vin.begin(), tx.vin.end(), [&coin](const auto& input) {
            return input.prevout == coin.outpoint;
        });
    });
}

// Build the no-change transaction using the same rules as Core's sendall RPC.
// Calling the RPC itself would also sign (including invoking external signers),
// which must not happen while the user is editing the form or previewing fees.
util::Result<wallet::CreatedTransactionResult> CreateSendAllTransaction(
    interfaces::Wallet& wallet_interface,
    const std::vector<wallet::CRecipient>& recipients,
    const wallet::CCoinControl& control, int remainder_index, bool sign)
{
    auto* wallet = wallet_interface.wallet();
    if (!wallet || remainder_index < 0 || static_cast<size_t>(remainder_index) >= recipients.size()) {
        return util::Error{Untranslated("Unable to prepare a send-all transaction.")};
    }
    LOCK(wallet->cs_wallet);
    FeeCalculation fee_calc;
    const CFeeRate rate = wallet::GetMinimumFeeRate(*wallet, control, &fee_calc);
    if (control.m_feerate && rate > *control.m_feerate) {
        return util::Error{Untranslated("Fee rate is below the minimum fee rate.")};
    }
    if (fee_calc.reason == FeeReason::FALLBACK && !wallet->m_allow_fallback_fee) {
        return util::Error{Untranslated("Fee estimation failed. Fallbackfee is disabled.")};
    }
    // Explicit selections restrict sendall; otherwise use all available coins
    // without changing the form's coin-control selection.
    auto inputs = control.ListSelected();
    if (inputs.empty()) {
        for (const auto& coin : AvailableSendAllCoins(*wallet, control)) {
            inputs.push_back(coin.outpoint);
        }
    }
    if (inputs.empty()) return util::Error{Untranslated("No available inputs to use maximum.")};
    CMutableTransaction tx;
    tx.version = control.m_version;
    CAmount total{0};
    for (const auto& input : inputs) {
        const auto* source = wallet->GetWalletTx(input.hash);
        if (!source || input.n >= source->tx->vout.size() || wallet->IsSpent(input)
            || !wallet->IsMine(source->tx->vout[input.n])) {
            return util::Error{Untranslated("A selected input is no longer available.")};
        }
        if (wallet->GetTxDepthInMainChain(*source) == 0 && source->tx->version == TRUC_VERSION) {
            return util::Error{Untranslated("Cannot spend an unconfirmed version 3 input in this transaction.")};
        }
        total += source->tx->vout[input.n].nValue;
        const uint32_t sequence = wallet->m_signal_rbf ? MAX_BIP125_RBF_SEQUENCE : CTxIn::MAX_SEQUENCE_NONFINAL;
        tx.vin.emplace_back(input, CScript{}, sequence);
    }
    CAmount fixed_amount{0};
    std::set<CTxDestination> destinations;
    for (size_t i = 0; i < recipients.size(); ++i) {
        const auto& recipient = recipients[i];
        if (!destinations.insert(recipient.dest).second) {
            return util::Error{Untranslated("Each recipient must have a different address when using maximum.")};
        }
        const CAmount amount = static_cast<int>(i) == remainder_index ? 0 : recipient.nAmount;
        fixed_amount += amount;
        tx.vout.emplace_back(amount, GetScriptForDestination(recipient.dest));
    }
    const auto size = wallet::CalculateMaximumSignedTxSize(CTransaction(tx), wallet);
    if (size.vsize < 0) return util::Error{Untranslated("Unable to determine the transaction size.")};
    if (size.weight > MAX_STANDARD_TX_WEIGHT) return util::Error{Untranslated("Transaction too large.")};
    const CAmount fee = rate.GetFee(size.vsize)
        + wallet->chain().calculateCombinedBumpFee(inputs, rate).value_or(0);
    if (fee > wallet->m_default_max_tx_fee) return util::Error{Untranslated("Transaction fee exceeds the maximum configured fee.")};
    if (total <= fee || fixed_amount > total - fee) {
        return util::Error{Untranslated("Selected inputs do not cover the amount plus fee")};
    }
    tx.vout[remainder_index].nValue = total - fee - fixed_amount;
    for (const auto& output : tx.vout) {
        if (IsDust(output, wallet->chain().relayDustFee())) {
            return util::Error{Untranslated("The amount after fees is below the dust threshold.")};
        }
    }
    FastRandomContext rng;
    wallet::DiscourageFeeSniping(tx, rng, wallet->chain(), wallet->GetLastBlockHash(), wallet->GetLastBlockHeight());
    if (sign && !wallet->SignTransaction(tx)) return util::Error{Untranslated("Signing transaction failed.")};
    return wallet::CreatedTransactionResult{MakeTransactionRef(std::move(tx)), fee, std::nullopt, fee_calc};
}

// A failed fixed-amount preview can still report the fee needed to spend
// selected inputs when even a transaction without change is unaffordable.
std::optional<SendFeePreview> EstimateInsufficientSelectedInputsFee(
    interfaces::Wallet& wallet_interface, const std::vector<wallet::CRecipient>& recipients,
    const wallet::CCoinControl& control)
{
    auto* wallet = wallet_interface.wallet();
    if (!wallet || !control.HasSelected() || control.m_allow_other_inputs) return std::nullopt;

    LOCK(wallet->cs_wallet);
    FeeCalculation fee_calc;
    const CFeeRate rate = wallet::GetMinimumFeeRate(*wallet, control, &fee_calc);
    if (rate.GetFeePerK() <= 0 || (control.m_feerate && rate > *control.m_feerate)
        || (fee_calc.reason == FeeReason::FALLBACK && !wallet->m_allow_fallback_fee)) {
        return std::nullopt;
    }

    CMutableTransaction tx;
    tx.version = control.m_version;
    CAmount input_amount{0};
    const auto inputs = control.ListSelected();
    for (const auto& input : inputs) {
        const auto* source = wallet->GetWalletTx(input.hash);
        if (!source || input.n >= source->tx->vout.size() || wallet->IsSpent(input)
            || !wallet->IsMine(source->tx->vout[input.n])) {
            return std::nullopt;
        }
        input_amount += source->tx->vout[input.n].nValue;
        tx.vin.emplace_back(input);
    }

    CAmount send_amount{0};
    for (const auto& recipient : recipients) {
        send_amount += recipient.nAmount;
        tx.vout.emplace_back(recipient.nAmount, GetScriptForDestination(recipient.dest));
    }
    const auto size = wallet::CalculateMaximumSignedTxSize(CTransaction(tx), wallet, &control);
    if (size.vsize < 0) return std::nullopt;
    const CAmount fee = rate.GetFee(size.vsize)
        + wallet->chain().calculateCombinedBumpFee(inputs, rate).value_or(0);
    if (!AmountPlusFeeExceedsBalance(send_amount, fee, input_amount)) return std::nullopt;
    return SendFeePreview{fee, static_cast<int>(inputs.size()), rate.GetFeePerK(), std::nullopt, false};
}

std::optional<SendFeePreview> TryPreviewFee(interfaces::Wallet& wallet,
                                     const std::vector<wallet::CRecipient>& recipients,
                                     const wallet::CCoinControl& coin_control, int remainder_index = -1)
{
    const auto result = remainder_index >= 0
        ? CreateSendAllTransaction(wallet, recipients, coin_control, remainder_index, /*sign=*/false)
        : wallet.createTransaction(recipients, coin_control, /*sign=*/false, /*change_pos=*/std::nullopt);
    if (!result) {
        return remainder_index < 0
            ? EstimateInsufficientSelectedInputsFee(wallet, recipients, coin_control)
            : std::nullopt;
    }

    const CAmount rate = coin_control.m_feerate
        ? coin_control.m_feerate->GetFeePerK()
        : wallet.getMinimumFee(FEE_RATE_BASIS_VBYTES, coin_control, nullptr, nullptr);
    return SendFeePreview{result->fee, result->tx ? static_cast<int>(result->tx->vin.size()) : 0, rate,
        remainder_index >= 0 ? std::make_optional(result->tx->vout[remainder_index].nValue) : std::nullopt,
        result->tx && !result->change_pos && TransactionSweepsAvailableFunds(wallet, *result->tx, coin_control)};
}

std::optional<SendFeePreview> EstimatePreviewFee(interfaces::Wallet& wallet,
                                          const std::vector<wallet::CRecipient>& recipients,
                                          const wallet::CCoinControl& base_coin_control,
                                          const OutputType preview_change_type,
                                          const unsigned int target, int remainder_index = -1)
{
    wallet::CCoinControl coin_control{base_coin_control};
    ApplySelectedInputsPolicy(coin_control);
    coin_control.m_feerate.reset();
    coin_control.m_confirm_target = target;
    ApplyPreviewChangeDestination(coin_control, preview_change_type);
    ApplyRegtestStaticFeeOverride(coin_control);

    if (const auto fee = TryPreviewFee(wallet, recipients, coin_control, remainder_index)) {
        return fee;
    }

    // A maximum amount must use the same fee policy as final construction.
    // Do not fill an amount using a made-up rate if sendall cannot be estimated.
    if (remainder_index >= 0 || Params().GetChainType() == ChainType::REGTEST) {
        return std::nullopt;
    }

    const CAmount required_fee_per_k = wallet.getRequiredFee(FEE_RATE_BASIS_VBYTES);
    if (required_fee_per_k <= 0) {
        return std::nullopt;
    }

    wallet::CCoinControl fallback_coin_control{coin_control};
    fallback_coin_control.m_confirm_target.reset();
    // Keep fallback previews distinct across presets even when the backend can
    // only provide a minimum required feerate.
    fallback_coin_control.m_feerate = CFeeRate{required_fee_per_k * FallbackFeeMultiplier(target)};

    if (const auto fee = TryPreviewFee(wallet, recipients, fallback_coin_control, remainder_index)) {
        return fee;
    }

    return std::nullopt;
}

std::optional<SendFeePreview> EstimateCustomPreviewFee(interfaces::Wallet& wallet,
                                                const std::vector<wallet::CRecipient>& recipients,
                                                const wallet::CCoinControl& base_coin_control,
                                                const OutputType preview_change_type,
                                                const CAmount fee_rate_per_kvb, int remainder_index = -1)
{
    wallet::CCoinControl coin_control{base_coin_control};
    ApplySelectedInputsPolicy(coin_control);
    coin_control.m_confirm_target.reset();
    coin_control.m_feerate = CFeeRate{fee_rate_per_kvb};
    ApplyPreviewChangeDestination(coin_control, preview_change_type);

    if (const auto fee = TryPreviewFee(wallet, recipients, coin_control, remainder_index)) {
        return fee;
    }

    return std::nullopt;
}

std::optional<std::vector<wallet::CRecipient>> BuildRecipients(const SendRecipientsListModel& recipients, const SendRecipient* maximum_recipient = nullptr)
{
    std::vector<wallet::CRecipient> vec_send;
    vec_send.reserve(recipients.recipients().size());

    for (auto* recipient : recipients.recipients()) {
        if (recipient == nullptr || (recipient != maximum_recipient && !recipient->isValid())) {
            return std::nullopt;
        }

        const CTxDestination destination = DecodeDestination(recipient->address()->address().toStdString());
        if (!IsValidDestination(destination)) {
            return std::nullopt;
        }

        vec_send.push_back({destination, recipient->cAmount(), false});
    }

    if (vec_send.empty()) {
        return std::nullopt;
    }

    return vec_send;
}

QString LocalizedString(const bilingual_str& value)
{
    return QString::fromStdString(value.translated.empty() ? value.original : value.translated);
}

QString OutputTypeId(OutputType type)
{
    return QString::fromStdString(FormatOutputType(type));
}

QString OutputTypeIdFromDestination(const CTxDestination& destination)
{
    if (std::get_if<PKHash>(&destination)) return OutputTypeId(OutputType::LEGACY);
    if (std::get_if<ScriptHash>(&destination)) return OutputTypeId(OutputType::P2SH_SEGWIT);
    if (std::get_if<WitnessV0KeyHash>(&destination) || std::get_if<WitnessV0ScriptHash>(&destination)) return OutputTypeId(OutputType::BECH32);
    if (std::get_if<WitnessV1Taproot>(&destination)) return OutputTypeId(OutputType::BECH32M);
    return {};
}

QString OutputTypeLabel(OutputType type)
{
    switch (type) {
    case OutputType::BECH32M:
        return QObject::tr("Bech32m (Taproot)");
    case OutputType::BECH32:
        return QObject::tr("Bech32 (SegWit)");
    case OutputType::P2SH_SEGWIT:
        return QObject::tr("Base58 (P2SH-SegWit)");
    case OutputType::LEGACY:
        return QObject::tr("Base58 (Legacy)");
    case OutputType::UNKNOWN:
        return {};
    }
    return {};
}

QString OutputTypeDescription(OutputType type)
{
    switch (type) {
    case OutputType::BECH32M:
        return QObject::tr("Lower fees · Better privacy");
    case OutputType::BECH32:
        return QObject::tr("Lower fees · Widely supported");
    case OutputType::P2SH_SEGWIT:
        return QObject::tr("Higher fees · Backward compatible");
    case OutputType::LEGACY:
        return QObject::tr("Higher fees · Not recommended");
    case OutputType::UNKNOWN:
        return {};
    }
    return {};
}

struct ImportedRecipient {
    QString address;
    QString label;
    CAmount amount{0};
};
struct ImportedTransactionResult {
    QString error;
    QString known_txid;
    QString message;
    CTransactionRef tx;
    std::optional<PartiallySignedTransaction> psbt;
    QVariantMap flow;
    CAmount fee{0};
    std::vector<ImportedRecipient> recipients;
    bool can_send{false};
    bool can_broadcast{false};
};

ImportedTransactionResult ReadImportedTransaction(interfaces::Wallet* wallet, const QString& path)
{
    ImportedTransactionResult out;
    PartiallySignedTransaction psbt{CMutableTransaction{}};
    out.error = PsbtQmlModel::LoadPsbtFromFile(path, psbt);
    if (!out.error.isEmpty()) return out;
    if (wallet && psbt.GetUnsignedTx()) {
        interfaces::WalletTxStatus status;
        int blocks{0};
        int64_t block_time{0};
        const auto txid = psbt.GetUnsignedTx()->GetHash();
        if (wallet->tryGetTxStatus(txid, status, blocks, block_time)) {
            out.known_txid = QString::fromStdString(txid.GetHex());
            return out;
        }
    }

    if (!wallet) {
        out.error = WalletQmlModel::tr("No wallet is loaded.");
        return out;
    }
    const auto unsigned_tx{psbt.GetUnsignedTx()};
    if (!unsigned_tx) {
        out.error = WalletQmlModel::tr("The PSBT does not contain an unsigned transaction.");
        return out;
    }
    if (unsigned_tx->vin.empty() || unsigned_tx->vout.empty()) {
        out.error = WalletQmlModel::tr("The PSBT has no inputs or outputs.");
        return out;
    }
    if (psbt.inputs.size() != unsigned_tx->vin.size() || psbt.outputs.size() != unsigned_tx->vout.size()) {
        out.error = WalletQmlModel::tr("The PSBT is malformed.");
        return out;
    }

    const bool spends_only_wallet_inputs{std::all_of(unsigned_tx->vin.begin(), unsigned_tx->vin.end(), [&wallet](const CTxIn& input) {
        return wallet->txinIsMine(input);
    })};

    PartiallySignedTransaction analysis_psbt{psbt};
    bool complete{FinalizePSBT(analysis_psbt)};
    size_t could_sign{0};
    const std::optional<common::PSBTError> fill_error{
        wallet->fillPSBT({.sign = false, .bip32_derivs = false}, &could_sign, analysis_psbt, complete)};
    if (fill_error) {
        out.error = PsbtQmlModel::PsbtErrorText(*fill_error);
        return out;
    }
    complete = FinalizePSBT(analysis_psbt);
    const auto analysis_tx{analysis_psbt.GetUnsignedTx()};
    if (!analysis_tx) {
        out.error = WalletQmlModel::tr("The PSBT does not contain an unsigned transaction.");
        return out;
    }
    std::optional<std::pair<int, int>> multisig_sig_info;
    if (!complete) {
        for (size_t i{0}; i < analysis_psbt.inputs.size(); ++i) {
            if (auto info{PsbtQmlModel::MultisigPsbtInputSigInfo(analysis_psbt, i)}) {
                multisig_sig_info = info;
                break;
            }
        }
    }

    const node::PSBTAnalysis analysis{node::AnalyzePSBT(analysis_psbt)};
    const bool fee_is_known{analysis.fee && *analysis.fee >= 0};
    const size_t unsigned_inputs{CountPSBTUnsignedInputs(analysis_psbt)};
    const bool wallet_has_signer{!wallet->privateKeysDisabled() || wallet->hasExternalSigner()};
    const bool can_send{fee_is_known && !multisig_sig_info && spends_only_wallet_inputs && (complete || (wallet_has_signer && unsigned_inputs > 0 && could_sign >= unsigned_inputs))};
    const bool can_broadcast{fee_is_known && complete};
    const QString review_message{
        !fee_is_known
            ? WalletQmlModel::tr("The transaction fee is missing or invalid. Add valid input information before broadcasting.")
            : multisig_sig_info
                ? WalletQmlModel::tr("This transaction requires %1 of %2 signatures.").arg(multisig_sig_info->first).arg(multisig_sig_info->second)
                : can_send || can_broadcast
                    ? QString{}
                    : WalletQmlModel::tr("This wallet does not have the keys to sign this transaction.")};

    std::vector<ImportedRecipient> draft_recipients;
    CAmount recipient_total{0};
    for (const CTxOut& output : analysis_tx->vout) {
        CTxDestination destination;
        if (!ExtractDestination(output.scriptPubKey, destination)) {
            if (output.nValue == 0 && output.scriptPubKey.IsUnspendable()) {
                continue;
            }
            out.error = WalletQmlModel::tr("Only PSBTs with standard address outputs are supported right now.");
            return out;
        }
        if (can_send && wallet->txoutIsMine(output)) {
            continue;
        }
        const QString address{QString::fromStdString(EncodeDestination(destination))};
        draft_recipients.push_back({address, ReadAddressLabel(*wallet, address), output.nValue});
        recipient_total += output.nValue;
    }

    if (draft_recipients.empty()) {
        out.error = WalletQmlModel::tr("The PSBT does not have any recipient outputs to review.");
        return out;
    }
    if (draft_recipients.size() > 25) {
        out.error = WalletQmlModel::tr("The PSBT has more recipients than this send flow supports.");
        return out;
    }
    if (recipient_total <= 0) {
        out.error = WalletQmlModel::tr("The PSBT does not send a positive amount.");
        return out;
    }


    out.tx = MakeTransactionRef(*analysis_tx);
    out.psbt = psbt;
    out.fee = analysis.fee.value_or(0);
    out.can_send = can_send;
    out.can_broadcast = can_broadcast;
    out.message = review_message;
    out.recipients = std::move(draft_recipients);
    QSet<QString> reviewed_addresses;
    for (const auto& recipient : out.recipients) reviewed_addresses.insert(recipient.address);
    out.flow = ReadTransactionFlow(*wallet, out.tx, &psbt, reviewed_addresses);
    return out;
}

} // namespace

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node, QObject* parent, QString initial_name)
    : WalletQmlModel(std::shared_ptr<interfaces::Wallet>{std::move(wallet)}, node, parent, std::move(initial_name))
{
}

WalletQmlModel::WalletQmlModel(std::shared_ptr<interfaces::Wallet> wallet, interfaces::Node* node, QObject* parent, QString initial_name)
    : QObject(parent)
    , m_wallet(std::move(wallet))
    , m_node(node)
{
    m_receive_requests = new ReceiveRequestHistoryModel(this);
    m_receive_payment_poll_timer.setInterval(1000);
    connect(&m_receive_payment_poll_timer, &QTimer::timeout, this, &WalletQmlModel::pollUnconfirmedReceiveRequestPayments);
    m_wallet_state.name = std::move(initial_name);
    m_address_list_model = new AddressListModel(this);
    m_bump_transaction_model = new BumpTransactionModel(m_wallet, backendExecutor(), this);
    m_bump_transaction_model->setSecurityStateChangedFn([this]() { requestWalletStateRefresh(); });
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    connect(m_send_recipients, &SendRecipientsListModel::totalAmountChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    connect(m_send_recipients, &SendRecipientsListModel::validationChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    m_sign_verify_message_model = new SignVerifyMessageModel(m_wallet, m_backend_executor, this);
    m_sign_verify_message_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_current_payment_request = new PaymentRequest(this);
    m_receiving_address = new PaymentRequest(this);
    m_detail_payment_request = new PaymentRequest(this);
    m_imported_psbt_model = new PsbtQmlModel(m_wallet, m_node, this, backendExecutor());
    initializeFeeEstimator();
    initializeWalletState();
    subscribeToWalletSignals();
    requestWalletStateRefresh();
}

WalletQmlModel::WalletQmlModel(interfaces::Node* node, QObject* parent)
    : QObject(parent)
    , m_node(node)
{
    m_address_list_model = new AddressListModel(this);
    m_bump_transaction_model = new BumpTransactionModel(nullptr, backendExecutor(), this);
    m_bump_transaction_model->setSecurityStateChangedFn([this]() { requestWalletStateRefresh(); });
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    connect(m_send_recipients, &SendRecipientsListModel::totalAmountChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    connect(m_send_recipients, &SendRecipientsListModel::validationChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    m_sign_verify_message_model = new SignVerifyMessageModel({}, m_backend_executor, this);
    m_sign_verify_message_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_current_payment_request = new PaymentRequest(this);
    m_receiving_address = new PaymentRequest(this);
    m_detail_payment_request = new PaymentRequest(this);
    m_receive_requests = new ReceiveRequestHistoryModel(this);
    m_imported_psbt_model = new PsbtQmlModel(nullptr, m_node, this, backendExecutor());
    initializeFeeEstimator();
    initializeWalletState();
}

WalletQmlModel::WalletQmlModel(QObject* parent)
    : WalletQmlModel(nullptr, parent)
{
}

WalletQmlModel::~WalletQmlModel()
{
    beginShutdown();
    if (m_fee_estimation_timer) {
        m_fee_estimation_timer->stop();
    }
    delete m_transaction_activity_model;
    delete m_address_list_model;
    delete m_coins_list_model;
    delete m_send_recipients;
    delete m_sign_verify_message_model;
    delete m_current_payment_request;
    delete m_detail_payment_request;
    delete m_receive_requests;
    delete m_imported_psbt_model;
    if (m_current_transaction) {
        delete m_current_transaction;
    }
}

void WalletQmlModel::initializeWalletState()
{
    m_notification_bridge = std::shared_ptr<QObject>{new QObject, [](QObject* object) {
        if (QThread::currentThread() == object->thread()) delete object;
        else object->deleteLater();
    }};
    if (m_wallet) m_wallet_state_backend = std::make_shared<WalletStateBackend>(m_wallet);
    connect(m_backend_executor.get(), &BackendExecutor::drained, this, [this] {
        Q_EMIT shutdownFinished();
    });
}

void WalletQmlModel::requestWalletStateRefresh()
{
    if (m_stopping || !m_wallet_state_backend) return;
    m_state_refresh_requested = true;
    if (m_state_refresh_pending || m_state_refresh_scheduled) return;
    m_state_refresh_scheduled = true;
    Q_EMIT receiveRequestReconciliationPendingChanged();
    QTimer::singleShot(0, this, &WalletQmlModel::startWalletStateRefresh);
}

void WalletQmlModel::startWalletStateRefresh()
{
    m_state_refresh_scheduled = false;
    if (m_stopping || !m_wallet_state_backend) return;
    m_state_refresh_pending = true;
    m_state_refresh_requested = false;
    const auto backend = m_wallet_state_backend;
    const auto node = m_node;
    m_backend_executor->submit(this, [backend, node] { return backend->read(node); },
        [this](WalletStateSnapshot state) {
            m_state_refresh_pending = false;
            applyWalletState(std::move(state));
            if (m_state_refresh_requested) requestWalletStateRefresh();
            Q_EMIT receiveRequestReconciliationPendingChanged();
        }, [this](std::exception_ptr) {
            m_state_refresh_pending = false;
            m_wallet_state_error = tr("Wallet information could not be loaded. Please try again.");
            Q_EMIT walletStateChanged();
            if (m_state_refresh_requested) requestWalletStateRefresh();
            Q_EMIT receiveRequestReconciliationPendingChanged();
        });
}

void WalletQmlModel::applySecurityState(bool encrypted, bool locked)
{
    if (m_is_encrypted == encrypted && m_is_locked == locked) return;
    m_is_encrypted = encrypted;
    m_is_locked = locked;
    m_wallet_state.encrypted = encrypted;
    m_wallet_state.locked = locked;
    Q_EMIT securityStateChanged();
}

void WalletQmlModel::applyWalletState(WalletStateSnapshot state)
{
    if (m_stopping) return;
    const bool name_changed = state.name != m_wallet_state.name;
    const bool labels_changed = state.labels != m_wallet_state.labels;
    const bool balance_changed = !m_wallet_state_ready || state.balance != m_wallet_state.balance || state.available_balance != m_wallet_state.available_balance;
    const bool was_encrypted = m_is_encrypted;
    const bool was_locked = m_is_locked;
    m_wallet_state = std::move(state);
    m_wallet_state_ready = true;
    m_wallet_state_error = m_wallet_subscription_error.isEmpty() ? m_wallet_state.persistence_error : m_wallet_subscription_error;
    m_is_encrypted = m_wallet_state.encrypted;
    m_is_locked = m_wallet_state.locked;
    m_receive_reconciliation_applying = true;
    m_receive_requests->setEntries(std::vector<QmlRecentRequestEntry>{m_wallet_state.receive_requests});
    for (auto* request : {m_receiving_address, m_current_payment_request, m_detail_payment_request}) {
        if (!request) continue;
        if (m_wallet_state.observed_addresses.contains(request->address())) request->setPaymentReceived(true);
        if (const auto entry = m_receive_requests->entryById(request->id()); entry && !request->isEditing()) {
            applySavedRequest(request, *entry);
        }
    }
    updateReceivedPaymentRequestAmounts();
    updateReceivePaymentPollTimer();
    m_receive_reconciliation_applying = false;
    if (name_changed) { Q_EMIT nameChanged(); Q_EMIT displayNameChanged(); }
    if (was_encrypted != m_is_encrypted || was_locked != m_is_locked) Q_EMIT securityStateChanged();
    Q_EMIT walletStateChanged();
    if (labels_changed) Q_EMIT addressListChanged();
    if (balance_changed) Q_EMIT balanceChanged();
    Q_EMIT sendAmountExhaustsBalanceChanged();
}

void WalletQmlModel::applySavedRequest(PaymentRequest* request, const QmlRecentRequestEntry& entry)
{
    if (!request) return;
    request->setDestination(DecodeDestination(entry.recipient.address));
    request->setId(static_cast<unsigned int>(entry.id));
    request->setCreated(entry.date);
    request->amount()->setSatoshi(entry.recipient.amount);
    request->setLabel(QString::fromStdString(entry.recipient.label));
    request->setMessage(QString::fromStdString(entry.recipient.message));
    request->setNoteSelf(QString::fromStdString(entry.recipient.noteSelf));
    request->setPaymentReceived(entry.payment_received);
    request->setReceivedAmountSatoshi(receivedPaymentRequestAmount(request->address()));
    request->setIsEditing(false);
    request->setNeedsUnlock(false);
    request->setUnlockError({});
}

bool WalletQmlModel::submitReceiveCommand(ReceiveCommand command, QString operation, PaymentRequest* request)
{
    if (m_stopping || !m_wallet_state_backend || m_receive_operation_pending) return false;
    m_receive_operation_pending = true;
    m_receive_operation_error.clear();
    Q_EMIT receiveOperationPendingChanged();
    Q_EMIT receiveOperationErrorChanged();
    Q_EMIT receiveRequestReconciliationPendingChanged();
    const auto backend = m_wallet_state_backend;
    const auto node = m_node;
    const QPointer<PaymentRequest> held{request};
    const auto revision = request ? request->revision() : 0;
    const bool reserve = command.reserve_receiving_address;
    const bool accepted = m_backend_executor->submit(this,
        [backend, node, command = std::move(command)]() mutable { return backend->execute(node, std::move(command)); },
        [this, held, revision, operation, reserve](ReceiveCommandResult result) {
            const bool current_request = held && held->revision() == revision;
            m_receive_operation_pending = false;
            m_receive_operation_error = result.error;
            applyWalletState(std::move(result.snapshot));
            if (current_request) {
                held->setNeedsUnlock(result.needs_unlock);
                held->setUnlockError(result.error);
                if (result.success && !result.receiving_address.isEmpty()) {
                    held->clear();
                    held->setDestination(DecodeDestination(result.receiving_address.toStdString()));
                }
                if (result.success && result.saved_entry) applySavedRequest(held, *result.saved_entry);
            }
            if (result.success && result.saved_entry) {
                if (m_detail_payment_request->id() == QString::number(result.saved_entry->id)) applySavedRequest(m_detail_payment_request, *result.saved_entry);
                if (!held && m_current_payment_request->id() == QString::number(result.saved_entry->id)) applySavedRequest(m_current_payment_request, *result.saved_entry);
            }
            if (result.success && reserve && current_request) m_receiving_address->clear();
            if (held && !current_request) {
                result.success = false;
                m_receive_operation_error = tr("The request changed while the operation was running. Check Activity for saved requests.");
            }
            Q_EMIT receiveOperationPendingChanged();
            Q_EMIT receiveOperationErrorChanged();
            Q_EMIT receiveRequestReconciliationPendingChanged();
            Q_EMIT receiveOperationFinished(operation, result.success);
        }, [this, operation](std::exception_ptr) {
            m_receive_operation_pending = false;
            m_receive_operation_error = tr("The wallet operation failed. Please try again.");
            Q_EMIT receiveOperationPendingChanged();
            Q_EMIT receiveOperationErrorChanged();
            Q_EMIT receiveRequestReconciliationPendingChanged();
            Q_EMIT receiveOperationFinished(operation, false);
            requestWalletStateRefresh();
        });
    if (!accepted) {
        m_receive_operation_pending = false;
        Q_EMIT receiveOperationPendingChanged();
        Q_EMIT receiveRequestReconciliationPendingChanged();
    }
    return accepted;
}

void WalletQmlModel::beginShutdown(bool remove_wallet)
{
    if (m_stopping) return;
    m_stopping = true;
    m_receive_payment_poll_timer.stop();
    if (m_transaction_activity_model) m_transaction_activity_model->stop();
    if (m_fee_estimation_timer) m_fee_estimation_timer->stop();
    // Move subscriptions into the last backend job. Callback delivery targets
    // the separately retained bridge, never a raw pointer to this model.
    auto handlers = std::make_tuple(std::move(m_handler_status_changed), std::move(m_handler_address_list_changed),
                                   std::move(m_handler_transaction_changed), std::move(m_handler_unload));
    // The retirement payload must own the last wallet handle after child
    // commands drain, so releasing GUI children cannot destroy the backend.
    if (m_bump_transaction_model) m_bump_transaction_model->detachWallet();
    if (m_imported_psbt_model) m_imported_psbt_model->detachWallet();
    auto wallet = std::move(m_wallet);
    auto backend = std::move(m_wallet_state_backend);
    m_sign_verify_message_model->setWallet({});
    m_sign_verify_message_model->setSecurityStateChangedFn({});
    m_backend_executor->submit(this, [wallet = std::move(wallet), backend = std::move(backend), handlers = std::move(handlers), remove_wallet]() mutable {
        std::apply([](auto&... handler) { ((handler ? handler->disconnect() : void()), ...); }, handlers);
        if (wallet && remove_wallet) wallet->remove();
    }, [] {});
    m_backend_executor->shutdown();
}

TransactionActivityModel* WalletQmlModel::transactionActivityModel()
{
    // Instantiate when the activity screen first asks for it.
    if (!m_transaction_activity_model) m_transaction_activity_model = new TransactionActivityModel(this);
    return m_transaction_activity_model;
}

void WalletQmlModel::setNode(interfaces::Node* node)
{
    m_node = node;
    if (m_imported_psbt_model) {
        m_imported_psbt_model->setNode(node);
    }
}

void WalletQmlModel::initializeFeeEstimator()
{
    m_fee_estimation_timer = new QTimer(this);
    m_fee_estimation_timer->setSingleShot(true);
    m_fee_estimation_timer->setInterval(FEE_ESTIMATE_DEBOUNCE_MS);
    connect(m_fee_estimation_timer, &QTimer::timeout, this, &WalletQmlModel::requestFeeEstimatesNow);
    connect(this, &WalletQmlModel::walletStateChanged, this, [this] {
        m_available_fee_balance.reset();
        m_fee_estimation_timer->start();
    });
    connect(m_send_recipients, &QAbstractItemModel::dataChanged, this, [this] { ++m_send_draft_revision; });
    connect(m_send_recipients, &SendRecipientsListModel::countChanged, this, [this] { ++m_send_draft_revision; });
}

QString WalletQmlModel::balance() const
{
    return QmlBitcoinUnits::formatForDisplay(QmlBitcoinUnits::fromDisplayUnit(m_display_unit), m_wallet_state.balance);
}

qint64 WalletQmlModel::balanceSatoshi() const
{
    return m_wallet_state.balance;
}

QString WalletQmlModel::estimatedFee() const
{
    if (m_custom_fee_enabled) {
        return customFeeRateValid() && m_custom_fee_estimate.has_value()
            ? FormatFeeEstimate(m_custom_fee_estimate->fee)
            : QString{};
    }
    return estimatedFeeForTarget(feeTargetBlocks());
}

std::optional<SendFeePreview> WalletQmlModel::selectedFeePreview() const
{
    if (m_custom_fee_enabled) return customFeeRateValid() ? m_custom_fee_estimate : std::nullopt;
    const auto it = m_fee_estimates.constFind(feeTargetBlocks());
    return it == m_fee_estimates.constEnd() ? std::nullopt : std::make_optional(it.value());
}

std::optional<CAmount> WalletQmlModel::selectedFeeEstimate() const
{
    const auto preview = selectedFeePreview();
    return preview ? std::make_optional(preview->fee) : std::nullopt;
}

qint64 WalletQmlModel::estimatedFeeSatoshi() const
{
    return selectedFeeEstimate().value_or(-1);
}

QString WalletQmlModel::estimatedFeeRate() const
{
    const auto preview = selectedFeePreview();
    return preview && preview->rate_per_kvb > 0
        ? QString::number(preview->rate_per_kvb / 1000.0, 'f', 3).remove(QRegularExpression("0+$")).remove(QRegularExpression("\\.$"))
        : QString{};
}

int WalletQmlModel::estimatedInputCount() const
{
    const auto preview = selectedFeePreview();
    return preview ? preview->inputs : 0;
}

qint64 WalletQmlModel::sendTotalSatoshi() const
{
    if (!m_send_recipients) return 0;
    const auto fee = selectedFeeEstimate();
    return m_send_recipients->totalAmountSatoshi()
        + (fee ? *fee : 0);
}

qint64 WalletQmlModel::availableSendBalanceSatoshi() const
{
    return m_available_fee_balance.value_or(cachedWalletState().available_balance);
}

bool WalletQmlModel::sendDraftSweepsWallet() const
{
    const auto preview = selectedFeePreview();
    return preview && preview->sweeps_wallet;
}

void WalletQmlModel::useMaximum()
{
    if (!m_wallet || !m_send_recipients || !m_send_recipients->currentRecipient()) return;
    setMaximumRecipient(m_send_recipients->currentRecipient());
    scheduleFeeEstimates();
}

void WalletQmlModel::setMaximumRecipient(SendRecipient* recipient)
{
    if (m_maximum_recipient == recipient) return;
    QObject::disconnect(m_maximum_amount_connection);
    QObject::disconnect(m_maximum_recipient_destroyed_connection);
    m_maximum_recipient = recipient;
    if (recipient) {
        m_maximum_amount_connection = connect(recipient->amount(), &BitcoinAmount::amountChanged, this, [this] {
            // Editing the prefilled amount switches back to a fixed-amount payment.
            if (!m_updating_maximum) setMaximumRecipient(nullptr);
        });
        m_maximum_recipient_destroyed_connection = connect(recipient, &QObject::destroyed, this, [this] {
            m_maximum_recipient.clear();
            Q_EMIT maximumRecipientChanged();
        });
    }
    Q_EMIT maximumRecipientChanged();
}

void WalletQmlModel::updateMaximumAmount()
{
    const auto preview = selectedFeePreview();
    if (!m_maximum_recipient || !preview || !preview->maximum_amount) return;
    QScopedValueRollback<bool> updating{m_updating_maximum, true};
    m_maximum_recipient->amount()->setSatoshi(*preview->maximum_amount);
}

void WalletQmlModel::setCustomFeeTarget(unsigned int target)
{
    if (!m_wallet || target < 1 || target > 1008) return;
    setCustomFeeEnabled(true);
    const auto rate = m_fee_target_rates.value(target, 0);
    setCustomFeeRate(rate > 0 ? QString::number(rate / 1000.0, 'f', 3) : QString{});
    m_pending_custom_target = target;
    setFeeTargetBlocks(target);
    scheduleFeeEstimates();
}

CFeeRate WalletQmlModel::dustRelayFee() const
{
    return cachedWalletState().dust_relay_fee;
}

bool WalletQmlModel::sendAmountExhaustsBalance() const
{
    if (!m_wallet || !m_send_recipients || !m_send_recipients->allValid()) {
        return false;
    }

    wallet::CCoinControl coin_control{m_coin_control};
    ApplySelectedInputsPolicy(coin_control);

    const CAmount balance{m_coin_control.HasSelected() ? m_available_selected_balance : availableSendBalanceSatoshi()};
    const CAmount total_amount{m_send_recipients->totalAmountSatoshi()};
    if (total_amount > balance) {
        return true;
    }
    if (const auto fee = selectedFeeEstimate()) {
        return AmountPlusFeeExceedsBalance(total_amount, *fee, balance);
    }

    // Without an estimate, a fixed-amount send cannot safely spend the full
    // balance because prepareTransaction() will still need to add a fee.
    return total_amount >= balance;
}

bool WalletQmlModel::customFeeRateValid() const
{
    return ParseCustomFeeRatePerKvB(m_custom_fee_rate).has_value();
}

QString WalletQmlModel::estimatedFeeForTarget(const unsigned int target_blocks) const
{
    const auto estimate = m_fee_estimates.constFind(target_blocks);
    if (estimate != m_fee_estimates.constEnd()) {
        return FormatFeeEstimate(estimate.value().fee);
    }

    return {};
}

int WalletQmlModel::feeTargetIndex(const unsigned int target_blocks) const
{
    for (size_t i = 0; i < STANDARD_FEE_TARGETS.size(); ++i) {
        if (STANDARD_FEE_TARGETS[i] == target_blocks) {
            return static_cast<int>(i);
        }
    }

    return 1;
}

QString WalletQmlModel::name() const
{
    return m_wallet_state.name;
}

QString WalletQmlModel::displayName() const
{
    if (!m_display_name.isEmpty()) {
        return m_display_name;
    }
    return name();
}

void WalletQmlModel::setDisplayName(const QString& display_name)
{
    if (m_display_name != display_name) {
        m_display_name = display_name;
        Q_EMIT displayNameChanged();
    }
}

QString WalletQmlModel::keyScheme() const
{
    return keySchemeDisplayText(keySchemeKind());
}

WalletQmlModel::KeyScheme WalletQmlModel::keySchemeKind() const
{
    return static_cast<KeyScheme>(m_wallet_state.key_scheme);
}

WalletQmlModel::KeyScheme WalletQmlModel::keySchemeForWallet(interfaces::Wallet& wallet)
{
    return static_cast<KeyScheme>(ReadWalletKeyScheme(wallet));
}

QString WalletQmlModel::keySchemeDisplayText(KeyScheme scheme)
{
    switch (scheme) {
    case KeyScheme::WatchOnly:
        return tr("Watch-only");
    case KeyScheme::MultiKey:
        return tr("Multi-key");
    case KeyScheme::ExternalSigner:
        return tr("External signer");
    case KeyScheme::SingleKey:
    default:
        return tr("Single-key");
    }
}

QString WalletQmlModel::privateKeysStatus() const
{
    return m_wallet_state.private_keys_disabled ? tr("Disabled") : tr("Enabled");
}

QString WalletQmlModel::externalSignerStatus() const
{
    return m_wallet_state.external_signer ? tr("Enabled") : tr("None");
}

bool WalletQmlModel::canManagePassphrase() const
{
    return m_wallet && m_wallet_state_ready && !m_wallet_state.private_keys_disabled;
}

bool WalletQmlModel::runSettingsOperation(const QString& operation, std::function<QString(interfaces::Wallet&)> work)
{
    if (m_settings_busy) return false;
    clearSettingsError();
    if (!m_wallet) {
        setSettingsError(tr("No wallet is selected."));
        return false;
    }
    m_settings_busy = true;
    Q_EMIT settingsBusyChanged();
    const auto complete = [this, operation](const QString& error) {
        m_settings_busy = false;
        setSettingsError(error);
        requestWalletStateRefresh();
        Q_EMIT settingsBusyChanged();
        Q_EMIT settingsOperationFinished(operation, error.isEmpty());
    };
    if (!backendExecutor()->submit(this,
            [wallet = walletHandle(), work = std::move(work)] {
                const QString error = work(*wallet);
                return std::tuple{error, wallet->isCrypted(), wallet->isLocked()};
            },
            [this, complete](const auto& result) {
                applySecurityState(std::get<1>(result), std::get<2>(result));
                complete(std::get<0>(result));
            }, [complete](std::exception_ptr) { complete(tr("The wallet operation failed.")); })) {
        complete(tr("The wallet is closing."));
        return false;
    }
    return true;
}

bool WalletQmlModel::encryptWallet(const QString& passphrase)
{
    if (passphrase.isEmpty()) {
        setSettingsError(tr("Enter a new wallet password."));
        return false;
    }
    return runSettingsOperation(QStringLiteral("encrypt"),
        [passphrase = QmlUtil::SecureStringFromQString(passphrase)](interfaces::Wallet& wallet) mutable {
            const bool encrypted = wallet.encryptWallet(passphrase);
            QmlUtil::ClearSecureString(passphrase);
            return encrypted ? QString{} : tr("The wallet password could not be set.");
        });
}

bool WalletQmlModel::changeWalletPassphrase(const QString& old_passphrase, const QString& new_passphrase)
{
    if (old_passphrase.isEmpty() || new_passphrase.isEmpty()) {
        setSettingsError(old_passphrase.isEmpty() ? tr("Enter the current wallet password.") : tr("Enter a new wallet password."));
        return false;
    }
    return runSettingsOperation(QStringLiteral("passphrase"),
        [old_passphrase = QmlUtil::SecureStringFromQString(old_passphrase),
         new_passphrase = QmlUtil::SecureStringFromQString(new_passphrase)](interfaces::Wallet& wallet) mutable {
            const bool changed = wallet.changeWalletPassphrase(old_passphrase, new_passphrase);
            QmlUtil::ClearSecureString(old_passphrase);
            QmlUtil::ClearSecureString(new_passphrase);
            return changed ? QString{} : tr("The current wallet password was incorrect.");
        });
}

bool WalletQmlModel::backupWallet(const QString& path)
{
    if (path.trimmed().isEmpty()) {
        setSettingsError(tr("Choose a location for the wallet backup."));
        return false;
    }
    return runSettingsOperation(QStringLiteral("backup"), [path](interfaces::Wallet& wallet) {
        QString wallet_name = QString::fromStdString(wallet.getWalletName());
        if (wallet_name.isEmpty()) wallet_name = QStringLiteral("wallet");
        const QString filename = wallet_name.replace(QRegularExpression(QStringLiteral("[/\\\\]")), QStringLiteral("_")) + QStringLiteral(".bak");
        QString destination = path;
        const QFileInfo info(destination);
        if (info.isDir()) {
            destination = QDir(destination).filePath(filename);
        } else if (!destination.endsWith(QStringLiteral(".bak"), Qt::CaseInsensitive) &&
                   !destination.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive)) {
            destination += QStringLiteral(".bak");
        }
        return wallet.backupWallet(destination.toStdString()) ? QString{} : tr("The wallet could not be backed up.");
    });
}

void WalletQmlModel::clearSettingsError()
{
    setSettingsError(QString());
}

QVariantList WalletQmlModel::availableReceiveAddressTypes() const
{
    if (!m_wallet || !m_wallet_state.can_get_addresses) {
        return {};
    }

    QVariantList types;
    const std::array ordered_types{
        OutputType::BECH32M,
        OutputType::BECH32,
        OutputType::P2SH_SEGWIT,
        OutputType::LEGACY,
    };

    for (const OutputType type : ordered_types) {
        if (type == OutputType::BECH32M && !m_wallet_state.taproot_enabled) {
            continue;
        }
        QVariantMap item;
        item.insert(QStringLiteral("id"), OutputTypeId(type));
        item.insert(QStringLiteral("label"), OutputTypeLabel(type));
        item.insert(QStringLiteral("description"), OutputTypeDescription(type));
        types.append(item);
    }
    return types;
}

QString WalletQmlModel::defaultReceiveAddressType() const
{
    return m_wallet_state.receive_address_type;
}

void WalletQmlModel::setDefaultReceiveAddressType(const QString& address_type)
{
    ReceiveCommand command;
    command.kind = ReceiveCommand::Kind::AddressType;
    command.address_type = address_type;
    submitReceiveCommand(std::move(command), QStringLiteral("addressType"));
}

QString WalletQmlModel::receiveAddressTypeLabel(const QString& address_type) const
{
    for (const QVariant& item : availableReceiveAddressTypes()) {
        const QVariantMap type{item.toMap()};
        if (type.value(QStringLiteral("id")).toString() == address_type) {
            return type.value(QStringLiteral("label")).toString();
        }
    }
    return {};
}

bool WalletQmlModel::setCurrentPaymentRequestAddress(QString address)
{
    if (!m_wallet || !m_current_payment_request || address.isEmpty()) {
        return false;
    }

    const CTxDestination destination{DecodeDestination(address.toStdString())};
    if (!IsValidDestination(destination)) {
        return false;
    }

    // A request already saved for this address is loaded for editing, not
    // duplicated: reopening the action on such an address means changing
    // the saved request, and silently minting a second request for the
    // same address multiplies rows that all describe one ask.
    if (m_receive_requests) {
        const QVariantList existing = m_receive_requests->matchingEntriesForAddress(address);
        if (!existing.isEmpty()) {
            const QString request_id = existing.first().toMap().value(QStringLiteral("requestId")).toString();
            if (!loadPaymentRequest(request_id)) {
                return false;
            }
            m_current_payment_request->setIsEditing(true);
            return true;
        }
    }

    m_current_payment_request->clear();
    m_current_payment_request->setDestination(destination);
    m_current_payment_request->setNoteSelf(getAddressLabel(address));
    m_current_payment_request->setIsEditing(false);
    m_current_payment_request->setIsEditing(true);
    // The explicit Receive action in Addresses must keep the chosen address.
    m_receiving_address->clear();
    m_receiving_address->setDestination(destination);
    ReceiveCommand command;
    command.kind = ReceiveCommand::Kind::RememberAddress;
    command.address = address;
    return submitReceiveCommand(std::move(command), QStringLiteral("rememberAddress"));
}

bool WalletQmlModel::ensureReceivingAddress(bool next, const QString& address_type)
{
    ReceiveCommand command;
    command.kind = ReceiveCommand::Kind::EnsureAddress;
    command.next = next;
    command.address_type = address_type;
    command.address = m_receiving_address->address();
    return submitReceiveCommand(std::move(command), QStringLiteral("address"), m_receiving_address);
}

bool WalletQmlModel::ensureReceivingAddressWithPassphrase(const QString& passphrase, bool next, const QString& address_type)
{
    ReceiveCommand command;
    command.kind = ReceiveCommand::Kind::EnsureAddress;
    command.next = next;
    command.address_type = address_type;
    command.address = m_receiving_address->address();
    command.passphrase = QmlUtil::SecureStringFromQString(passphrase);
    return submitReceiveCommand(std::move(command), QStringLiteral("address"), m_receiving_address);
}

bool WalletQmlModel::commitReceivingPaymentRequest()
{
    if (!m_current_payment_request || !m_current_payment_request->id().isEmpty() || m_receiving_address->address().isEmpty()) return false;
    if (m_current_payment_request->amount()->satoshi() == 0 && m_current_payment_request->label().trimmed().isEmpty()
        && m_current_payment_request->message().trimmed().isEmpty() && m_current_payment_request->noteSelf().trimmed().isEmpty()) return false;
    m_current_payment_request->setDestination(m_receiving_address->destination());
    return savePaymentRequest(m_current_payment_request);
}

bool WalletQmlModel::savePaymentRequest(PaymentRequest* request)
{
    if (!request || !MoneyRange(request->amount()->satoshi())) return false;
    ReceiveCommand command;
    command.kind = ReceiveCommand::Kind::Save;
    command.entry.id = request->id().toLongLong();
    command.entry.date = request->created();
    command.entry.recipient.address = request->address().toStdString();
    command.entry.recipient.amount = request->amount()->satoshi();
    command.entry.recipient.label = request->label().toStdString();
    command.entry.recipient.message = request->message().toStdString();
    command.entry.recipient.noteSelf = request->noteSelf().toStdString();
    command.address_type = request->addressType();
    command.reserve_receiving_address = request == m_current_payment_request && request->id().isEmpty() && !request->address().isEmpty();
    return submitReceiveCommand(std::move(command), QStringLiteral("save"), request);
}

bool WalletQmlModel::commitPaymentRequest()
{
    return savePaymentRequest(m_current_payment_request);
}

bool WalletQmlModel::commitPaymentRequestWithPassphrase(const QString& passphrase)
{
    if (!m_current_payment_request || !MoneyRange(m_current_payment_request->amount()->satoshi())) return false;
    ReceiveCommand command;
    command.kind = ReceiveCommand::Kind::Save;
    command.entry.id = m_current_payment_request->id().toLongLong();
    command.entry.recipient.address = m_current_payment_request->address().toStdString();
    command.entry.recipient.amount = m_current_payment_request->amount()->satoshi();
    command.entry.recipient.label = m_current_payment_request->label().toStdString();
    command.entry.recipient.message = m_current_payment_request->message().toStdString();
    command.entry.recipient.noteSelf = m_current_payment_request->noteSelf().toStdString();
    command.address_type = m_current_payment_request->addressType();
    command.passphrase = QmlUtil::SecureStringFromQString(passphrase);
    return submitReceiveCommand(std::move(command), QStringLiteral("save"), m_current_payment_request);
}

void WalletQmlModel::reloadReceiveRequests()
{
    requestWalletStateRefresh();
}

bool WalletQmlModel::removeReceiveRequest(const QString& request_id)
{
    ReceiveCommand command;
    command.kind = ReceiveCommand::Kind::Remove;
    command.entry.id = request_id.toLongLong();
    return submitReceiveCommand(std::move(command), QStringLiteral("remove"));
}

bool WalletQmlModel::loadPaymentRequest(const QString& request_id)
{
    if (!m_current_payment_request || !m_receive_requests) return false;
    const auto entry = m_receive_requests->entryById(request_id);
    if (!entry) return false;
    if (entry->id < 0 || entry->id > std::numeric_limits<unsigned int>::max()) return false;

    const CTxDestination destination = DecodeDestination(entry->recipient.address);
    if (!IsValidDestination(destination)) return false;

    m_current_payment_request->clear();
    m_current_payment_request->setDestination(destination);
    m_current_payment_request->setLabel(QString::fromStdString(entry->recipient.label));
    m_current_payment_request->setMessage(QString::fromStdString(entry->recipient.message));
    m_current_payment_request->setNoteSelf(QString::fromStdString(entry->recipient.noteSelf));
    m_current_payment_request->amount()->setSatoshi(entry->recipient.amount);
    m_current_payment_request->setId(static_cast<unsigned int>(entry->id));
    m_current_payment_request->setCreated(entry->date);
    m_current_payment_request->setReceivedAmountSatoshi(receivedPaymentRequestAmount(m_current_payment_request->address()));
    m_current_payment_request->setPaymentReceived(entry->payment_received);
    m_current_payment_request->setIsEditing(false);
    return true;
}

bool WalletQmlModel::loadPaymentRequestDetail(const QString& request_id)
{
    if (!m_detail_payment_request || !m_receive_requests) return false;
    const auto entry = m_receive_requests->entryById(request_id);
    if (!entry) return false;
    if (entry->id < 0 || entry->id > std::numeric_limits<unsigned int>::max()) return false;

    const CTxDestination destination = DecodeDestination(entry->recipient.address);
    if (!IsValidDestination(destination)) return false;

    m_detail_payment_request->clear();
    m_detail_payment_request->setDestination(destination);
    m_detail_payment_request->setLabel(QString::fromStdString(entry->recipient.label));
    m_detail_payment_request->setMessage(QString::fromStdString(entry->recipient.message));
    m_detail_payment_request->setNoteSelf(QString::fromStdString(entry->recipient.noteSelf));
    m_detail_payment_request->amount()->setSatoshi(entry->recipient.amount);
    m_detail_payment_request->setId(static_cast<unsigned int>(entry->id));
    m_detail_payment_request->setCreated(entry->date);
    m_detail_payment_request->setReceivedAmountSatoshi(receivedPaymentRequestAmount(m_detail_payment_request->address()));
    m_detail_payment_request->setPaymentReceived(entry->payment_received);
    m_detail_payment_request->setIsEditing(false);
    return true;
}

bool WalletQmlModel::updatePaymentRequest(const QString& request_id, qint64 amount,
                                        const QString& label, const QString& message, const QString& note)
{
    const auto entry = m_receive_requests->entryById(request_id);
    if (!entry || !MoneyRange(amount)) return false;
    if (amount == 0 && label.trimmed().isEmpty() && message.trimmed().isEmpty() && note.trimmed().isEmpty()) return false;
    ReceiveCommand command;
    command.kind = ReceiveCommand::Kind::Save;
    command.entry = *entry;
    command.entry.recipient.amount = amount;
    command.entry.recipient.label = label.toStdString();
    command.entry.recipient.message = message.toStdString();
    command.entry.recipient.noteSelf = note.toStdString();
    return submitReceiveCommand(std::move(command), QStringLiteral("update"));
}

void WalletQmlModel::refreshReceiveRequestPayments()
{
    requestWalletStateRefresh();
}

void WalletQmlModel::pollUnconfirmedReceiveRequestPayments()
{
    requestWalletStateRefresh();
}

void WalletQmlModel::updateReceivePaymentPollTimer()
{
    if (!m_stopping && m_wallet_state.unconfirmed_payments) m_receive_payment_poll_timer.start();
    else m_receive_payment_poll_timer.stop();
}

CAmount WalletQmlModel::receivedPaymentRequestAmount(const QString& address) const
{
    const auto it = m_wallet_state.received_amounts.find(address);
    return it == m_wallet_state.received_amounts.end() ? 0 : it->second;
}

void WalletQmlModel::updateReceivedPaymentRequestAmounts()
{
    for (auto* request : {m_current_payment_request, m_detail_payment_request}) {
        if (request && !request->id().isEmpty()) {
            request->setReceivedAmountSatoshi(receivedPaymentRequestAmount(request->address()));
        }
    }
}

void WalletQmlModel::usePaymentRequestAsTemplate(const QString& request_id)
{
    if (!m_current_payment_request || !m_receive_requests) return;
    const auto entry = m_receive_requests->entryById(request_id);
    if (!entry) return;
    const CTxDestination destination = DecodeDestination(entry->recipient.address);

    // A repeated request starts with a fresh receiving address, allocated by
    // the Receive page (including its normal wallet-unlock flow).
    m_receiving_address->clear();
    ReceiveCommand command;
    command.kind = ReceiveCommand::Kind::RememberAddress;
    submitReceiveCommand(std::move(command), QStringLiteral("rememberAddress"));
    m_current_payment_request->clear();
    m_current_payment_request->setLabel(QString::fromStdString(entry->recipient.label));
    m_current_payment_request->setMessage(QString::fromStdString(entry->recipient.message));
    m_current_payment_request->setNoteSelf(QString::fromStdString(entry->recipient.noteSelf));
    m_current_payment_request->amount()->setSatoshi(entry->recipient.amount);
    m_current_payment_request->setAddressType(OutputTypeIdFromDestination(destination));

    // Toggle isEditing to re-trigger QML input sync with populated values
    m_current_payment_request->setIsEditing(false);
    m_current_payment_request->setIsEditing(true);
}

unsigned int WalletQmlModel::nextPaymentRequestId() const
{
    if (!m_receive_requests) return 1;
    const int64_t max_id = m_receive_requests->maxId();
    if (max_id <= 0 || max_id >= std::numeric_limits<unsigned int>::max() - 1) return 1;
    return static_cast<unsigned int>(max_id + 1);
}

QString WalletQmlModel::getAddressLabel(const QString& address) const
{
    const auto found = m_wallet_state.labels.find(address);
    return found == m_wallet_state.labels.end() ? QString{} : found->second;
}

bool WalletQmlModel::setAddressLabel(const QString& address, const QString& label)
{
    ReceiveCommand command;
    command.kind = ReceiveCommand::Kind::Label;
    command.address = address;
    command.label = label;
    return submitReceiveCommand(std::move(command), QStringLiteral("label"));
}

bool WalletQmlModel::writeAddressBookLabel(const QString& address, const QString& label)
{
    return setAddressLabel(address, label);
}

void WalletQmlModel::syncPaymentRequestNoteToAddress(const QString& address, const QString& label)
{
    setAddressLabel(address, label);
}

std::vector<interfaces::WalletAddress> WalletQmlModel::getAddresses() const
{
    return m_wallet_state.addresses;
}

std::map<QString, CAmount> WalletQmlModel::addressBalances() const
{
    return m_wallet_state.address_balances;
}

std::set<QString> WalletQmlModel::usedAddresses() const
{
    return m_wallet_state.used_addresses;
}

std::set<QString> WalletQmlModel::changeAddresses() const
{
    return m_wallet_state.change_addresses;
}

void WalletQmlModel::scheduleFeeEstimates()
{
    if (m_updating_maximum || m_fee_estimation_timer == nullptr) {
        return;
    }
    ++m_send_draft_revision;
    m_fee_estimation_timer->stop();

    // Until a destination is entered, show the gross remainder. A valid
    // address enables the sendall preview, which replaces it with the net amount.
    if (m_wallet && m_maximum_recipient && m_maximum_recipient->address()->address().isEmpty()) {
        wallet::CCoinControl control{m_coin_control};
        ApplySelectedInputsPolicy(control);
        CAmount remainder = control.HasSelected() ? m_available_selected_balance : availableSendBalanceSatoshi();
        for (const auto* recipient : m_send_recipients->recipients()) {
            if (recipient != m_maximum_recipient) remainder -= recipient->cAmount();
        }
        QScopedValueRollback<bool> updating{m_updating_maximum, true};
        m_maximum_recipient->amount()->setSatoshi(std::max(CAmount{0}, remainder));
    }

    if (!m_wallet || !m_send_recipients) {
        clearFeeEstimates();
        return;
    }

    ++m_fee_estimate_request_id;

    bool estimates_changed{!m_fee_estimates.isEmpty()};
    bool custom_estimate_changed{m_custom_fee_estimate.has_value()};
    if (estimates_changed) {
        m_fee_estimates.clear();
    }
    if (custom_estimate_changed) {
        m_custom_fee_estimate.reset();
    }
    if (estimates_changed || custom_estimate_changed) {
        Q_EMIT estimatedFeeChanged();
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }

    bool pending_changed{!m_fee_estimate_pending};
    if (pending_changed) {
        m_fee_estimate_pending = true;
        Q_EMIT feeEstimatePendingChanged();
    }

    if (estimates_changed || custom_estimate_changed || pending_changed) {
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }

    m_fee_estimation_timer->start();
}

void WalletQmlModel::requestFeeEstimatesNow()
{
    if (m_fee_job_running) { m_fee_refresh_requested = true; return; }
    m_fee_refresh_requested = false;
    if (!m_wallet || !m_send_recipients) {
        clearFeeEstimates();
        return;
    }
    const auto recipients = BuildRecipients(*m_send_recipients, m_maximum_recipient);
    const int remainder = m_send_recipients->recipients().indexOf(m_maximum_recipient);
    const auto request = ++m_fee_estimate_request_id;
    wallet::CCoinControl control{m_coin_control};
    ApplySelectedInputsPolicy(control);
    const auto change_type = control.m_change_type.value_or(cachedWalletState().default_address_type);
    const auto custom_rate = m_custom_fee_enabled ? ParseCustomFeeRatePerKvB(m_custom_fee_rate) : std::nullopt;
    const auto custom_target = m_pending_custom_target;
    if (!m_fee_estimate_pending) {
        m_fee_estimate_pending = true;
        Q_EMIT feeEstimatePendingChanged();
    }
    struct Result {
        QHash<unsigned int, SendFeePreview> estimates;
        std::optional<SendFeePreview> custom;
        QHash<unsigned int, CAmount> target_rates;
        CAmount available{0};
        bool selected{false};
    };
    m_fee_job_running = true;
    if (!backendExecutor()->submit(this, [wallet = walletHandle(), recipients, remainder, control, change_type, custom_rate, custom_target] {
        Result out;
        out.available = wallet->getAvailableBalance(control);
        out.selected = control.HasSelected();
        for (const auto target : CUSTOM_FEE_TARGETS) {
            auto target_control = control;
            target_control.m_feerate.reset();
            target_control.m_confirm_target = target;
            ApplyRegtestStaticFeeOverride(target_control);
            out.target_rates.insert(target, target_control.m_feerate ? target_control.m_feerate->GetFeePerK()
                : wallet->getMinimumFee(FEE_RATE_BASIS_VBYTES, target_control, nullptr, nullptr));
        }
        if (custom_target && !out.target_rates.contains(*custom_target)) {
            auto target_control = control;
            target_control.m_feerate.reset();
            target_control.m_confirm_target = *custom_target;
            out.target_rates.insert(*custom_target, wallet->getMinimumFee(FEE_RATE_BASIS_VBYTES, target_control, nullptr, nullptr));
        }
        if (recipients) {
            for (const auto target : STANDARD_FEE_TARGETS) {
                if (auto estimate = EstimatePreviewFee(*wallet, *recipients, control, change_type, target, remainder)) out.estimates.insert(target, *estimate);
            }
            if (custom_rate) out.custom = EstimateCustomPreviewFee(*wallet, *recipients, control, change_type, *custom_rate, remainder);
        }
        return out;
    }, [this, request](Result out) {
        m_fee_job_running = false;
        if (m_fee_refresh_requested) { requestFeeEstimatesNow(); return; }
        if (request != m_fee_estimate_request_id) return;
        if (out.selected) m_available_selected_balance = out.available;
        else m_available_fee_balance = out.available;
        m_fee_target_rates = std::move(out.target_rates);
        if (!m_pending_custom_target) updateCustomFeeTarget();
        applyFeeEstimates(out.estimates, out.custom, request);
        Q_EMIT sendAmountExhaustsBalanceChanged();
        if (m_pending_custom_target) {
            const auto target = *m_pending_custom_target;
            m_pending_custom_target.reset();
            const auto rate = m_fee_target_rates.value(target, 0);
            setCustomFeeRate(rate > 0 ? QString::number(rate / 1000.0, 'f', 3) : QString{});
            setFeeTargetBlocks(target);
        }
        if (m_maximum_recipient && m_maximum_recipient->address()->address().isEmpty()) {
            CAmount remainder = out.available;
            for (const auto* recipient : m_send_recipients->recipients()) if (recipient != m_maximum_recipient) remainder -= recipient->cAmount();
            QScopedValueRollback<bool> updating{m_updating_maximum, true};
            m_maximum_recipient->amount()->setSatoshi(std::max(CAmount{0}, remainder));
        }
    }, [this, request](std::exception_ptr) {
        m_fee_job_running = false;
        if (m_fee_refresh_requested) requestFeeEstimatesNow();
        else if (request == m_fee_estimate_request_id) clearFeeEstimates();
    })) {
        m_fee_job_running = false;
        clearFeeEstimates();
    }
}

void WalletQmlModel::applyFeeEstimates(const QHash<unsigned int, SendFeePreview>& estimates,
                                       const std::optional<SendFeePreview>& custom_estimate,
                                       const quint64 request_id)
{
    if (request_id != m_fee_estimate_request_id) {
        return;
    }

    bool estimates_changed{m_fee_estimates != estimates};
    bool custom_estimate_changed{m_custom_fee_estimate != custom_estimate};
    if (estimates_changed) {
        m_fee_estimates = estimates;
    }
    if (custom_estimate_changed) {
        m_custom_fee_estimate = custom_estimate;
    }
    updateMaximumAmount();
    if (estimates_changed || custom_estimate_changed) {
        Q_EMIT estimatedFeeChanged();
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }

    bool pending_changed{m_fee_estimate_pending};
    if (pending_changed) {
        m_fee_estimate_pending = false;
        Q_EMIT feeEstimatePendingChanged();
    }

    if (estimates_changed || custom_estimate_changed || pending_changed) {
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }
}

void WalletQmlModel::clearFeeEstimates()
{
    ++m_fee_estimate_request_id;

    bool estimates_changed{!m_fee_estimates.isEmpty()};
    bool custom_estimate_changed{m_custom_fee_estimate.has_value()};
    if (estimates_changed) {
        m_fee_estimates.clear();
    }
    if (custom_estimate_changed) {
        m_custom_fee_estimate.reset();
    }
    if (estimates_changed || custom_estimate_changed) {
        Q_EMIT estimatedFeeChanged();
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }

    bool pending_changed{m_fee_estimate_pending};
    if (pending_changed) {
        m_fee_estimate_pending = false;
        Q_EMIT feeEstimatePendingChanged();
    }

    if (estimates_changed || custom_estimate_changed || pending_changed) {
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }
}

bool WalletQmlModel::prepareTransaction()
{
    return prepareTransactionInternal(std::nullopt);
}

bool WalletQmlModel::prepareTransactionWithPassphrase(const QString& passphrase)
{
    return prepareTransactionInternal(std::optional<SecureString>{QmlUtil::SecureStringFromQString(passphrase)});
}

bool WalletQmlModel::prepareTransactionInternal(std::optional<SecureString> passphrase)
{
    if (m_transaction_pending) return false;
    clearTransactionStatus();
    if (!m_wallet || !m_send_recipients || !m_send_recipients->allValid()) {
        setTransactionStatus(m_send_recipients ? m_send_recipients->validationError() : tr("Enter at least one valid recipient to continue."));
        return false;
    }
    const auto recipients = BuildRecipients(*m_send_recipients, m_maximum_recipient);
    if (!recipients) {
        setTransactionStatus(tr("Enter at least one valid recipient to continue."));
        return false;
    }
    wallet::CCoinControl control{m_coin_control};
    ApplySelectedInputsPolicy(control);
    if (m_custom_fee_enabled) {
        const auto rate = ParseCustomFeeRatePerKvB(m_custom_fee_rate);
        if (!rate) return false;
        control.m_confirm_target.reset();
        control.m_feerate = CFeeRate{*rate};
    } else {
        control.m_feerate.reset();
        if (!control.m_confirm_target) control.m_confirm_target = DEFAULT_STANDARD_FEE_TARGET;
        ApplyRegtestStaticFeeOverride(control);
    }
    const int remainder = m_send_recipients->recipients().indexOf(m_maximum_recipient);
    QSet<QString> reviewed_addresses;
    for (const auto* recipient : m_send_recipients->recipients()) reviewed_addresses.insert(recipient->address()->address());
    const auto revision = m_send_draft_revision;
    const auto request = ++m_transaction_request_id;
    const auto target = feeTargetBlocks();
    const auto rate = estimatedFeeRate();
    setTransactionPending(true);
    const bool accepted = backendExecutor()->submit(this,
        [wallet = walletHandle(), recipients = *recipients, control, remainder, reviewed_addresses, passphrase = std::move(passphrase)]() mutable {
            TransactionResult out;
            bool relock{false};
            if (!UnlockTransaction(*wallet, passphrase, out, relock, tr("Enter your wallet password to prepare this transaction."))) return out;
            WalletRelockGuard guard{*wallet, [] {}, relock};
            CAmount total{0};
            for (const auto& recipient : recipients) total += recipient.nAmount;
            if (remainder < 0 && wallet->getAvailableBalance(control) < total) {
                out.error = control.HasSelected() ? tr("Selected inputs do not cover the amount plus fee")
                    : tr("The wallet does not have enough balance for this transaction.");
                return out;
            }
            const bool sign = !wallet->privateKeysDisabled();
            const auto created = remainder >= 0
                ? CreateSendAllTransaction(*wallet, recipients, control, remainder, sign)
                : wallet->createTransaction(recipients, control, sign, std::nullopt);
            if (!created) {
                out.error = LocalizedString(util::ErrorString(created));
                return out;
            }
            out.tx = created->tx;
            out.fee = created->fee;
            out.change_pos = created->change_pos;
            out.sweeps = !created->change_pos && TransactionSweepsAvailableFunds(*wallet, *out.tx, control);
            out.flow = ReadTransactionFlow(*wallet, out.tx, nullptr, reviewed_addresses);
            return out;
        }, [this, request, revision, target, rate, remainder](TransactionResult out) {
            setTransactionPending(false);
            requestWalletStateRefresh();
            if (request != m_transaction_request_id) return;
            if (revision != m_send_draft_revision) {
                setTransactionStatus(tr("The payment changed while preparing. Review it again."));
                Q_EMIT transactionPrepared(false);
                return;
            }
            if (!out.error.isEmpty() || !out.tx) {
                setTransactionStatus(out.error, out.needs_unlock);
                Q_EMIT transactionPrepared(false);
                return;
            }
            if (remainder >= 0 && m_maximum_recipient) {
                QScopedValueRollback<bool> updating{m_updating_maximum, true};
                m_maximum_recipient->amount()->setSatoshi(out.tx->vout[remainder].nValue);
            }
            delete m_current_transaction;
            m_current_transaction = new WalletQmlModelTransaction(m_send_recipients, this);
            m_current_transaction->setWtx(out.tx);
            if (!m_current_transaction->captureReviewedRecipients(*m_send_recipients)) {
                delete m_current_transaction;
                m_current_transaction = nullptr;
                setTransactionStatus(tr("Unable to match the prepared transaction's recipients."));
                Q_EMIT transactionPrepared(false);
                return;
            }
            m_current_transaction->setTransactionFee(out.fee);
            m_current_transaction->setReviewFeeDetails(target, rate);
            if (remainder >= 0) m_current_transaction->reassignAmounts(out.change_pos ? static_cast<int>(*out.change_pos) : -1);
            m_current_transaction->setDisplayUnit(m_display_unit);
            m_current_transaction_flow = std::move(out.flow);
            m_current_psbt.reset();
            m_current_transaction_source = CurrentTransactionSource::SendDraft;
            m_current_transaction_sweeps_wallet = out.sweeps;
            m_current_transaction_can_send = true;
            m_current_transaction_can_broadcast = false;
            m_current_transaction_review_message.clear();
            Q_EMIT currentTransactionChanged();
            Q_EMIT transactionPrepared(true);
        }, [this](std::exception_ptr error) { transactionFailed(error); Q_EMIT transactionPrepared(false); });
    if (!accepted) {
        setTransactionPending(false);
        setTransactionStatus(tr("The wallet is closing."));
    }
    return accepted;
}

void WalletQmlModel::setTransactionPending(bool pending)
{
    if (m_transaction_pending == pending) return;
    m_transaction_pending = pending;
    Q_EMIT transactionPendingChanged();
}

void WalletQmlModel::transactionFailed(std::exception_ptr error)
{
    setTransactionPending(false);
    try {
        if (error) std::rethrow_exception(error);
    } catch (const std::exception& exception) {
        setTransactionStatus(QString::fromUtf8(exception.what()));
    } catch (...) {
        setTransactionStatus(tr("The wallet operation failed."));
    }
    requestWalletStateRefresh();
}

void WalletQmlModel::approveExternalSignerTransaction()
{
    if (m_transaction_pending) return;
    if (!m_wallet || !m_current_transaction || !hasExternalSigner()) {
        Q_EMIT externalSignerApprovalFailed(tr("External signer not available."), true);
        return;
    }
    const auto tx = m_current_transaction->getWtx();
    if (!tx) return;
    const auto psbt = m_current_psbt ? std::optional{*m_current_psbt} : std::nullopt;
    const auto request = ++m_transaction_request_id;
    setTransactionPending(true);
    if (!backendExecutor()->submit(this, [wallet = walletHandle(), tx, psbt]() {
            TransactionResult out;
            CMutableTransaction unsigned_tx{*tx};
            ClearTransactionInputScripts(unsigned_tx);
            out.psbt = psbt.value_or(PartiallySignedTransaction{unsigned_tx});
            bool complete{false};
            auto error = wallet->fillPSBT({.sign = false, .bip32_derivs = true}, nullptr, *out.psbt, complete);
            if (!error && !complete) error = wallet->fillPSBT({.sign = true, .bip32_derivs = true}, nullptr, *out.psbt, complete);
            if (error) {
                out.signer_not_found = *error == common::PSBTError::EXTERNAL_SIGNER_NOT_FOUND;
                switch (*error) {
                case common::PSBTError::EXTERNAL_SIGNER_NOT_FOUND:
                    out.error = tr("External signer not found. Connect one device and try again.");
                    break;
                case common::PSBTError::EXTERNAL_SIGNER_FAILED:
                    out.error = tr("External signer failed to sign. Try again.");
                    break;
                default:
                    out.error = PsbtQmlModel::PsbtErrorText(*error);
                    break;
                }
                return out;
            }
            CMutableTransaction signed_tx;
            if (FinalizeAndExtractPSBT(*out.psbt, signed_tx)) out.tx = MakeTransactionRef(std::move(signed_tx));
            return out;
        }, [this, request](TransactionResult out) {
            setTransactionPending(false);
            requestWalletStateRefresh();
            if (request != m_transaction_request_id) return;
            if (!out.error.isEmpty()) {
                Q_EMIT externalSignerApprovalFailed(out.error, out.signer_not_found);
                return;
            }
            m_current_psbt = std::make_unique<PartiallySignedTransaction>(std::move(*out.psbt));
            m_current_transaction_can_send = bool(out.tx);
            m_current_transaction_can_broadcast = false;
            m_current_transaction_review_message = out.tx ? QString{} : tr("Signed on external signer. More signatures are required.");
            if (out.tx) m_current_transaction->setWtx(out.tx);
            Q_EMIT currentTransactionChanged();
            if (out.tx) Q_EMIT externalSignerApprovalSucceeded();
            else Q_EMIT externalSignerApprovalPartiallySucceeded();
        }, [this](std::exception_ptr error) {
            transactionFailed(error);
            Q_EMIT externalSignerApprovalFailed(m_transaction_error, false);
        })) {
        setTransactionPending(false);
        Q_EMIT externalSignerApprovalFailed(tr("The wallet is closing."), false);
    }
}

bool WalletQmlModel::sendTransaction()
{
    return sendTransactionInternal();
}

bool WalletQmlModel::sendTransactionWithPassphrase(const QString& passphrase)
{
    return sendTransactionInternal(std::optional<SecureString>{QmlUtil::SecureStringFromQString(passphrase)});
}

bool WalletQmlModel::broadcastCurrentTransaction()
{
    if (m_transaction_pending) return false;
    clearTransactionStatus();
    if (!m_node || !m_current_transaction || !m_current_psbt || !m_current_transaction_can_broadcast) {
        setTransactionStatus(tr("This transaction is not ready to broadcast."));
        return false;
    }
    setTransactionPending(true);
    const auto request = ++m_transaction_request_id;
    const bool accepted = backendExecutor()->submit(this, [node = m_node, psbt = *m_current_psbt]() mutable {
        TransactionResult out;
        const auto analysis = node::AnalyzePSBT(psbt);
        if (!analysis.fee || *analysis.fee < 0) {
            out.error = tr("The transaction fee is missing or invalid.");
            return out;
        }
        CMutableTransaction tx;
        if (!FinalizeAndExtractPSBT(psbt, tx)) {
            out.error = tr("This transaction is not fully signed.");
            return out;
        }
        out.tx = MakeTransactionRef(std::move(tx));
        std::string error_string;
        const auto error = node->broadcastTransaction(out.tx, node::DEFAULT_MAX_RAW_TX_FEE_RATE.GetFeePerK(), error_string);
        if (error != node::TransactionError::OK) {
            out.error = tr("Transaction broadcast failed: %1").arg(QString::fromStdString(common::TransactionErrorString(error).translated));
            if (!error_string.empty()) out.error += QStringLiteral(": ") + QString::fromStdString(error_string);
        }
        return out;
    }, [this, request](TransactionResult out) {
        setTransactionPending(false);
        requestWalletStateRefresh();
        if (request != m_transaction_request_id) return;
        if (!out.error.isEmpty()) {
            setTransactionStatus(out.error);
            Q_EMIT transactionSent(false);
            return;
        }
        m_current_transaction->setWtx(out.tx);
        m_current_psbt.reset();
        m_current_transaction_source = CurrentTransactionSource::None;
        m_current_transaction_can_send = false;
        m_current_transaction_can_broadcast = false;
        m_current_transaction_review_message.clear();
        clearSelectedCoins();
        Q_EMIT currentTransactionChanged();
        Q_EMIT transactionSent(true);
    }, [this](std::exception_ptr error) { transactionFailed(error); Q_EMIT transactionSent(false); });
    if (!accepted) {
        setTransactionPending(false);
        setTransactionStatus(tr("The wallet is closing."));
    }
    return accepted;
}

bool WalletQmlModel::sendTransactionInternal(std::optional<SecureString> passphrase)
{
    if (m_transaction_pending) return false;
    clearTransactionStatus();
    if (!m_wallet || !m_current_transaction) {
        setTransactionStatus(tr("Review a transaction before sending it."));
        return false;
    }
    if (!m_current_transaction_can_send) {
        setTransactionStatus(m_current_transaction_review_message.isEmpty()
            ? tr("This transaction cannot be sent from this wallet.") : m_current_transaction_review_message);
        return false;
    }
    const auto tx = m_current_transaction->getWtx();
    if (!tx) return false;
    const auto psbt = m_current_psbt ? std::optional{*m_current_psbt} : std::nullopt;
    const auto labels = m_current_transaction_source == CurrentTransactionSource::SendDraft
        ? m_current_transaction->recipientLabels() : QMap<QString, QString>{};
    const auto request = ++m_transaction_request_id;
    setTransactionPending(true);
    const bool accepted = backendExecutor()->submit(this,
        [wallet = walletHandle(), tx, psbt, labels, passphrase = std::move(passphrase)]() mutable {
            TransactionResult out;
            out.tx = tx;
            if (psbt) {
                auto working = *psbt;
                auto finalized = working;
                CMutableTransaction signed_tx;
                bool complete = FinalizeAndExtractPSBT(finalized, signed_tx);
                if (!complete) {
                    if (wallet->privateKeysDisabled() && !wallet->hasExternalSigner()) {
                        out.error = tr("This wallet cannot sign transactions.");
                        return out;
                    }
                    bool relock{false};
                    if (!UnlockTransaction(*wallet, passphrase, out, relock, tr("Enter your wallet password to send this transaction."))) return out;
                    WalletRelockGuard guard{*wallet, [] {}, relock};
                    size_t signed_inputs{0};
                    const auto error = wallet->fillPSBT({.sign = true, .bip32_derivs = true}, &signed_inputs, working, complete);
                    if (error) {
                        out.error = PsbtQmlModel::PsbtErrorText(*error);
                        return out;
                    }
                    if (!complete || !FinalizeAndExtractPSBT(working, signed_tx)) {
                        out.error = tr("Only PSBTs this wallet can fully sign are supported right now.");
                        return out;
                    }
                }
                out.tx = MakeTransactionRef(std::move(signed_tx));
            } else if (wallet->privateKeysDisabled() && !wallet->hasExternalSigner()) {
                out.error = tr("This wallet cannot sign transactions.");
                return out;
            }
            if (passphrase) QmlUtil::ClearSecureString(*passphrase);
            wallet->commitTransaction(out.tx, {}, {});
            for (auto it = labels.cbegin(); it != labels.cend(); ++it) {
                const auto destination = DecodeDestination(it.key().toStdString());
                if (!IsValidDestination(destination)) continue;
                std::string old_label;
                const bool exists = wallet->getAddress(destination, &old_label, nullptr);
                const auto label = it.value().toStdString();
                if (!exists || old_label != label) wallet->setAddressBook(destination, label,
                    exists ? std::nullopt : std::optional{wallet::AddressPurpose::SEND});
            }
            return out;
        }, [this, request](TransactionResult out) {
            setTransactionPending(false);
            requestWalletStateRefresh();
            if (request != m_transaction_request_id) return;
            if (!out.error.isEmpty()) {
                setTransactionStatus(out.error, out.needs_unlock);
                Q_EMIT transactionSent(false);
                return;
            }
            m_current_transaction->setWtx(out.tx);
            m_current_psbt.reset();
            m_current_transaction_source = CurrentTransactionSource::None;
            m_current_transaction_can_send = false;
            m_current_transaction_can_broadcast = false;
            m_current_transaction_review_message.clear();
            clearSelectedCoins();
            Q_EMIT currentTransactionChanged();
            Q_EMIT transactionSent(true);
        }, [this](std::exception_ptr error) { transactionFailed(error); Q_EMIT transactionSent(false); });
    if (!accepted) {
        setTransactionPending(false);
        setTransactionStatus(tr("The wallet is closing."));
    }
    return accepted;
}

WalletQmlModel::PsbtImportResult WalletQmlModel::importPsbtFromFile(const QString& path)
{
    if (m_transaction_pending || !m_imported_psbt_model) return PsbtImportResult::PsbtUnsupported;
    clearTransactionStatus();
    const auto request = ++m_transaction_request_id;
    const auto revision = m_send_draft_revision;
    setTransactionPending(true);
    if (!backendExecutor()->submit(this, [wallet = walletHandle(), path] {
            return ReadImportedTransaction(wallet.get(), path);
        }, [this, request, revision](ImportedTransactionResult out) {
            setTransactionPending(false);
            if (request != m_transaction_request_id) return;
            if (revision != m_send_draft_revision) out.error = tr("The payment changed while importing. Import the file again.");
            if (!out.error.isEmpty()) {
                m_imported_psbt_model->setError(out.error);
                Q_EMIT psbtImported(PsbtImportResult::PsbtUnsupported);
                return;
            }
            if (!out.known_txid.isEmpty()) {
                m_imported_psbt_model->setMatchedTxid(out.known_txid);
                Q_EMIT psbtImported(PsbtImportResult::TransactionAlreadyKnown);
                return;
            }
            m_send_recipients->clear();
            for (size_t i = 0; i < out.recipients.size(); ++i) {
                if (i > 0) m_send_recipients->add();
                auto* recipient = m_send_recipients->currentRecipient();
                recipient->setAddress(out.recipients[i].address);
                recipient->setLabel(out.recipients[i].label);
                recipient->amount()->setSatoshi(out.recipients[i].amount);
                recipient->setMessage(QString{});
            }
            m_send_recipients->setCurrentIndex(0);
            delete m_current_transaction;
            m_current_transaction = new WalletQmlModelTransaction(m_send_recipients, this);
            m_current_transaction->setWtx(out.tx);
            if (!m_current_transaction->captureReviewedRecipients(*m_send_recipients)) {
                delete m_current_transaction;
                m_current_transaction = nullptr;
                m_imported_psbt_model->setError(tr("Unable to match the imported transaction's recipients."));
                Q_EMIT psbtImported(PsbtImportResult::PsbtUnsupported);
                return;
            }
            m_current_transaction->setTransactionFee(out.fee);
            m_current_transaction->setDisplayUnit(m_display_unit);
            m_current_transaction_flow = std::move(out.flow);
            m_current_psbt = std::make_unique<PartiallySignedTransaction>(std::move(*out.psbt));
            m_current_transaction_source = CurrentTransactionSource::ImportedPsbt;
            m_current_transaction_can_send = out.can_send;
            m_current_transaction_can_broadcast = out.can_broadcast;
            m_current_transaction_review_message = out.message;
            m_imported_psbt_model->clear();
            Q_EMIT currentTransactionChanged();
            Q_EMIT psbtImported(out.can_send ? PsbtImportResult::WalletCanSign : PsbtImportResult::WalletCannotSign);
        }, [this](std::exception_ptr error) {
            transactionFailed(error);
            m_imported_psbt_model->setError(m_transaction_error);
            Q_EMIT psbtImported(PsbtImportResult::PsbtUnsupported);
        })) {
        setTransactionPending(false);
        m_imported_psbt_model->setError(tr("The wallet is closing."));
        return PsbtImportResult::PsbtUnsupported;
    }
    return PsbtImportResult::Pending;
}

QString WalletQmlModel::saveCurrentTransactionAsPsbt(const QString& path)
{
    if (m_transaction_pending) return tr("A wallet operation is already in progress.");
    if (!m_wallet || !m_current_transaction || !m_current_transaction->getWtx()) return tr("No transaction is prepared.");
    const auto tx = m_current_transaction->getWtx();
    const auto psbt = m_current_psbt ? std::optional{*m_current_psbt} : std::nullopt;
    const bool fill = !psbt || (m_current_transaction_source != CurrentTransactionSource::ImportedPsbt && m_current_transaction_can_send);
    setTransactionPending(true);
    if (!backendExecutor()->submit(this, [wallet = walletHandle(), tx, psbt, path, fill] {
            CMutableTransaction unsigned_tx{*tx};
            ClearTransactionInputScripts(unsigned_tx);
            auto working = psbt.value_or(PartiallySignedTransaction{unsigned_tx});
            if (fill) {
                bool complete{false};
                if (const auto error = wallet->fillPSBT({.sign = false, .bip32_derivs = true}, nullptr, working, complete)) {
                    return PsbtQmlModel::PsbtErrorText(*error);
                }
            }
            return PsbtQmlModel::SavePsbtToFile(working, path);
        }, [this](const QString& error) { setTransactionPending(false); Q_EMIT psbtSaved(error); },
        [this](std::exception_ptr error) { transactionFailed(error); Q_EMIT psbtSaved(m_transaction_error); })) {
        setTransactionPending(false);
        return tr("The wallet is closing.");
    }
    return {};
}

QVariantMap WalletQmlModel::currentTransactionFlow() const
{
    QVariantMap flow = m_current_transaction_flow;
    if (flow.isEmpty()) return flow;
    const auto unit = QmlBitcoinUnits::fromDisplayUnit(m_display_unit);
    const auto format_amount = [unit](CAmount amount) -> QString {
        return QmlBitcoinUnits::formatForDisplay(unit, amount) + QLatin1Char(' ')
            + QmlBitcoinUnits::label(unit, amount);
    };
    for (const QString& side : {QStringLiteral("inputs"), QStringLiteral("outputs")}) {
        QVariantList entries = flow.value(side).toList();
        for (QVariant& value : entries) {
            QVariantMap entry = value.toMap();
            if (entry.value(QStringLiteral("amountKnown")).toBool()) {
                entry.insert(QStringLiteral("amount"), format_amount(entry.value(QStringLiteral("amountSat")).toLongLong()));
            }
            value = entry;
        }
        flow.insert(side, entries);
    }
    if (flow.value(QStringLiteral("feeKnown")).toBool()) {
        flow.insert(QStringLiteral("feeAmount"), format_amount(flow.value(QStringLiteral("feeSat")).toLongLong()));
    }
    return flow;
}

void WalletQmlModel::discardCurrentTransaction()
{
    if (m_transaction_pending) return;
    ++m_transaction_request_id;
    m_current_transaction_flow.clear();
    const bool had_transaction_state{
        m_current_transaction ||
        m_current_psbt ||
        m_current_transaction_can_send ||
        m_current_transaction_can_broadcast ||
        !m_current_transaction_review_message.isEmpty()};

    delete m_current_transaction;
    m_current_transaction = nullptr;
    m_current_psbt.reset();
    m_current_transaction_source = CurrentTransactionSource::None;
    m_current_transaction_can_send = false;
    m_current_transaction_can_broadcast = false;
    m_current_transaction_review_message.clear();
    clearTransactionStatus();
    m_send_recipients->clear();

    if (had_transaction_state) {
        Q_EMIT currentTransactionChanged();
    }
}

interfaces::Wallet::CoinsList WalletQmlModel::listCoins() const
{
    if (!m_wallet) {
        return {};
    }
    return cachedWalletState().coins;
}

bool WalletQmlModel::lockCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return backendExecutor()->submit(this, [wallet = walletHandle(), output] { return wallet->lockCoin(output, true); },
        [this](bool) { requestWalletStateRefresh(); });
}

bool WalletQmlModel::unlockCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return backendExecutor()->submit(this, [wallet = walletHandle(), output] { return wallet->unlockCoin(output); },
        [this](bool) { requestWalletStateRefresh(); });
}

bool WalletQmlModel::isLockedCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return cachedWalletState().locked_coins.contains(output);
}

void WalletQmlModel::listLockedCoins(std::vector<COutPoint>& outputs)
{
    if (!m_wallet) {
        return;
    }
    outputs.assign(cachedWalletState().locked_coins.begin(), cachedWalletState().locked_coins.end());
}

void WalletQmlModel::selectCoin(const COutPoint& output)
{
    const bool was_selected{m_coin_control.IsSelected(output)};
    m_coin_control.Select(output);
    if (!was_selected) {
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }
    scheduleFeeEstimates();
}

void WalletQmlModel::unselectCoin(const COutPoint& output)
{
    const bool was_selected{m_coin_control.IsSelected(output)};
    m_coin_control.UnSelect(output);
    if (was_selected) {
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }
    scheduleFeeEstimates();
}

bool WalletQmlModel::isSelectedCoin(const COutPoint& output)
{
    return m_coin_control.IsSelected(output);
}

std::vector<COutPoint> WalletQmlModel::listSelectedCoins() const
{
    return m_coin_control.ListSelected();
}

void WalletQmlModel::clearSelectedCoins()
{
    setMaximumRecipient(nullptr);
    if (m_coin_control.HasSelected()) setSelectedCoins({});
}

void WalletQmlModel::setSelectedCoins(const std::vector<COutPoint>& outputs)
{
    // Restoring a cancelled coin-picker edit preserves the maximum recipient.
    m_coin_control.UnSelectAll();
    for (const auto& output : outputs) m_coin_control.Select(output);
    if (m_coins_list_model) m_coins_list_model->refreshSelection();
    Q_EMIT sendAmountExhaustsBalanceChanged();
    scheduleFeeEstimates();
}

unsigned int WalletQmlModel::feeTargetBlocks() const
{
    return m_coin_control.m_confirm_target.value_or(DEFAULT_STANDARD_FEE_TARGET);
}

void WalletQmlModel::setFeeTargetBlocks(unsigned int target_blocks)
{
    if (m_coin_control.m_confirm_target != target_blocks) {
        ++m_send_draft_revision;
        m_coin_control.m_confirm_target = target_blocks;
        updateMaximumAmount();
        Q_EMIT feeTargetBlocksChanged();
        Q_EMIT estimatedFeeChanged();
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }
}

void WalletQmlModel::setCustomFeeEnabled(const bool enabled)
{
    if (m_custom_fee_enabled != enabled) {
        m_custom_fee_enabled = enabled;
        Q_EMIT customFeeEnabledChanged();
        Q_EMIT estimatedFeeChanged();
        Q_EMIT sendAmountExhaustsBalanceChanged();
        scheduleFeeEstimates();
    }
}

void WalletQmlModel::setCustomFeeRate(const QString& fee_rate)
{
    const QString trimmed_fee_rate = fee_rate.trimmed();
    const bool was_valid = customFeeRateValid();

    if (m_custom_fee_rate == trimmed_fee_rate) {
        return;
    }

    m_custom_fee_rate = trimmed_fee_rate;
    m_custom_fee_estimate.reset();

    m_pending_custom_target.reset();
    updateCustomFeeTarget();

    Q_EMIT customFeeRateChanged();
    if (was_valid != customFeeRateValid()) {
        Q_EMIT customFeeRateValidChanged();
    }
    Q_EMIT estimatedFeeChanged();
    Q_EMIT sendAmountExhaustsBalanceChanged();
    scheduleFeeEstimates();
}

void WalletQmlModel::updateCustomFeeTarget()
{
    if (const auto requested_rate = ParseCustomFeeRatePerKvB(m_custom_fee_rate); requested_rate && !m_fee_target_rates.isEmpty()) {
        const auto first = m_fee_target_rates.value(CUSTOM_FEE_TARGETS.front());
        if (std::any_of(CUSTOM_FEE_TARGETS.begin(), CUSTOM_FEE_TARGETS.end(), [this, first](unsigned int target) {
                return m_fee_target_rates.value(target) != first;
            })) {
            unsigned int inferred_target = CUSTOM_FEE_TARGETS.back();
            for (const auto target : CUSTOM_FEE_TARGETS) {
                const auto rate = m_fee_target_rates.value(target);
                if (rate > 0 && *requested_rate >= rate) { inferred_target = target; break; }
            }
            setFeeTargetBlocks(inferred_target);
        }
    }
}

void WalletQmlModel::setDisplayUnit(int unit)
{
    if (unit != m_display_unit) {
        m_display_unit = unit;
        if (m_address_list_model) {
            m_address_list_model->setDisplayUnit(unit);
        }
        if (m_current_transaction) {
            m_current_transaction->setDisplayUnit(unit);
            Q_EMIT currentTransactionChanged();
        }
        Q_EMIT balanceChanged();
        Q_EMIT displayUnitChanged(unit);
    }
}

void WalletQmlModel::subscribeToWalletSignals()
{
    if (!m_wallet) return;
    const QPointer<WalletQmlModel> guard{this};
    m_backend_executor->submit(this, [wallet = m_wallet, guard, bridge = m_notification_bridge] {
        auto status = wallet->handleStatusChanged([guard, bridge] {
            QMetaObject::invokeMethod(bridge.get(), [guard] {
                if (guard) guard->requestWalletStateRefresh();
            }, Qt::QueuedConnection);
        });
        auto address = wallet->handleAddressBookChanged([guard, bridge](const CTxDestination&, const std::string&, bool, wallet::AddressPurpose, ChangeType) {
            QMetaObject::invokeMethod(bridge.get(), [guard] {
                if (guard) guard->requestWalletStateRefresh();
            }, Qt::QueuedConnection);
        });
        auto transaction = wallet->handleTransactionChanged([guard, bridge](const Txid& txid, ChangeType change) {
            QMetaObject::invokeMethod(bridge.get(), [guard, txid, change] {
                if (!guard || guard->m_stopping) return;
                guard->requestWalletStateRefresh();
                Q_EMIT guard->transactionChanged(QString::fromStdString(txid.ToString()), change);
            }, Qt::QueuedConnection);
        });
        auto unload = wallet->handleUnload([guard, bridge] {
            QMetaObject::invokeMethod(bridge.get(), [guard] {
                if (guard && !guard->m_stopping) Q_EMIT guard->walletUnloaded();
            }, Qt::QueuedConnection);
        });
        return std::make_tuple(std::move(status), std::move(address), std::move(transaction), std::move(unload));
    }, [this](auto handlers) {
        std::tie(m_handler_status_changed, m_handler_address_list_changed, m_handler_transaction_changed, m_handler_unload) = std::move(handlers);
    }, [this](std::exception_ptr) {
        m_wallet_subscription_error = tr("Wallet notifications could not be registered. Reopen the wallet to try again.");
        m_wallet_state_error = m_wallet_subscription_error;
        Q_EMIT walletStateChanged();
    });
}

void WalletQmlModel::refreshSecurityState()
{
    requestWalletStateRefresh();
}

bool WalletQmlModel::unlockForAction(std::optional<SecureString>& passphrase, bool& relock)
{
    relock = false;
    if (!m_wallet) {
        if (passphrase.has_value()) {
            QmlUtil::ClearSecureString(*passphrase);
            passphrase.reset();
        }
        return true;
    }
    if (!passphrase.has_value()) {
        // Either the wallet is unlocked already (action proceeds), or it isn't
        // and the caller asked for an unlock-less attempt — let the action fail
        // downstream rather than blocking here.
        return true;
    }

    const auto result{TryUnlockWithPassphrase(*m_wallet, *passphrase)};
    passphrase.reset();
    switch (result) {
    case WalletUnlockResult::IncorrectPassphrase:
        setTransactionStatus(tr("The wallet password you entered was incorrect."));
        return false;
    case WalletUnlockResult::AlreadyUnlocked:
        return true;
    case WalletUnlockResult::UnlockedNowRelockRequired:
        relock = true;
        refreshSecurityState();
        return true;
    }
    return false;
}

void WalletQmlModel::clearTransactionStatus()
{
    setTransactionStatus(QString());
}

void WalletQmlModel::setTransactionStatus(const QString& error, bool needs_unlock)
{
    if (m_transaction_error != error) {
        m_transaction_error = error;
        Q_EMIT transactionErrorChanged();
    }
    if (m_transaction_needs_unlock != needs_unlock) {
        m_transaction_needs_unlock = needs_unlock;
        Q_EMIT transactionNeedsUnlockChanged();
    }
}

void WalletQmlModel::setSettingsError(const QString& error)
{
    if (m_settings_error != error) {
        m_settings_error = error;
        Q_EMIT settingsErrorChanged();
    }
}

QString WalletQmlModel::persistedReceiveAddressTypeKey() const
{
    return QStringLiteral("receiveAddressTypes/%1").arg(name());
}
