
// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletqmlmodel.h>
#include <qml/walletsend.h>
#include <qml/wallettransaction.h>
#include <stdexcept>
#include <qml/walletprepare.h>
#include <qml/retainedvalue.h>
#include <qml/guipresentation.h>
#include <qml/psbtwork.h>
#include <qml/models/signertransaction.h>
#include <qml/models/psbtfee.h>
#include <qml/backendexecutor.h>
#include <qml/walletbalanceadapter.h>

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
#include <wallet/types.h>
#include <wallet/wallet.h> // Public CRecipient value required by interfaces::Wallet.

#include <QDateTime>
#include <QMetaObject>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QSettings>
#include <QQmlEngine>
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
#include <tuple>
#include <variant>
#include <vector>

namespace {
constexpr unsigned int DEFAULT_STANDARD_FEE_TARGET{6};
constexpr int FEE_ESTIMATE_DEBOUNCE_MS{250};
constexpr unsigned int FEE_RATE_BASIS_VBYTES{1000};
constexpr std::array<unsigned int, 3> STANDARD_FEE_TARGETS{2, DEFAULT_STANDARD_FEE_TARGET, 10};
constexpr std::array<unsigned int, 7> CUSTOM_FEE_TARGETS{2, 3, 4, 6, 10, 25, 50};
const QRegularExpression CUSTOM_FEE_RATE_PATTERN{QStringLiteral(R"(^[0-9]+(?:\.[0-9]{0,3})?$)")};

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

void ApplySelectedInputsPolicy(wallet::CCoinControl& coin_control)
{
    // Manually selected coins should be the only wallet inputs used.
    coin_control.m_allow_other_inputs = !coin_control.HasSelected();
}

std::optional<CAmount> ParseCustomFeeRatePerKvB(const QString& custom_fee_rate, CAmount minimum_fee_rate_per_kvb)
{
    if (custom_fee_rate.size() > 32) return std::nullopt;
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
    if (fee_rate_per_kvb < minimum_fee_rate_per_kvb) {
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

std::optional<bool> TransactionSweepsAvailableFunds(interfaces::Wallet& wallet, const CTransaction& transaction,
                                     const wallet::CCoinControl& control)
{
    return wallettransaction::Sweeps(wallet, transaction, control);
}

util::Result<wallet::CreatedTransactionResult> CreateSendAllTransaction(
    interfaces::Wallet& wallet, const std::vector<wallet::CRecipient>& recipients,
    const wallet::CCoinControl& control, int remainder, bool sign)
{
    return wallettransaction::Create(wallet, recipients, control, remainder, sign);
}

std::optional<SendFeePreview> EstimateInsufficientSelectedInputsFee(
    interfaces::Wallet& wallet, const std::vector<wallet::CRecipient>& recipients,
    const wallet::CCoinControl& control)
{
    const auto probe = wallettransaction::InsufficientFeeProbe(wallet, recipients, control);
    if (!probe) return {};
    const auto rate = control.m_feerate ? control.m_feerate->GetFeePerK()
        : wallet.getMinimumFee(FEE_RATE_BASIS_VBYTES, control, nullptr, nullptr);
    return SendFeePreview{probe->fee, static_cast<int>(probe->tx->vin.size()), rate, std::nullopt, false};
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
    if (!result->tx || (remainder_index >= 0 && static_cast<size_t>(remainder_index) >= result->tx->vout.size())) {
        throw std::runtime_error{"The fee preview transaction is incomplete"};
    }
    const auto sweeps = result->change_pos ? std::optional<bool>{false}
        : TransactionSweepsAvailableFunds(wallet, *result->tx, coin_control);
    // Propagate uncertainty to the Native read owner, not the fee-policy
    // fallback. An unknown sweep must never suppress all-funds confirmation.
    if (!sweeps) throw std::runtime_error{"The wallet's remaining spendable balance could not be confirmed"};
    return SendFeePreview{result->fee, static_cast<int>(result->tx->vin.size()), rate,
        remainder_index >= 0 ? std::make_optional(result->tx->vout[remainder_index].nValue) : std::nullopt, *sweeps};
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
    // An explicit custom rate may be below the wallet's automatic minimum.
    coin_control.fOverrideFeeRate = true;
    ApplyPreviewChangeDestination(coin_control, preview_change_type);

    if (const auto fee = TryPreviewFee(wallet, recipients, coin_control, remainder_index)) {
        return fee;
    }

    return std::nullopt;
}

bool ReadyForFeePreview(const std::shared_ptr<const senddraft::Snapshot>& draft, quint64 maximum)
{
    if (!draft || draft->recipients.empty() || draft->duplicate_destinations || draft->pending_count) return false;
    if (!maximum) return draft->all_valid && draft->amount_total_valid;
    const auto position = draft->positions.find(maximum);
    if (position == draft->positions.end()) return false;
    const auto& recipient = draft->recipients[position->second];
    return recipient.address_valid && draft->invalid_count == (recipient.valid ? 0 : 1);
}


QString OutputTypeId(OutputType type)
{
    return QString::fromStdString(FormatOutputType(type));
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

} // namespace

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node, QObject* parent)
    : WalletQmlModel(std::move(wallet), node, FeeReadServices{}, parent)
{
}

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node,
                             FeeReadServices fee_services, QObject* parent)
    : WalletQmlModel(std::move(wallet), node, std::move(fee_services), psbtfee::Services{}, parent)
{
}

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node,
                             FeeReadServices fee_services, psbtfee::Services psbt_services, QObject* parent)
    : WalletQmlModel(std::move(wallet), node, std::move(fee_services), std::move(psbt_services), backup::Services{}, parent)
{
}

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node,
                             FeeReadServices fee_services, psbtfee::Services psbt_services, backup::Services backup_services,
                             QObject* parent)
    : WalletQmlModel(SharedWallet{std::move(wallet)}, node,
                     std::move(fee_services), std::move(psbt_services), std::move(backup_services), parent)
{
}

WalletQmlModel::WalletQmlModel(SharedWallet wallet, interfaces::Node* node,
                             FeeReadServices fee_services, psbtfee::Services psbt_services, backup::Services backup_services,
                             QObject* parent)
    : QObject(parent)
    , m_admission_closed(std::move(wallet.shutdown_gate))
    , m_opening_preferences(std::move(wallet.preferences))
    , m_opening_native_events(std::move(wallet.native_events))
    , m_fee_policy(std::move(wallet.fee_policy))
    , m_name(std::move(wallet.captured_name))
    , m_balance_scheduler(std::move(wallet.observations))
    , m_wallet(std::move(wallet.wallet))
    , m_node(node)
    , m_backup_services(std::move(backup_services))
    , m_psbt_fee_services(std::move(psbt_services))
    , m_custom_psbt_lookup(bool(m_psbt_fee_services.lookup))
    , m_fee_read_services(std::move(fee_services))
{
    m_receive_requests = new ReceiveRequestHistoryModel(this);
    m_receive_payment_poll_timer.setInterval(1000);
    connect(&m_receive_payment_poll_timer, &QTimer::timeout, this, &WalletQmlModel::pollUnconfirmedReceiveRequestPayments);
    m_address_list_model = new AddressListModel(this);
    m_bump_transaction_model = new BumpTransactionModel(m_wallet.get(), this);
    m_bump_transaction_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    connect(m_send_recipients, &SendRecipientsListModel::totalAmountChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    connect(m_send_recipients, &SendRecipientsListModel::validationChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    m_sign_verify_message_model = new SignVerifyMessageModel(m_wallet.get(), this);
    m_sign_verify_message_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_current_payment_request = new PaymentRequest(this);
    m_receiving_address = new PaymentRequest(this);
    m_detail_payment_request = new PaymentRequest(this);
    m_imported_psbt_model = new PsbtQmlModel(m_wallet.get(), m_node, this);
    initializeBackend();
    initializeOpening();
    const QPointer<WalletQmlModel> receipt_observer{this};
    if (m_opening) {
        const PaymentRequest::PayloadPreparer prepare_payload = [opening = m_opening, bridge = m_notification_bridge](auto input, auto done) {
            const auto deliver = [bridge, done = std::move(done)](QString payload) {
                QMetaObject::invokeMethod(bridge.get(), [done, payload = std::move(payload)] { done(payload); }, Qt::QueuedConnection);
            };
            constexpr size_t MAX_INPUT_BYTES{65'536};
            size_t bytes{sizeof(input) + 256};
            for (const auto* text : {&input.address, &input.label, &input.message}) {
                if (static_cast<size_t>(text->size()) > (MAX_INPUT_BYTES - bytes) / sizeof(QChar)) { deliver({}); return; }
                bytes += static_cast<size_t>(text->size()) * sizeof(QChar);
            }
            if (!opening->postDomain([input = std::move(input), deliver] {
                deliver(walletrequests::BuildBitcoinUri(input.address, input.amount, input.label, input.message));
            }, bytes * 4)) deliver({});
        };
        for (auto* request : {m_current_payment_request, m_receiving_address, m_detail_payment_request}) request->setPayloadPreparer(prepare_payload);
        m_receipts = std::make_shared<walletreceipts::Reader>(m_opening,
            [bridge = m_notification_bridge, receipt_observer] {
                QMetaObject::invokeMethod(bridge.get(), [receipt_observer] {
                    if (receipt_observer && receipt_observer->observationsAllowed()) receipt_observer->publishReceipts();
                }, Qt::QueuedConnection);
            });
        const auto weak_reader = std::weak_ptr<walletreceipts::Reader>{m_receipts};
        m_request_store = std::make_shared<walletrequests::Store>(m_opening,
            [bridge = m_notification_bridge, receipt_observer] {
                QMetaObject::invokeMethod(bridge.get(), [receipt_observer] {
                    if (receipt_observer && receipt_observer->observationsAllowed()) receipt_observer->publishRequestHistory();
                }, Qt::QueuedConnection);
            }, [weak_reader](QSet<QString> addresses) {
                if (const auto reader = weak_reader.lock()) reader->reload(std::move(addresses));
            }, QDateTime::currentDateTimeUtc, [weak_reader] {
                const auto reader = weak_reader.lock(); return reader && reader->settled();
            }, walletrequests::SettingsReservationStorage().clear, walletrequests::SettingsReservationStorage());
        m_receipts->onSnapshot([weak = std::weak_ptr<walletrequests::Store>{m_request_store}](auto snapshot) {
            if (const auto store = weak.lock()) store->observed(snapshot);
        });
        m_opening->onTransactions([weak = std::weak_ptr<walletreceipts::Reader>{m_receipts}](uint64_t revision, std::map<Txid, ChangeType> changes, bool rescan) {
            if (const auto owner = weak.lock()) owner->notice(revision, std::move(changes), rescan);
        });
    }
    connect(m_receiving_address, &PaymentRequest::addressChanged, this, &WalletQmlModel::refreshReceiveRequestPayments);
    reloadReceiveRequests();
    initializeFeeEstimator();
    initializePsbtFeeReader();
    initializeBalanceReader();
    m_sign_verify_message_model->setOpening(m_opening, {m_opening->scheduler(), m_fee_read_services.post});
    m_bump_transaction_model->setOpening(m_opening, {m_opening->scheduler(), m_fee_read_services.post});
    m_imported_psbt_model->configureSession(m_opening, m_psbt_fee_services);
}

WalletQmlModel::WalletQmlModel(interfaces::Node* node, QObject* parent)
    : QObject(parent)
    , m_node(node)
{
    m_address_list_model = new AddressListModel(this);
    m_bump_transaction_model = new BumpTransactionModel(nullptr, this);
    m_bump_transaction_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    connect(m_send_recipients, &SendRecipientsListModel::totalAmountChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    connect(m_send_recipients, &SendRecipientsListModel::validationChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    m_sign_verify_message_model = new SignVerifyMessageModel(nullptr, this);
    m_sign_verify_message_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_current_payment_request = new PaymentRequest(this);
    m_receiving_address = new PaymentRequest(this);
    m_detail_payment_request = new PaymentRequest(this);
    m_receive_requests = new ReceiveRequestHistoryModel(this);
    m_imported_psbt_model = new PsbtQmlModel(nullptr, m_node, this);
    initializeBackend();
    initializeFeeEstimator();
    initializePsbtFeeReader();
    m_sign_verify_message_model->setOpening({}, {m_backend_executor->scheduler(), m_fee_read_services.post});
    m_bump_transaction_model->setOpening({}, {m_backend_executor->scheduler(), m_fee_read_services.post});
    m_imported_psbt_model->configureSession({}, m_psbt_fee_services);
}

WalletQmlModel::WalletQmlModel(QObject* parent)
    : WalletQmlModel(nullptr, parent)
{
}

struct WalletQmlModel::ReviewRetirement : std::enable_shared_from_this<ReviewRetirement> {
    std::map<WalletQmlModelTransaction*, QPointer<WalletQmlModelTransaction>> reviews;
    std::shared_ptr<QObject> bridge{presentation::MakeGuiObject()};
    QPointer<WalletQmlModel> observer;
    std::function<void(std::function<void()>)> post{presentation::ReliablePost(
        [bridge = bridge](auto callback) { QMetaObject::invokeMethod(bridge.get(), std::move(callback), Qt::QueuedConnection); })};
    bool closing{false}, scheduled{false};
    void add(WalletQmlModelTransaction* review) {
        reviews.emplace(review, review);
        // This registry retires views in bounded turns. QObject parenting
        // would also delete an in-flight view if its destroyed handler closes
        // the facade and releases the bridge before that destructor returns.
        review->setParent(nullptr);
        QQmlEngine::setObjectOwnership(review,QQmlEngine::CppOwnership);
        const auto weak = weak_from_this();
        QObject::connect(review, &QObject::destroyed, bridge.get(), [weak, review] { if (const auto state = weak.lock()) state->reviews.erase(review); });
        if (closing) enqueue();
    }
    void enqueue() {
        if (std::exchange(scheduled, true)) return;
        const auto self = shared_from_this();
        post([self] { self->scheduled = false; self->pump(); });
    }
    void pump() {
        if (!closing) return;
        for (int budget = 0; budget < 32 && !reviews.empty(); ++budget) {
            const auto review = reviews.begin()->second; reviews.erase(reviews.begin());
            if (review) { review->closeAdmission(); delete review; }
        }
        if (!reviews.empty()) { enqueue(); return; }
        if (observer) { observer->m_reviews_drained = true; observer->checkShutdownFinished(); }
    }
    void close() { closing = true; enqueue(); }
};
void WalletQmlModel::setReviewRetirementPostForTesting(std::function<void(std::function<void()>)> post)
{
    if (m_admission_closed || !post) return;
    if (!m_review_retirement) { m_review_retirement = std::make_shared<ReviewRetirement>(); m_review_retirement->observer = this; }
    m_review_retirement->post = presentation::ReliablePost(std::move(post));
}
void WalletQmlModel::registerReview(WalletQmlModelTransaction* review)
{
    if (!m_review_retirement) { m_review_retirement = std::make_shared<ReviewRetirement>(); m_review_retirement->observer = this; }
    m_reviews_drained = false; m_review_retirement->add(review);
    if (m_admission_closed) m_review_retirement->close();
}

WalletQmlModel::~WalletQmlModel()
{
    blockSignals(true);
    beginShutdown();
    if (m_fee_read_services.timer) {
        m_fee_read_services.timer->stop();
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

TransactionActivityModel* WalletQmlModel::transactionActivityModel()
{
    // Instantiate when the activity screen first asks for it.
    if (!m_transaction_activity_model) m_transaction_activity_model = new TransactionActivityModel(this);
    return m_transaction_activity_model;
}

void WalletQmlModel::setNode(interfaces::Node* node)
{
    const QPointer<WalletQmlModel> observer{this};
    const bool recheck_fee = m_psbt_fee_pending || (m_current_psbt &&
        (!m_psbt_fee_proof || !m_psbt_fee_proof->amount.has_value()));
    m_psbt_fee_request.reset();
    m_psbt_fee_pending = false;
    m_node = node;
    if (!m_custom_psbt_lookup) m_psbt_fee_services.lookup = psbtfee::NodeLookup(node);
    if (m_imported_psbt_model) {
        m_imported_psbt_model->setFeeServices(m_psbt_fee_services);
        m_imported_psbt_model->setNode(node);
    }
    if (observer && recheck_fee && observationsAllowed() && m_current_psbt) requestPsbtFeeProof();
}

void WalletQmlModel::initializeOpening()
{
    if (!m_wallet) return;
    m_opening_drained = false;
    m_available_published = true;
    const QPointer<WalletQmlModel> observer{this};
    auto preferences = std::move(m_opening_preferences);
    if (!preferences.read && m_node) preferences.read = [node = m_node](const std::string& name) {
        const auto values = node->getPersistentSetting("qml_receive_address_types");
        const auto& value = values[name];
        return value.isStr() ? value.get_str() : std::string{};
    };
    // A Node void write cannot establish persistence. Production injects the
    // public Chain adapter, which supplies atomic update and a disk outcome.
    m_opening = std::make_shared<walletopening::Session>(
        m_balance_scheduler ? m_balance_scheduler : m_backend_executor->scheduler(),
        m_backend_executor->scheduler(), m_unload_requested,
        [bridge = m_notification_bridge, observer] {
            QMetaObject::invokeMethod(bridge.get(), [observer] { if (observer) observer->publishOpening(); }, Qt::QueuedConnection);
        }, std::move(preferences), std::chrono::steady_clock::now, m_admission_closed.shared(), m_opening_native_events);
    auto started = m_opening->start(m_wallet);
    if (started.borrower) m_wallet = std::move(started.borrower);
    if (!started) m_opening->retire();
    if (started && m_address_list_model) m_address_list_model->refresh();
    if (started && m_coins_list_model) m_coins_list_model->update();
}

QString WalletQmlModel::closeQuestionToken() const
{
    return m_close_question ? m_opening_token + QLatin1Char(':') + QString::number(m_close_question->number) : QString{};
}

void WalletQmlModel::requestClose()
{
    if (m_opening && available()) m_opening->requestClose();
}

void WalletQmlModel::answerClose(const QString& token, bool accept)
{
    if (m_opening && m_close_question && token == closeQuestionToken()) m_opening->answerClose(*m_close_question, accept);
}

void WalletQmlModel::publishOpening()
{
    auto update = m_opening->take();
    if (!update) return;
    const QPointer<WalletQmlModel> guard{this};
    if (update->phase == walletopening::Phase::Closed) m_opening_drained = true;
    if (m_stopping || m_admission_closed) { checkShutdownFinished(); return; }
    if (update->metadata && available()) {
        auto metadata = std::move(*update->metadata);
        const QString name = QString::fromStdString(metadata.name);
        const bool name_changed = m_name != name;
        const bool security_changed = m_is_encrypted != metadata.encrypted || m_is_locked != metadata.locked;
        m_name = name;
        m_metadata_known = true;
        m_private_keys_disabled = metadata.private_keys_disabled;
        m_external_signer = metadata.external_signer;
        m_is_encrypted = metadata.encrypted;
        m_is_locked = metadata.locked;
        m_can_get_addresses = metadata.can_get_addresses;
        m_taproot = metadata.taproot;
        m_receive_preference = QString::fromStdString(metadata.receive_preference);
        if (name_changed) { Q_EMIT nameChanged(); if (!guard || m_stopping) return; }
        Q_EMIT openingMetadataChanged(); if (!guard || m_stopping) return;
        if (security_changed) { Q_EMIT securityStateChanged(); if (!guard || m_stopping) return; }
    }
    if (update->preference_saved && update->preference_revision == m_opening->preferenceRevision()) {
        m_receive_preference_pending = false;
        m_receive_preference_error = *update->preference_saved ? QString{} : tr("The address preference could not be confirmed.");
        Q_EMIT receivePreferenceChanged(); if (!guard || m_stopping) return;
    }
    const bool question_changed = bool(m_close_question) != bool(update->question)
        || (m_close_question && update->question && m_close_question->number != update->question->number);
    m_close_question = std::move(update->question);
    if (question_changed) { Q_EMIT closeQuestionChanged(); if (!guard || m_stopping) return; }
    if (update->unloaded || (update->failed && !available())) {
        if (std::exchange(m_available_published, false)) { Q_EMIT availableChanged(); if (!guard || m_stopping) return; }
        Q_EMIT walletUnloaded(); if (!guard || m_stopping) return;
        beginShutdown();
        return;
    }
    if (update->phase != walletopening::Phase::Open) {
        if (std::exchange(m_available_published, false)) { Q_EMIT availableChanged(); if (!guard || m_stopping) return; }
        if (!m_close_confirmed) { m_close_confirmed = true; Q_EMIT closeConfirmed(); if (!guard || m_stopping) return; }
        if (observationsAllowed()) beginShutdown();
        return;
    }
    if (update->addresses) {
        Q_EMIT addressInventoryChanged(); if (!guard || !observationsAllowed()) return;
        Q_EMIT addressListChanged(); if (!guard || !observationsAllowed()) return;
    }
    const bool transactions_changed = !update->transactions.empty() || update->rescan;
    if (transactions_changed) {
        // Backend ingress normally arrives first. The revision also makes
        // startup replay and this presentation handoff idempotent.
        m_receipts->notice(update->transaction_revision, std::move(update->transactions), update->rescan);
        // Ready can reach GUI before this Session mailbox. Acknowledge the
        // current revision after taking it, without waiting for another poll.
        m_receive_reconciliation_pending = !m_receipts->settled();
        if (!m_receive_reconciliation_pending) m_opening->acknowledgeTransactions();
        Q_EMIT receiveRequestReconciliationPendingChanged(); if (!guard || m_stopping) return;
    }
    if (update->status || transactions_changed) {
        refreshBalance();
        requestSendFacts();
        if (!guard || m_stopping) return;
        Q_EMIT walletContentsChanged(); if (!guard || m_stopping) return;
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }
}

void WalletQmlModel::initializeBackend()
{
    connect(this, &WalletQmlModel::currentTransactionChanged, this, &WalletQmlModel::requestCurrentFlow);
    connect(this, &WalletQmlModel::displayUnitChanged, this, &WalletQmlModel::requestCurrentFlow);
    connect(m_send_recipients, &QAbstractItemModel::dataChanged, this, [this] { invalidatePreparation(); });
    connect(m_send_recipients, &SendRecipientsListModel::recipientDraftChanged, this, &WalletQmlModel::invalidatePreparation);
    connect(m_send_recipients, &QAbstractItemModel::rowsInserted, this, [this] { invalidatePreparation(); });
    connect(m_send_recipients, &QAbstractItemModel::rowsRemoved, this, [this] { invalidatePreparation(); });
    connect(m_send_recipients, &QAbstractItemModel::modelReset, this, &WalletQmlModel::invalidatePreparation);
    connect(this, &WalletQmlModel::maximumRecipientChanged, this, &WalletQmlModel::invalidatePreparation);
    connect(this, &WalletQmlModel::customFeeEnabledChanged, this, &WalletQmlModel::invalidatePreparation);
    connect(this, &WalletQmlModel::customFeeRateChanged, this, &WalletQmlModel::invalidatePreparation);
    connect(this, &WalletQmlModel::feeTargetBlocksChanged, this, &WalletQmlModel::invalidatePreparation);
    connect(this, &WalletQmlModel::currentTransactionChanged, this, [this] {
        if (m_approval) { Q_EMIT signerApprovalChanged(); return; }
        if (m_approval_state == ApprovalState::ApprovalUncertain && m_current_transaction &&
            m_approval_transaction == m_current_transaction->getWtx()) return;
        if (m_approval_review == m_current_transaction && m_current_transaction &&
            m_approval_transaction == m_current_transaction->getWtx()) return;
        if (m_approval_state == ApprovalState::ApprovalInitial) return;
        m_approval_state = ApprovalState::ApprovalInitial;
        m_approval_error.clear();
        Q_EMIT signerApprovalChanged();
    });
    m_backend_executor->setObjectName(QStringLiteral("wallet"));
    m_notification_bridge = presentation::MakeGuiObject();
    connect(m_backend_executor.get(), &BackendExecutor::drained, this, [this] {
        m_backend_drained = true;
        checkShutdownFinished();
    });
}

void WalletQmlModel::checkShutdownFinished()
{
    if (m_stopping && m_backend_drained && m_balance_drained && m_opening_drained && m_reviews_drained && !m_shutdown_finished) {
        m_shutdown_finished = true;
        Q_EMIT shutdownFinished();
    }
}

void WalletQmlModel::closeAdmission()
{
    m_admission_closed = true;
    m_preparation_state = PreparationState::PreparationClosed;
    m_psbt_action_state = PsbtActionState::PsbtClosed;
    if (m_flow_details) m_flow_details->closeAdmission();
    ++m_flow_revision;
    m_current_flow.clear(); m_flow_lifetime.reset(); m_flow_document.reset(); m_flow_proof.reset(); m_flow_transaction.reset();
    { std::lock_guard lock{m_send_facts_input->mutex}; m_send_facts_input->selected.reset(); m_send_facts_input->draft.reset(); }
    {
        std::lock_guard lock{m_flow_input->mutex};
        m_flow_input->transaction.reset(); m_flow_input->document.reset(); m_flow_input->proof.reset();
        m_flow_input->recipients.clear(); m_flow_input->notes.clear(); ++m_flow_input->revision;
    }
    m_current_psbt.reset(); m_psbt_fee_proof.reset(); m_preparation_previous_review.clear(); m_preparation_had_previous_review = false;
    m_approval_transaction.reset(); m_approval.reset();
    m_current_transaction = nullptr; // Review aliases retire through the independent bounded registry.
    if (m_review_retirement) m_review_retirement->close();
    m_send_operation.reset();
    m_psbt_action.reset(); m_psbt_action_lease.reset(); m_psbt_document_lease.reset();
    m_preparation.reset(); m_preparation_lease.reset();
    m_custom_target_pending = false;
    m_balance_timer.stop();
    m_receive_payment_poll_timer.stop();
    if (m_fee_read_services.timer) m_fee_read_services.timer->stop();
    if (m_opening) m_opening->closeAdmission();
    if (m_address_list_model) m_address_list_model->closeAdmission();
    if (m_coins_list_model) m_coins_list_model->closeAdmission();
    if (m_send_recipients) m_send_recipients->closeAdmission();
    if (m_send_facts_query) m_send_facts_query->close();
    if (m_flow_query) { m_flow_query->close(); m_flow_query.reset(); }
    if (m_transaction_activity_model) m_transaction_activity_model->closeAdmission();
    if (m_sign_verify_message_model) m_sign_verify_message_model->closeAdmission();
    if (m_bump_transaction_model) m_bump_transaction_model->closeAdmission();
    if (m_imported_psbt_model) m_imported_psbt_model->closeAdmission();
}

void WalletQmlModel::beginShutdown(bool remove_wallet)
{
    if (m_stopping) return;
    closeAdmission();
    m_stopping = true;
    const QPointer<WalletQmlModel> observer{this};
    const bool was_available{std::exchange(m_available_published, false)};
    m_unload_requested->store(true);
    m_balance_timer.stop();
    m_receive_payment_poll_timer.stop();
    if (m_fee_read_services.timer) m_fee_read_services.timer->stop();
    if (m_backup_operation) m_backup_operation->retire();
    if (m_balance_reader) m_balance_reader->close();
    m_balance_known = false;
    m_balance_state = BalanceState::Closed;
    // Complete the resource handoff before any presentation signal can
    // reenter or destroy this facade. No Core effect depends on its lifetime.
    ++m_fee_estimate_request_id;
    m_fee_estimates.clear();
    m_custom_fee_estimate.reset();
    m_fee_estimate_pending = false;
    ++m_fee_estimate_revision;
    invalidateFeeRead(true);
    m_psbt_fee_request.reset();
    m_psbt_fee_pending = false;
    if (m_request_store) {
        m_request_store->close();
        m_request_store->retire(m_receive_requests->releaseSnapshot());
    }
    if (m_receipts) {
        m_receipts->close();
        m_receipts->retire(std::move(m_receipt_snapshot));
    }
    if (m_opening) m_opening->retire(remove_wallet);
    if (m_bump_transaction_model) m_bump_transaction_model->detachWallet();
    if (m_imported_psbt_model) m_imported_psbt_model->detachWallet();
    m_sign_verify_message_model->setWallet(nullptr);
    m_sign_verify_message_model->setSecurityStateChangedFn({});
    auto wallet = std::move(m_wallet);
    const auto backend = m_backend_executor;
    backend->submit(this, [wallet = std::move(wallet), receipts = std::move(m_receipts), requests = std::move(m_request_store)]() mutable {
        requests.reset(); receipts.reset(); wallet.reset();
    }, [] {});
    backend->shutdown();
    if (!observer) return;
    if (m_transaction_activity_model) { m_transaction_activity_model->stop(); if (!observer) return; }
    if (was_available) { Q_EMIT availableChanged(); if (!observer) return; }
    Q_EMIT balanceObservationChanged();
    if (observer) observer->checkShutdownFinished();
}

namespace {
class QtFeeReadTimer final : public FeeReadTimer
{
public:
    explicit QtFeeReadTimer(QObject* parent) : m_timer(new QTimer(parent))
    {
        m_timer->setSingleShot(true);
        m_timer->setInterval(FEE_ESTIMATE_DEBOUNCE_MS);
        QObject::connect(m_timer, &QTimer::timeout, m_timer, [this] {
            auto expired = std::exchange(m_expired, {});
            if (expired) expired();
        });
    }
    ~QtFeeReadTimer() override { delete m_timer; }
    void restart(std::function<void()> expired) override
    {
        m_expired = std::move(expired);
        m_timer->start();
    }
    void stop() override
    {
        m_timer->stop();
        m_expired = {};
    }
private:
    QTimer* m_timer;
    std::function<void()> m_expired;
};
}

void WalletQmlModel::initializeFeeEstimator()
{
    if (!m_fee_read_services.scheduler) m_fee_read_services.scheduler = m_opening ? m_opening->scheduler() : m_backend_executor->scheduler();
    if (!m_fee_read_services.timer) m_fee_read_services.timer = std::make_shared<QtFeeReadTimer>(this);
    if (!m_fee_read_services.post) {
        m_fee_read_services.post = [bridge = m_notification_bridge](std::function<void()> publication) {
            QMetaObject::invokeMethod(bridge.get(), std::move(publication), Qt::QueuedConnection);
        };
    }
    m_flow_details = new FlowDetailsModel(this);
    if (const auto gate = m_admission_closed.shared()) {
        auto post = std::move(m_fee_read_services.post);
        m_fee_read_services.post = [gate, post = std::move(post)](std::function<void()> callback) {
            post([gate, callback = std::move(callback)] { if (!gate->load()) callback(); });
        };
    }
    m_flow_details->setServices({m_fee_read_services.scheduler, m_fee_read_services.post});
    if (!m_fee_read_services.read) {
        m_fee_read_services.read = [opening = m_opening, weak = std::weak_ptr<interfaces::Wallet>{m_wallet}, policy = m_fee_policy](const FeeReadInput& input) {
            FeeReadResult result;
            const auto wallet = opening ? opening->borrow() : weak.lock();
            if (!wallet) return result;
            std::vector<wallet::CRecipient> recipients;
            recipients.reserve(input.recipients.size());
            for (const auto& recipient : input.recipients) {
                const auto destination = DecodeDestination(recipient.address);
                if (!IsValidDestination(destination)) return result;
                recipients.push_back({destination, recipient.amount, false});
            }
            if (recipients.empty()) return result;
            const auto change_type = input.coin_control.m_change_type.value_or(wallet->getDefaultAddressType());
            const auto custom_rate = ParseCustomFeeRatePerKvB(input.custom_fee_rate, (policy.minimum_relay ? std::max<CAmount>(DEFAULT_MIN_RELAY_TX_FEE, policy.minimum_relay()) : DEFAULT_MIN_RELAY_TX_FEE));
            for (const unsigned int target : STANDARD_FEE_TARGETS) {
                if (const auto estimate = EstimatePreviewFee(*wallet, recipients, input.coin_control, change_type, target, input.remainder_index)) {
                    result.estimates.insert(target, *estimate);
                }
            }
            if (input.custom_fee_enabled && custom_rate) {
                result.custom = EstimateCustomPreviewFee(*wallet, recipients, input.coin_control, change_type, *custom_rate, input.remainder_index);
            }
            return result;
        };
    }
    if (m_send_recipients) {
        m_send_recipients->setOpening(m_opening, {m_fee_read_services.scheduler, m_fee_read_services.post});
        connect(m_send_recipients, &SendRecipientsListModel::recipientDraftChanged, this, [this](quint64 token, quint64 revision) {
            if (m_automatic_maximum && (token != m_automatic_maximum->token || revision != m_automatic_maximum->revision))
                invalidateAutomaticMaximum();
        });
        connect(m_send_recipients, &SendRecipientsListModel::validationChanged, this, &WalletQmlModel::scheduleFeesForRecipientValidation);
        const auto changed_intent = [this] { invalidateAutomaticMaximum(); };
        connect(this, &WalletQmlModel::openingMetadataChanged, this, [this] {
            if (m_automatic_maximum && m_automatic_maximum->capabilities != automaticMaximumCapabilities())
                invalidateAutomaticMaximum();
        });
        connect(this, &WalletQmlModel::coinSelectionChanged, this, changed_intent);
        connect(this, &WalletQmlModel::feeTargetBlocksChanged, this, changed_intent);
        connect(this, &WalletQmlModel::customFeeEnabledChanged, this, changed_intent);
        connect(this, &WalletQmlModel::customFeeRateChanged, this, changed_intent);
        connect(this, &WalletQmlModel::maximumRecipientChanged, this, changed_intent);
    }
    if (m_coins_list_model) {
        m_coins_list_model->setOpening(m_opening, {m_fee_read_services.scheduler, m_fee_read_services.post});
        connect(m_coins_list_model, &CoinsListModel::selectionSnapshotChanged, this, [this] {
            invalidatePreparation();
            if (coinSelectionPending()) m_send_facts_known = false;
            const QPointer<WalletQmlModel> observer{this};
            Q_EMIT coinSelectionChanged();
            if (!observer || m_admission_closed) return;
            Q_EMIT sendAmountExhaustsBalanceChanged();
            if (observer) scheduleFeeEstimates();
        });
    }
    requestSendFacts();
}

void WalletQmlModel::initializePsbtFeeReader()
{
    if (!m_psbt_fee_services.scheduler) m_psbt_fee_services.scheduler = m_backend_executor->scheduler();
    if (!m_psbt_fee_services.post) m_psbt_fee_services.post = m_fee_read_services.post;
    if (!m_custom_psbt_lookup) m_psbt_fee_services.lookup = psbtfee::NodeLookup(m_node);
    m_imported_psbt_model->setFeeServices(m_psbt_fee_services);
}

bool WalletQmlModel::matchesReviewedPsbtFee(const PartiallySignedTransaction& psbt) const
{
    return m_current_transaction && m_psbt_fee_proof
        && psbtfee::MatchesReview(psbt, *m_psbt_fee_proof, m_current_transaction->reviewedFee());
}

bool WalletQmlModel::requestPsbtFeeProof()
{
    const auto token = std::make_shared<const psbtfee::RequestIdentity>();
    m_psbt_fee_request = token;
    m_psbt_fee_pending = true;
    m_current_transaction_review_message = tr("Checking transaction fee.");
    const QPointer<WalletQmlModel> observer{this};
    const auto admission = psbtfee::Request(m_psbt_fee_services, m_current_psbt, m_psbt_fee_proof,
        [observer, token](psbtfee::Resolution result) {
            if (observer) observer->applyPsbtFeeProof(token, std::move(result));
        });
    if (!admission) applyPsbtFeeProof(token, {{}, false, true}, false);
    Q_EMIT currentTransactionChanged();
    return admission && observer && observer->m_psbt_fee_request == token;
}

void WalletQmlModel::applyPsbtFeeProof(const std::shared_ptr<const psbtfee::RequestIdentity>& token, psbtfee::Resolution result, bool publish)
{
    if (m_stopping || !m_current_transaction || m_psbt_fee_request != token) return;
    m_psbt_fee_request.reset();
    m_psbt_fee_pending = false;
    if (!result.failed && result.proof) m_psbt_fee_proof = std::move(result.proof);
    const bool known = !result.failed && m_psbt_fee_proof && m_psbt_fee_proof->amount.has_value();
    QPointer<WalletQmlModelTransaction> previous;
    if (known) {
        // Publish a complete value projection. Observers of the old document
        // cannot close or replace it halfway through fee-amount notifications.
        auto* authenticated = new WalletQmlModelTransaction(*m_current_transaction, *m_psbt_fee_proof->amount, this);
        previous = m_current_transaction;
        m_current_transaction = authenticated;
    }
    m_current_transaction_can_send = known && (m_psbt_key_can_send || (result.complete && m_psbt_can_commit_complete));
    m_current_transaction_can_broadcast = known && result.complete;
    m_current_transaction_review_message = !known
        ? tr("The transaction fee is missing or invalid. Add valid input information before broadcasting.")
        : m_current_transaction_can_send || m_current_transaction_can_broadcast ? QString{}
        : tr("This wallet does not have the keys to sign this transaction.");
    if (publish) Q_EMIT currentTransactionChanged();
    if (previous) previous->deleteLater();
}

void WalletQmlModel::invalidateFeeRead(bool close)
{
    const FeeReadState::Event event{close ? FeeReadState::Kind::Close : FeeReadState::Kind::Invalidate,
                                    FeeReadRevision{m_fee_estimate_request_id}};
    execution::Submit(*m_fee_read_services.scheduler,
        [state = m_fee_read_state, event] { state->apply(event); }, [] {}, {},
        {execution::Lane::Domain, sizeof(event)});
}

QString WalletQmlModel::balance() const
{
    if (!m_wallet) {
        return "0";
    }
    return QmlBitcoinUnits::formatForDisplay(QmlBitcoinUnits::fromDisplayUnit(m_display_unit), m_balance);
}

qint64 WalletQmlModel::balanceSatoshi() const
{
    return displayedBalanceSatoshi();
}

void WalletQmlModel::initializeBalanceReader()
{
    m_balance_drained = false;
    m_balance_state = BalanceState::Unknown;
    const QPointer<WalletQmlModel> observer{this};
    const auto scheduler = m_balance_scheduler ? m_balance_scheduler : m_backend_executor->scheduler();
    m_balance_reader = std::make_shared<walletbalance::Reader>(scheduler,
        walletbalance::WalletRead(m_wallet), [bridge = m_notification_bridge, observer] {
            QMetaObject::invokeMethod(bridge.get(), [observer] {
                if (observer && observer->observationsAllowed()) observer->publishBalance();
            }, Qt::QueuedConnection);
        }, m_unload_requested, [bridge = m_notification_bridge, observer, lifetime = m_opening ? m_opening->borrow() : m_wallet] {
            (void)lifetime;
            QMetaObject::invokeMethod(bridge.get(), [observer] {
                if (!observer) return;
                observer->m_balance_drained = true;
                observer->checkShutdownFinished();
            }, Qt::QueuedConnection);
        });
    m_balance_timer.setInterval(1000);
    connect(&m_balance_timer, &QTimer::timeout, this, &WalletQmlModel::refreshBalance);
    m_balance_timer.start();
    refreshBalance();
}

void WalletQmlModel::refreshBalance()
{
    if (m_stopping || m_admission_closed) return;
    if (m_opening) m_opening->retryNotifications();
    if (m_receipts) m_receipts->retry();
    if (m_balance_reader) m_balance_reader->refresh();
    // Recover a retained local outcome if its presentation wakeup was lost.
    // This reads no Core state and cannot repeat the accepted device effect.
    const QPointer<WalletQmlModel> observer{this};
    if (m_preparation && preparationBusy()) publishPreparation();
    if (!observer) return;
    if (observationsAllowed() && m_approval) publishApproval();
    if (!observer || m_stopping) return;
    m_sign_verify_message_model->retryPublications();
    if (!observer || m_stopping) return;
    m_bump_transaction_model->retryPublications();
    if (!observer || m_stopping) return;
    m_imported_psbt_model->retryPublication();
    if (!observer || m_stopping) return;
    m_coins_list_model->retryPublications();
    if (!observer || m_stopping) return;
    m_send_recipients->retryPublications();
}

void WalletQmlModel::publishBalance()
{
    if (!observationsAllowed()) return;
    if (m_admission_closed) return;
    const auto result = m_balance_reader->take();
    if (!result) return;
    BalanceState state{BalanceState::Unknown};
    switch (result->status) {
    case walletbalance::Status::Ready: state = BalanceState::Ready; break;
    case walletbalance::Status::Busy: state = BalanceState::Busy; break;
    case walletbalance::Status::Unavailable: state = BalanceState::Unavailable; break;
    case walletbalance::Status::Failed: state = BalanceState::Failed; break;
    case walletbalance::Status::Rejected: state = BalanceState::Rejected; break;
    case walletbalance::Status::Closed: state = BalanceState::Closed; break;
    }
    const bool changed = result->snapshot && (!m_balance_known || m_balance != result->snapshot->trusted);
    const bool observation_changed = state != m_balance_state || (result->snapshot && !m_balance_known);
    m_balance_state = state;
    if (result->snapshot) {
        m_balance = result->snapshot->trusted;
        m_balance_known = true;
    }
    const QPointer<WalletQmlModel> observer{this};
    if (observation_changed) Q_EMIT balanceObservationChanged();
    if (observer && observationsAllowed() && changed) Q_EMIT balanceChanged();
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
    return m_available_send_balance;
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
    publishAutomaticMaximum(*preview->maximum_amount);
}

void WalletQmlModel::setCustomFeeTarget(unsigned int target)
{
    if (!available() || target < 1 || target > 1008) return;
    const QPointer<WalletQmlModel> observer{this};
    m_custom_target_pending = false;
    m_pending_custom_target = target;
    setCustomFeeEnabled(true);
    if (!observer || !available()) return;
    setCustomFeeRateInternal({}, false);
    if (!observer || !available()) return;
    setFeeTargetBlocks(target);
    if (!observer || !available()) return;
    m_custom_target_pending = true;
    requestSendFacts();
    if (observer) Q_EMIT customFeeTargetPendingChanged();
}

CFeeRate WalletQmlModel::dustRelayFee() const
{
    return CFeeRate{m_dust_relay_fee.value_or(DUST_RELAY_TX_FEE)};
}

bool WalletQmlModel::sendAmountExhaustsBalance() const
{
    if (!m_send_facts_known || !m_wallet || !m_send_recipients || !m_send_recipients->allValid()) {
        return false;
    }

    const CAmount balance{m_selected_send_balance};
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
    return ParseCustomFeeRatePerKvB(m_custom_fee_rate, m_minimum_relay_fee.value_or(DEFAULT_MIN_RELAY_TX_FEE)).has_value();
}

double WalletQmlModel::minimumCustomFeeRate() const
{
    return static_cast<double>(m_minimum_relay_fee.value_or(DEFAULT_MIN_RELAY_TX_FEE)) / FEE_RATE_BASIS_VBYTES;
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
    return m_name;
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
    if (!m_metadata_known) return KeyScheme::UnknownScheme;
    if (m_external_signer) return KeyScheme::ExternalSigner;
    return m_private_keys_disabled ? KeyScheme::WatchOnly : KeyScheme::Software;
}

WalletQmlModel::KeyScheme WalletQmlModel::keySchemeForWallet(interfaces::Wallet& wallet)
{
    if (wallet.hasExternalSigner()) return KeyScheme::ExternalSigner;
    return wallet.privateKeysDisabled() ? KeyScheme::WatchOnly : KeyScheme::Software;
}

QString WalletQmlModel::keySchemeDisplayText(KeyScheme scheme)
{
    switch (scheme) {
    case KeyScheme::WatchOnly: return tr("Watch-only");
    case KeyScheme::MultiKey: return tr("Multi-key");
    case KeyScheme::ExternalSigner: return tr("External signer");
    case KeyScheme::SingleKey: return tr("Single-key");
    case KeyScheme::Software: return tr("Software wallet");
    case KeyScheme::UnknownScheme: return tr("Unknown");
    }
    return tr("Unknown");
}

QString WalletQmlModel::privateKeysStatus() const
{
    return !m_metadata_known ? tr("Unknown") : m_private_keys_disabled ? tr("Disabled") : tr("Enabled");
}

QString WalletQmlModel::externalSignerStatus() const
{
    return !m_metadata_known ? tr("Unknown") : m_external_signer ? tr("Enabled") : tr("None");
}

bool WalletQmlModel::canManagePassphrase() const
{
    return available() && m_metadata_known && !m_private_keys_disabled;
}

namespace {
QString SecurityErrorText(walletsecurity::Error error)
{
    using Error = walletsecurity::Error;
    switch (error) {
    case Error::None: return {};
    case Error::MissingCurrent: return QObject::tr("Enter the current wallet password.");
    case Error::MissingNew: return QObject::tr("Enter a new wallet password.");
    case Error::NoPrivateKeys: return QObject::tr("This wallet cannot manage a local password.");
    case Error::EncryptionFailed: return QObject::tr("The wallet password could not be set.");
    case Error::IncorrectCurrent: return QObject::tr("The current wallet password was incorrect.");
    case Error::ChangeFailed: return QObject::tr("The wallet password could not be changed.");
    case Error::Uncertain: return QObject::tr("The wallet operation outcome could not be confirmed. Check the wallet before retrying.");
    case Error::Rejected: return QObject::tr("The wallet operation could not be started.");
    }
    return QObject::tr("The wallet operation could not be started.");
}
}

WalletQmlModel::ApprovalState WalletQmlModel::signerApprovalState() const
{
    return m_approval && m_approval_review != m_current_transaction
        ? ApprovalState::ApprovalOtherReview : m_approval_state;
}

bool WalletQmlModel::startSecurity(walletsecurity::Kind kind, const QString& current, const QString& replacement)
{
    if (!available() || !m_opening || securityBusy() || preparationBusy() || sendBusy() || m_approval) return false;
    // The original Native owner retains the immutable editor input. Encoding
    // and secure-buffer work happens inside the accepted security action.
    auto current_input = secretinput::Capture(m_opening->scheduler(), current);
    auto replacement_input = secretinput::Capture(m_opening->scheduler(), replacement);
    if (!current_input || !replacement_input) {
        m_security_state = SecurityState::SecurityFailed;
        m_security_error = tr("The password input could not be queued. No wallet password was changed.");
        Q_EMIT securityOperationChanged();
        return false;
    }
    const auto ticket = std::make_shared<const walletsecurity::Identity>();
    const QPointer<WalletQmlModel> observer{this};
    auto operation = std::make_shared<walletsecurity::Operation>(m_opening, kind,
        [bridge = m_notification_bridge, observer, ticket] {
            QMetaObject::invokeMethod(bridge.get(), [observer, ticket] {
                if (observer && observer->observationsAllowed() && observer->m_security_ticket == ticket) observer->publishSecurityOperation();
            }, Qt::QueuedConnection);
        });
    m_security_ticket = ticket;
    m_security_operation = operation;
    m_security_state = SecurityState::SecurityPending;
    m_security_error.clear();
    const bool had_settings_error = !m_settings_error.isEmpty();
    m_settings_error.clear();
    const auto admission = operation->start({kind, {}, {}, std::move(current_input.input), std::move(replacement_input.input)});
    if (had_settings_error) {
        Q_EMIT settingsErrorChanged();
        if (!observer || m_stopping || m_security_ticket != ticket) return bool(admission);
    }
    Q_EMIT securityOperationChanged();
    return bool(admission);
}

void WalletQmlModel::publishSecurityOperation()
{
    if (!observationsAllowed()) return;
    if (m_admission_closed) return;
    const auto ticket = m_security_ticket;
    const auto result = m_security_operation->result();
    if (!result) return;
    const QString error = SecurityErrorText(result->error);
    const bool security_changed = result->state_known && (m_is_encrypted != result->encrypted || m_is_locked != result->locked);
    m_security_state = result->error == walletsecurity::Error::None ? SecurityState::SecuritySucceeded
        : result->error == walletsecurity::Error::Uncertain ? SecurityState::SecurityUncertain : SecurityState::SecurityFailed;
    m_security_error = error;
    m_settings_error = error;
    if (result->state_known) { m_is_encrypted = result->encrypted; m_is_locked = result->locked; }
    const QPointer<WalletQmlModel> observer{this};
    if (security_changed) { Q_EMIT securityStateChanged(); if (!observer || m_stopping || m_security_ticket != ticket) return; }
    Q_EMIT settingsErrorChanged(); if (!observer || m_stopping || m_security_ticket != ticket) return;
    if (result->error == walletsecurity::Error::Uncertain) m_opening->refreshMetadata();
    Q_EMIT securityOperationChanged();
}

bool WalletQmlModel::encryptWallet(const QString& passphrase)
{
    return startSecurity(walletsecurity::Kind::Encrypt, {}, passphrase);
}

bool WalletQmlModel::changeWalletPassphrase(const QString& old_passphrase, const QString& new_passphrase)
{
    return startSecurity(walletsecurity::Kind::ChangePassphrase, old_passphrase, new_passphrase);
}

bool WalletQmlModel::backupWallet(const QString& path)
{
    clearSettingsError();
    if (!m_wallet) {
        setSettingsError(tr("No wallet is selected."));
        return false;
    }
    if (!available()) {
        setSettingsError(tr("This wallet is closing."));
        return false;
    }
    if (path.size() > 32'768 || path.trimmed().isEmpty()) {
        setSettingsError(tr("Choose a location for the wallet backup."));
        return false;
    }

    if (m_backup_operation && m_backup_operation->pending()) {
        setSettingsError(tr("A backup operation is already pending."));
        return false;
    }
    auto services = m_backup_services;
    const auto wallet_lane = m_backend_executor->scheduler();
    if (services.scheduler && (!services.wallet_name || !services.write)) {
        setSettingsError(tr("The backup service has no owned wallet adapter."));
        return false;
    }
    if (!services.scheduler) services.scheduler = wallet_lane;
    if (!services.post) services.post = m_fee_read_services.post;
    if (!services.wallet_name) services.wallet_name = [wallet = m_wallet.get()] { return wallet->getWalletName(); };
    if (!services.inspect) services.inspect = backup::Inspect;
    if (!m_backup_services.coordinator) m_backup_services.coordinator = std::make_shared<backup::InstallCoordinator>();
    if (!services.write) {
        services.write = [wallet = m_wallet.get(), coordinator = m_backup_services.coordinator](const backup::Proposal& target, bool replace) {
            return backup::Write(target, replace, [wallet](const std::string& staged) { return wallet->backupWallet(staged); }, *coordinator);
        };
    }
    auto* operation = new BackupFileOperation(path, std::move(services), this);
    const QPointer<BackupFileOperation> previous{m_backup_operation};
    m_backup_operation = operation;
    const bool admitted = operation->start();
    Q_EMIT backupOperationChanged();
    if (previous) previous->deleteLater();
    return admitted;
}

ImageExportOperation* WalletQmlModel::savePaymentRequestImage(PaymentRequest* request, const QImage& image, const QUrl& path)
{
    if (!available() || m_stopping || !m_opening || !m_request_storage_loaded || receiveRequestReconciliationPending() ||
        !request || (request != m_current_payment_request && request != m_detail_payment_request && request != m_receiving_address) ||
        request->paymentReceived() || request->qrPayload().isEmpty()) return nullptr;
    const auto* record = m_receive_requests->preparedEntry(request->id());
    if (!record || record->entry.payment_received || record->display.uri != request->qrPayload()) return nullptr;
    const auto publication = m_receive_requests->snapshot();
    if (!publication || !m_request_store->presentationCurrent() || publication->revision != m_request_store->revision()) return nullptr;
    imageexport::Identity identity{m_opening->identity(), request->id(), request->qrPayload(), request->generation(), request->revision()};
    imageexport::Services services{m_balance_scheduler ? m_balance_scheduler : m_backend_executor->scheduler(), m_unload_requested, {}, {}};
    services.write = [weak = std::weak_ptr<walletrequests::Store>{m_request_store}, version = publication->revision](const QImage& pixels, const QUrl& target) {
        const auto store = weak.lock();
        if (!store || !store->presentationCurrent() || store->revision() != version) return imageexport::Error::Retired;
        return imageexport::WritePng(pixels, target);
    };
    services.submit = [weak = std::weak_ptr<walletopening::Session>{m_opening}](auto work, auto done, auto failed, size_t bytes) {
        const auto opening = weak.lock();
        const auto action = opening ? opening->startAction() : nullptr;
        if (!action) return execution::Admission{{}, execution::Rejection::Closed};
        // The existing Action lease joins physical opening retirement. It is
        // held only by the background task, never until GUI acknowledgement.
        return opening->perform(action, [work = std::move(work)](interfaces::Wallet&) { return work(); },
                                std::move(done), std::move(failed), bytes);
    };
    auto* operation = new ImageExportOperation(image, path, std::move(identity), std::move(services),
        [bridge = m_notification_bridge](auto done) { QMetaObject::invokeMethod(bridge.get(), std::move(done), Qt::QueuedConnection); });
    connect(request, &PaymentRequest::generationChanged, operation, &ImageExportOperation::retire);
    connect(request, &PaymentRequest::draftChanged, operation, &ImageExportOperation::retire);
    connect(request, &PaymentRequest::qrPayloadChanged, operation, &ImageExportOperation::retire);
    connect(request, &PaymentRequest::paymentReceivedChanged, operation, &ImageExportOperation::retire);
    connect(request, &QObject::destroyed, operation, &ImageExportOperation::retire);
    connect(this, &WalletQmlModel::availableChanged, operation, [operation] { operation->retire(); });
    operation->start();
    return operation;
}

void WalletQmlModel::clearSettingsError()
{
    setSettingsError(QString());
}

QVariantList WalletQmlModel::availableReceiveAddressTypes() const
{
    if (!available() || !m_metadata_known || !m_can_get_addresses) {
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
        if (type == OutputType::BECH32M && !m_taproot) {
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
    const QVariantList available_types = availableReceiveAddressTypes();
    const QString& saved_type = m_receive_preference;
    for (const QVariant& item : available_types) {
        const QVariantMap type{item.toMap()};
        if (type.value(QStringLiteral("id")).toString() == saved_type) {
            return saved_type;
        }
    }

    // The list starts with Taproot, falling back to SegWit when unavailable.
    return available_types.empty()
        ? QString{}
        : available_types.front().toMap().value(QStringLiteral("id")).toString();
}

bool WalletQmlModel::setDefaultReceiveAddressType(const QString& address_type)
{
    if (!available() || !m_opening || address_type.size() > 16) return false;
    const auto admission = m_opening->setReceivePreference(address_type.toStdString());
    m_receive_preference_pending = bool(admission) || m_receive_preference_pending;
    m_receive_preference_error = admission ? QString{} : tr("The address preference could not be started.");
    Q_EMIT receivePreferenceChanged();
    return bool(admission);
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

void WalletQmlModel::removeWallet()
{
    beginShutdown(true);
}

ReceiveRequestOperation* WalletQmlModel::setCurrentPaymentRequestAddress(const QString& address)
{
    if (!available() || !m_current_payment_request || address.isEmpty()) return nullptr;
    walletrequests::Command command; command.kind = walletrequests::Kind::SelectAddress;
    command.view = 1; command.view_revision = m_receiving_address->revision();
    command.draft.address = address;
    return submitRequest(std::move(command), m_current_payment_request);
}

ReceiveRequestOperation* WalletQmlModel::ensureReceivingAddress(bool next, const QString& address_type)
{
    walletrequests::Command command; command.kind = walletrequests::Kind::ReserveAddress;
    command.view = 1; command.view_revision = m_receiving_address->revision();
    command.draft.address = m_receiving_address->address();
    command.draft.address_type = address_type.isEmpty() ? defaultReceiveAddressType() : address_type;
    command.next = next; command.explicit_type = !address_type.isEmpty();
    return submitRequest(std::move(command), m_receiving_address);
}

ReceiveRequestOperation* WalletQmlModel::ensureReceivingAddressWithPassphrase(const QString& passphrase, bool next, const QString& address_type)
{
    walletrequests::Command command; command.kind = walletrequests::Kind::ReserveAddress;
    command.view = 1; command.view_revision = m_receiving_address->revision();
    command.draft.address = m_receiving_address->address();
    command.draft.address_type = address_type.isEmpty() ? defaultReceiveAddressType() : address_type;
    command.next = next; command.explicit_type = !address_type.isEmpty();
    auto captured = secretinput::Capture(m_opening ? m_opening->scheduler() : nullptr, passphrase);
    command.captured_secret = std::move(captured.input); command.secret_capture_error = captured.error;
    return submitRequest(std::move(command), m_receiving_address);
}

ReceiveRequestOperation* WalletQmlModel::resumeReceivingAddress(ReceiveRequestOperation* original, const QString& passphrase)
{
    if (!available() || !m_opening || !original || !original->addressIntent() || original->addressIntent()->create ||
        original->addressIntent()->opening != m_opening->identity() || original->viewRevision() != m_receiving_address->revision()) return nullptr;
    walletrequests::Command command; command.kind = walletrequests::Kind::ResumeAddress;
    command.resume = original->addressIntent();
    auto captured = secretinput::Capture(m_opening->scheduler(), passphrase);
    command.captured_secret = std::move(captured.input); command.secret_capture_error = captured.error;
    return submitRequest(std::move(command), m_receiving_address);
}

ReceiveRequestOperation* WalletQmlModel::retryReceivingAddress(ReceiveRequestOperation* original)
{
    if (!available() || !m_opening || !original || !original->addressIntent() || original->addressIntent()->create ||
        original->addressIntent()->opening != m_opening->identity()) return nullptr;
    walletrequests::Command command; command.kind = walletrequests::Kind::ResumeAddress;
    command.resume = original->addressIntent();
    return submitRequest(std::move(command), m_receiving_address);
}

ReceiveRequestOperation* WalletQmlModel::commitReceivingPaymentRequest()
{
    if (!available() || !m_receiving_address_ready || !m_receiving_intent || !m_receiving_address || !m_current_payment_request ||
        !m_current_payment_request->id().isEmpty() || m_receiving_address->address().isEmpty()) return nullptr;
    // The owner checks receipt readiness and the captured address on Native.
    // Admission is represented by a pending operation, never by a saved ID.
    m_current_payment_request->setPreparedDestination(m_receiving_address->destination(), m_receiving_address->address());
    return savePaymentRequest(m_current_payment_request, true);
}

ReceiveRequestOperation* WalletQmlModel::submitRequest(walletrequests::Command command, PaymentRequest* draft, bool consume_address)
{
    auto* operation = new ReceiveRequestOperation;
    QQmlEngine::setObjectOwnership(operation, QQmlEngine::JavaScriptOwnership);
    const QPointer<WalletQmlModel> observer{this};
    const QPointer<PaymentRequest> target{draft};
    const auto revision = draft ? draft->revision() : 0;
    const auto address_revision = m_receiving_address->revision();
    const auto consumed_intent = command.reservation;
    const bool label_only = command.kind == walletrequests::Kind::Edit || command.kind == walletrequests::Kind::Label;
    const auto* original_opening = m_opening.get();
    connect(operation, &ReceiveRequestOperation::changed, this, [observer, operation, target, revision, consume_address, address_revision, consumed_intent, original_opening, label_only, label_refresh_published = false]() mutable {
        if (!observer || observer->m_stopping || operation->pending()) return;
        const auto result = *operation->result();
        const auto intent = operation->addressIntent();
        const bool ready = operation->addressReady();
        const auto error_text = operation->errorText();
        observer->publishRequestHistory();
        if (!observer || observer->m_stopping) return;
        if (result.label_saved && !label_refresh_published && observer->observationsAllowed() && observer->m_opening.get() == original_opening) {
            // The actual acknowledged write owns this refresh even when the
            // public wallet adapter does not repeat an address notification.
            label_refresh_published = true;
            if (label_only) Q_EMIT observer->addressLabelsChanged();
            else Q_EMIT observer->addressInventoryChanged();
            if (!observer || !observer->observationsAllowed() || observer->m_opening.get() != original_opening) return;
            Q_EMIT observer->addressListChanged();
            if (!observer || !observer->observationsAllowed()) return;
        }
        if (!result.paid_address.isEmpty() && observer->m_receiving_address->address() == result.paid_address) {
            observer->m_receiving_address_ready = false;
            observer->m_receiving_address->setPaymentReceived(true);
            if (!observer || observer->m_stopping) return;
            Q_EMIT observer->receivingAddressStateChanged();
            if (!observer || observer->m_stopping) return;
        }
        if (intent && !intent->create && observer->m_receiving_intent != intent) return;
        const bool apply = target && target->revision() == revision;
        if (apply && intent && intent->select && (result.address || result.request_loaded)) {
            target->clear();
            if (!observer || !target || observer->m_stopping) return;
        }
        if (apply && result.address) {
            const auto& address = *result.address;
            target->setPreparedDestination(address.destination, address.address);
            if (!observer || !target || observer->m_stopping) return;
            target->setAddressType(address.address_type);
            if (!observer || !target || observer->m_stopping) return;
            target->setPaymentReceived(false);
            if (!observer || !target || observer->m_stopping) return;
            if (intent && intent->select) {
                target->setNoteSelf(address.label);
                if (!observer || !target || observer->m_stopping) return;
                target->setIsEditing(true);
                if (!observer || !target || observer->m_stopping) return;
                if (observer->m_receiving_address->revision() == address_revision) {
                    observer->m_receiving_address->clear();
                    if (!observer || observer->m_stopping) return;
                    observer->m_receiving_address->setPreparedDestination(address.destination, address.address);
                    if (!observer || observer->m_stopping) return;
                    observer->m_receiving_address->setAddressType(address.address_type);
                    if (!observer || observer->m_stopping) return;
                }
            }
        }
        if (apply && intent) {
            target->setNeedsUnlock(result.error == walletrequests::Error::NeedsUnlock || result.error == walletrequests::Error::IncorrectPassphrase);
            if (!observer || !target || observer->m_stopping) return;
            target->setUnlockError(result.error == walletrequests::Error::IncorrectPassphrase ? error_text : QString{});
            if (!observer || !target || observer->m_stopping) return;
        }
        if ((result.request_saved || result.request_loaded) && result.record && apply) {
            observer->applyRequest(target, result.record->display);
            if (!observer || !target || observer->m_stopping) return;
            if (result.request_loaded) {
                target->setIsEditing(true);
                if (!observer || observer->m_stopping) return;
            }
        }
        if (intent && !intent->create) {
            observer->m_receiving_address_pending = false;
            observer->m_receiving_address_ready = ready && apply;
            observer->m_receive_reconciliation_pending = observer->m_receipts && !observer->m_receipts->settled();
            if (ready && result.address) observer->setDefaultReceiveAddressType(result.address->address_type);
            if (!observer || observer->m_stopping) return;
            Q_EMIT observer->receivingAddressStateChanged();
            if (!observer || observer->m_stopping) return;
            Q_EMIT observer->receiveRequestReconciliationPendingChanged();
            if (!observer || observer->m_stopping) return;
        }
        if (consume_address && result.request_saved && result.reservation_released &&
            observer->m_receiving_intent == consumed_intent && observer->m_receiving_address->revision() == address_revision) {
            observer->m_receiving_address_ready = false;
            observer->m_receiving_address->clear();
            if (observer && observer->observationsAllowed()) Q_EMIT observer->receivingAddressStateChanged();
        }
    });
    operation->start(available() ? m_request_store : nullptr, std::move(command), m_notification_bridge);
    const auto intent = operation->addressIntent();
    if (intent && !intent->create) {
        m_receiving_intent = intent;
        m_receiving_address_ready = false; m_receiving_address_pending = operation->pending();
        m_receive_reconciliation_pending = true;
        Q_EMIT receivingAddressStateChanged();
        if (observer && observer->observationsAllowed()) Q_EMIT receiveRequestReconciliationPendingChanged();
    }
    return operation;
}

ReceiveRequestOperation* WalletQmlModel::savePaymentRequest(PaymentRequest* request, bool consume_address)
{
    if (!request || !m_request_store) return nullptr;
    walletrequests::Command command;
    command.kind = request->id().isEmpty() ? walletrequests::Kind::Create : walletrequests::Kind::Edit;
    if (const auto record = m_receive_requests->preparedEntry(request->id())) {
        command.id = record->entry.id; command.revision = record->revision;
    } else if (!request->id().isEmpty()) {
        bool valid{false}; command.id = request->id().toLongLong(&valid);
        if (!valid) return nullptr;
    }
    command.draft = {request->address(), request->label(), request->message(), request->noteSelf(),
                     request->addressType(), request->amount()->satoshi()};
    command.require_unused = consume_address;
    command.consume_address = consume_address;
    if (consume_address) command.reservation = m_receiving_intent;
    if (command.draft.address_type.isEmpty()) command.draft.address_type = defaultReceiveAddressType();
    command.view = consume_address ? 1 : 2;
    command.view_revision = request->revision();
    return submitRequest(std::move(command), request, consume_address);
}

ReceiveRequestOperation* WalletQmlModel::commitPaymentRequest()
{
    if (!available() || !m_current_payment_request) return nullptr;
    return savePaymentRequest(m_current_payment_request);
}

ReceiveRequestOperation* WalletQmlModel::commitPaymentRequestWithPassphrase(const QString& passphrase)
{
    if (!available() || !m_current_payment_request) return nullptr;
    auto* request = m_current_payment_request;
    walletrequests::Command command;
    command.kind = request->id().isEmpty() ? walletrequests::Kind::Create : walletrequests::Kind::Edit;
    if (const auto record = m_receive_requests->preparedEntry(request->id())) { command.id = record->entry.id; command.revision = record->revision; }
    command.draft = {request->address(), request->label(), request->message(), request->noteSelf(),
        request->addressType().isEmpty() ? defaultReceiveAddressType() : request->addressType(), request->amount()->satoshi()};
    command.view = 2; command.view_revision = request->revision();
    auto captured = secretinput::Capture(m_opening ? m_opening->scheduler() : nullptr, passphrase);
    command.captured_secret = std::move(captured.input); command.secret_capture_error = captured.error;
    return submitRequest(std::move(command), request);
}

void WalletQmlModel::reloadReceiveRequests()
{
    if (!m_request_store || m_stopping) return;
    walletrequests::Command command; command.kind = walletrequests::Kind::Reload;
    const auto admission = m_request_store->submit(std::move(command));
    m_request_storage_busy = bool(admission);
    m_receive_reconciliation_pending = bool(admission);
    m_request_storage_error = admission ? QString{} : tr("Payment requests could not be loaded. Try again.");
    if (m_receipts && !m_receive_reconciliation_error.isEmpty()) m_receipts->retry();
    const QPointer<WalletQmlModel> observer{this};
    Q_EMIT receiveRequestReconciliationPendingChanged();
    if (!observer || m_stopping) return;
    Q_EMIT receiveRequestStorageChanged();
}

ReceiveRequestOperation* WalletQmlModel::removeReceiveRequest(const QString& request_id)
{
    walletrequests::Command command; command.kind = walletrequests::Kind::Erase;
    const auto record = m_receive_requests->preparedEntry(request_id);
    if (!record) return nullptr;
    command.id = record->entry.id; command.revision = record->revision;
    return submitRequest(std::move(command));
}

bool WalletQmlModel::applyRequest(PaymentRequest* target, walletrequests::PreparedRequest data, bool as_template)
{
    // A property observer can close the opening or destroy either projection.
    // Keep values independent of the model's replaceable snapshot throughout.
    const QPointer<WalletQmlModel> observer{this};
    const QPointer<PaymentRequest> held{target};
    const auto generation = target->generation();
    const auto current = [&] {
        if (!observer || !held || observer->m_stopping || held->generation() != generation) return false;
        // A template consumes captured values, not the source row's identity.
        if (as_template) return true;
        const auto* latest = observer->m_receive_requests->preparedEntry(data.id);
        return latest && latest->revision == data.revision;
    };
    // Our setters produce bounded property notifications. Additional input
    // revisions belong to a reentrant receiver and stop this snapshot update.
    const auto update = [&](uint64_t own_signals, auto effect) {
        if (!current()) return false;
        const auto before = target->revision();
        effect();
        if (!current()) return false;
        const auto after = target->revision();
        return after >= before && after - before <= own_signals;
    };
    if (!as_template && !update(2, [&] { target->setPreparedDestination(data.destination, data.address); })) return false;
    if (!update(1, [&] { target->setAddressType(data.address_type); })) return false;
    if (!update(1, [&] { target->setLabel(data.label); })) return false;
    if (!update(1, [&] { target->setMessage(data.message); })) return false;
    if (!update(1, [&] { target->setNoteSelf(data.note); })) return false;
    if (!update(1, [&] { target->amount()->setSatoshi(data.amount_sats); })) return false;
    if (as_template) {
        if (!update(0, [&] { target->setIsEditing(false); })) return false;
        return update(0, [&] { target->setIsEditing(true); });
    }
    if (!update(1, [&] { target->setId(data.numeric_id); })) return false;
    if (!update(0, [&] { target->setCreated(data.created); })) return false;
    if (!update(0, [&] { target->setReceivedAmountSatoshi(receivedPaymentRequestAmount(data.address)); })) return false;
    if (!update(0, [&] { target->setPaymentReceived(data.payment_received); })) return false;
    if (!update(0, [&] { target->setNeedsUnlock(false); })) return false;
    if (!update(0, [&] { target->setUnlockError({}); })) return false;
    if (!update(0, [&] { target->setIsEditing(false); })) return false;
    return update(0, [&] { target->setPreparedPayload(data.uri); });
}

bool WalletQmlModel::receiveRequestReconciliationPending() const
{
    const auto snapshot = m_receive_requests ? m_receive_requests->snapshot() : nullptr;
    return m_receive_reconciliation_pending || m_receive_reconciliation_applying ||
        (m_opening && m_opening->notificationsPending()) || (m_receipts && !m_receipts->settled()) ||
        (m_request_store && (!m_request_store->presentationCurrent() || !snapshot || snapshot->revision != m_request_store->revision()));
}

void WalletQmlModel::publishRequestHistory()
{
    if (!observationsAllowed()) return;
    if (m_admission_closed) return;
    if (!m_request_store || m_stopping) return;
    auto update = m_request_store->take();
    if (!update || (update->snapshot && update->snapshot->opening != m_opening->identity())) return;
    const QPointer<WalletQmlModel> observer{this};
    m_request_storage_loaded = update->loaded;
    m_request_storage_busy = update->busy;
    m_request_storage_error = ReceiveRequestOperation::ErrorText(update->error);
    Q_EMIT receiveRequestReconciliationPendingChanged();
    if (!observer || m_stopping) return;
    if (!update->snapshot) { Q_EMIT receiveRequestStorageChanged(); return; }
    const auto store = m_request_store;
    store->retire(m_receive_requests->setSnapshot(std::move(update->snapshot)));
    if (!observer || m_stopping) return;
    // Only the bounded set of held projections is refreshed. Unsaved drafts
    // retain their text; paid public fields are always restored from authority.
    for (auto* held : {m_current_payment_request, m_detail_payment_request}) {
        const auto record = m_receive_requests->preparedEntry(held->id());
        if (!record) continue;
        if (!held->isEditing()) applyRequest(held, record->display);
        else if (record->display.payment_received) {
            const auto data = record->display;
            const QPointer<PaymentRequest> target{held};
            const auto generation = held->generation();
            const auto current = [&] {
                if (!observer || !target || observer->m_stopping || target->generation() != generation) return false;
                const auto* latest = observer->m_receive_requests->preparedEntry(data.id);
                return latest && latest->revision == data.revision;
            };
            held->setPaymentReceived(true);
            if (!current()) return;
            held->amount()->setSatoshi(data.amount_sats);
            if (!current()) return;
            held->setLabel(data.label);
            if (!current()) return;
            held->setMessage(data.message);
        }
        if (!observer || m_stopping) return;
    }
    Q_EMIT receiveRequestReconciliationPendingChanged();
    if (!observer || m_stopping) return;
    Q_EMIT receiveRequestStorageChanged();
}

bool WalletQmlModel::loadPaymentRequest(const QString& request_id)
{
    if (!available()) return false;
    const auto record = m_receive_requests->preparedEntry(request_id);
    if (!record || !m_current_payment_request) return false;
    const auto data = record->display;
    const QPointer<WalletQmlModel> observer{this};
    const QPointer<PaymentRequest> target{m_current_payment_request};
    const auto generation = target->generation();
    target->clear();
    if (!observer || !target || observer->m_stopping || target->generation() != generation + 1 ||
        !target->address().isEmpty() || !target->label().isEmpty() || !target->message().isEmpty() ||
        !target->noteSelf().isEmpty() || target->amount()->isSet()) return false;
    return applyRequest(target, data);
}

bool WalletQmlModel::loadPaymentRequestDetail(const QString& request_id)
{
    if (!available()) return false;
    const auto record = m_receive_requests->preparedEntry(request_id);
    if (!record || !m_detail_payment_request) return false;
    const auto data = record->display;
    const QPointer<WalletQmlModel> observer{this};
    const QPointer<PaymentRequest> target{m_detail_payment_request};
    const auto generation = target->generation();
    target->clear();
    if (!observer || !target || observer->m_stopping || target->generation() != generation + 1 ||
        !target->address().isEmpty() || !target->label().isEmpty() || !target->message().isEmpty() ||
        !target->noteSelf().isEmpty() || target->amount()->isSet()) return false;
    return applyRequest(target, data);
}

ReceiveRequestOperation* WalletQmlModel::updatePaymentRequest(const QString& request_id, qint64 amount,
                                                           const QString& label, const QString& message, const QString& note)
{
    const auto record = m_receive_requests->preparedEntry(request_id);
    if (!record) return nullptr;
    walletrequests::Command command; command.kind = walletrequests::Kind::Edit;
    command.id = record->entry.id; command.revision = record->revision;
    command.draft = {record->display.address, label, message, note, {}, amount};
    return submitRequest(std::move(command));
}

void WalletQmlModel::refreshReceiveRequestPayments()
{
    if (m_stopping || !m_request_store) return;
    m_request_store->interest(1, m_receiving_address->address());
}

void WalletQmlModel::publishReceipts()
{
    if (!observationsAllowed()) return;
    if (m_admission_closed) return;
    if (!available()) return;
    if (m_receive_reconciliation_applying) { m_receipt_projection_again = true; return; }
    auto update = m_receipts->take();
    if (!update) return;
    const QPointer<WalletQmlModel> guard{this};
    m_receive_reconciliation_pending = update->status != walletreceipts::Status::Ready;
    m_receipt_needs_poll = update->needs_poll;
    m_receive_reconciliation_error = update->status == walletreceipts::Status::Failed || update->status == walletreceipts::Status::Rejected
        ? tr("Payments could not be checked. Try again.") : QString{};
    if (update->snapshot && update->snapshot->opening == m_opening->identity()) {
        m_receipts->retire(std::exchange(m_receipt_snapshot, std::move(update->snapshot)));
        m_receive_reconciliation_applying = true;
        if (m_receiving_address && m_receipt_snapshot->observed.contains(m_receiving_address->address())) {
            m_receiving_address->setPaymentReceived(true); if (!guard || m_stopping) return;
        }
        updateReceivedPaymentRequestAmounts(); if (!guard || m_stopping) return;
        m_receive_reconciliation_applying = false;
    }
    if (std::exchange(m_receipt_projection_again, false)) {
        QTimer::singleShot(0, this, [guard] { if (guard && guard->observationsAllowed()) guard->publishReceipts(); });
    }
    if (!m_receive_reconciliation_pending) m_opening->acknowledgeTransactions();
    for (const auto& [id, change] : update->transactions) {
        Q_EMIT transactionChanged(QString::fromStdString(id.ToString()), change); if (!guard || m_stopping) return;
    }
    if (update->rescan && m_transaction_activity_model) {
        m_transaction_activity_model->reload(); if (!guard || m_stopping) return;
    }
    if (m_receipt_needs_poll) m_receive_payment_poll_timer.start();
    else m_receive_payment_poll_timer.stop();
    Q_EMIT receiveRequestReconciliationPendingChanged();
}

void WalletQmlModel::pollUnconfirmedReceiveRequestPayments()
{
    if (observationsAllowed() && m_receipts) m_receipts->poll();
}

CAmount WalletQmlModel::receivedPaymentRequestAmount(const QString& address) const
{
    if (!m_receipt_snapshot) return 0;
    const auto it = m_receipt_snapshot->totals.find(address);
    return it == m_receipt_snapshot->totals.end() ? 0 : it->second;
}

void WalletQmlModel::updateReceivedPaymentRequestAmounts()
{
    const QPointer<WalletQmlModel> observer{this};
    for (auto* request : {m_current_payment_request, m_detail_payment_request}) {
        if (request && !request->id().isEmpty()) {
            request->setReceivedAmountSatoshi(receivedPaymentRequestAmount(request->address()));
            if (!observer || m_stopping) return;
        }
    }
}

void WalletQmlModel::usePaymentRequestAsTemplate(const QString& request_id)
{
    if (!available() || !m_request_store || !m_current_payment_request || !m_receive_requests) return;
    const auto record = m_receive_requests->preparedEntry(request_id);
    if (!record) return;
    const auto prepared = record->display;
    const QPointer<PaymentRequest> target{m_current_payment_request};
    const auto target_generation = target->generation();
    const auto target_revision = target->revision();
    // Fence the previous password question at admission. Storage cleanup and
    // the fresh-address decision stay ordered with the next reserve command.
    walletrequests::Command command; command.kind = walletrequests::Kind::ForgetAddress;
    command.view = 1; command.view_revision = m_receiving_address->revision();
    command.clear_reservation = true; command.draft.address = m_receiving_address->address();
    const auto admitted = m_request_store->submit(std::move(command));
    if (!admitted) {
        m_request_storage_error = tr("The receiving address could not be reset. Try again.");
        Q_EMIT receiveRequestStorageChanged();
        return;
    }
    const QPointer<WalletQmlModel> observer{this};
    m_receiving_intent.reset();
    m_receiving_address_ready = false; m_receiving_address_pending = false;
    m_receiving_address->clear();
    if (!observer || m_stopping) return;
    Q_EMIT receivingAddressStateChanged();
    if (!observer || m_stopping) return;
    if (!target || target->generation() != target_generation || target->revision() != target_revision) return;
    target->clear();
    if (!observer || !target || m_stopping || target->generation() != target_generation + 1 ||
        !target->label().isEmpty() || !target->message().isEmpty() || !target->noteSelf().isEmpty() || target->amount()->isSet()) return;
    applyRequest(target, prepared, true);
}

ReceiveRequestOperation* WalletQmlModel::setAddressLabel(const QString& address, const QString& label)
{
    walletrequests::Command command; command.kind = walletrequests::Kind::Label;
    command.address = address; command.label = label;
    return submitRequest(std::move(command));
}

void WalletQmlModel::requestSendFacts()
{
    if (!available() || m_stopping || !m_opening || coinSelectionPending()) return;
    {
        std::lock_guard lock{m_send_facts_input->mutex};
        m_send_facts_input->control = m_coin_control;
        m_send_facts_input->selected = selectedCoins();
        m_send_facts_input->draft = m_send_recipients->draftSnapshot();
        m_send_facts_input->maximum_token = m_send_recipients->recipientToken(m_maximum_recipient);
        ++m_send_facts_input->revision;
    }
    if (!m_send_facts_query) {
        const QPointer<WalletQmlModel> observer{this};
        m_send_facts_query = std::make_shared<execution::SnapshotQuery<SendFacts>>(m_opening->scheduler(),
            [opening = m_opening, input = m_send_facts_input, node = m_node, policy = m_fee_policy] {
                wallet::CCoinControl control;
                quint64 revision;
                std::shared_ptr<const CoinsListModel::Selection> selected;
                std::shared_ptr<const senddraft::Snapshot> draft;
                quint64 maximum_token;
                {
                    std::lock_guard lock{input->mutex};
                    control = input->control;
                    selected = input->selected;
                    draft = input->draft;
                    maximum_token = input->maximum_token;
                    revision = input->revision;
                }
                auto wallet = opening->borrow();
                if (!wallet) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
                if (selected) for (const auto& outpoint : *selected) control.Select(outpoint);
                ApplySelectedInputsPolicy(control);
                SendFacts facts;
                facts.available = wallet->getAvailableBalance(wallet::CCoinControl{});
                facts.selected = wallet->getAvailableBalance(control);
                if (draft) facts.gross_remainder = senddraft::GrossRemainder(*draft, maximum_token, facts.selected);
                facts.dust = node ? node->getDustRelayFee().GetFeePerK() : DUST_RELAY_TX_FEE;
                facts.minimum_relay = policy.minimum_relay ? std::max<CAmount>(DEFAULT_MIN_RELAY_TX_FEE, policy.minimum_relay()) : DEFAULT_MIN_RELAY_TX_FEE;
                facts.revision = revision;
                for (const auto target : CUSTOM_FEE_TARGETS) {
                    auto target_control = control;
                    target_control.m_feerate.reset();
                    target_control.m_confirm_target = target;
                    facts.target_rates.insert(target, wallet->getMinimumFee(FEE_RATE_BASIS_VBYTES, target_control, nullptr, nullptr));
                }
                control.m_feerate.reset();
                ApplyRegtestStaticFeeOverride(control);
                facts.selected_target_rate = control.m_feerate ? control.m_feerate->GetFeePerK()
                    : wallet->getMinimumFee(FEE_RATE_BASIS_VBYTES, control, nullptr, nullptr);
                return facts;
            }, [post = m_fee_read_services.post, observer] {
                post([observer] { if (observer) observer->publishSendFacts(); });
            });
    }
    const bool was_known = std::exchange(m_send_facts_known, false);
    m_send_facts_query->request();
    if (was_known) Q_EMIT sendFactsChanged();
}

void WalletQmlModel::publishSendFacts()
{
    if (!observationsAllowed()) return;
    if (coinSelectionPending()) return;
    const auto result = m_send_facts_query->take();
    if (m_stopping || !available() || !result) return;
    if (!result->snapshot) {
        if (std::exchange(m_custom_target_pending, false)) Q_EMIT customFeeTargetPendingChanged();
        return;
    }
    {
        std::lock_guard lock{m_send_facts_input->mutex};
        if (result->snapshot->revision != m_send_facts_input->revision) return;
    }
    m_available_send_balance = result->snapshot->available;
    m_selected_send_balance = result->snapshot->selected;
    const bool was_custom_valid = customFeeRateValid();
    m_dust_relay_fee = result->snapshot->dust;
    m_minimum_relay_fee = result->snapshot->minimum_relay;
    m_custom_target_rates = result->snapshot->target_rates;
    m_send_facts_known = true;
    const QPointer<WalletQmlModel> observer{this};
    Q_EMIT sendFactsChanged();
    if (!observer || m_stopping) return;
    if (was_custom_valid != customFeeRateValid()) {
        Q_EMIT customFeeRateValidChanged();
        if (!observer || m_stopping) return;
    }
    Q_EMIT sendAmountExhaustsBalanceChanged();
    if (!observer || m_stopping) return;
    {
        std::lock_guard lock{m_send_facts_input->mutex};
        if (result->snapshot->revision != m_send_facts_input->revision) return;
    }
    if (m_custom_target_pending && m_custom_fee_enabled && feeTargetBlocks() == m_pending_custom_target) {
        m_custom_target_pending = false;
        const auto rate = result->snapshot->selected_target_rate;
        setCustomFeeRateInternal(rate > 0 ? QString::number(rate / 1000.0, 'f', 3) : QString{}, false);
        if (!observer || m_stopping) return;
        Q_EMIT customFeeTargetPendingChanged();
        if (!observer || m_stopping) return;
    }
    if (m_maximum_recipient && m_maximum_recipient->address()->address().isEmpty() && result->snapshot->gross_remainder) {
        QScopedValueRollback<bool> updating{m_updating_maximum, true};
        m_maximum_recipient->amount()->setSatoshi(*result->snapshot->gross_remainder);
    }
}

quint8 WalletQmlModel::automaticMaximumCapabilities() const
{
    return quint8((m_metadata_known ? 1 : 0) | (m_private_keys_disabled ? 2 : 0) | (m_external_signer ? 4 : 0) |
                  (m_is_encrypted ? 8 : 0) | (m_is_locked ? 16 : 0) | (m_can_get_addresses ? 32 : 0) | (m_taproot ? 64 : 0));
}
void WalletQmlModel::invalidateAutomaticMaximum()
{
    if (!m_automatic_maximum) return;
    m_automatic_maximum.reset();
    // A foreign edit may reenter inside the automatic amount signal. The old
    // GUI receipt must not finish that newer intent before its draft is ready.
    if (m_updating_maximum) ++m_fee_estimate_request_id;
}
void WalletQmlModel::publishAutomaticMaximum(CAmount amount)
{
    if (!m_maximum_recipient || !m_send_recipients || m_stopping || !available()) return;
    const auto recipient = m_maximum_recipient;
    if (recipient->amount()->satoshi() == amount) return;
    m_automatic_maximum = AutomaticMaximum{recipient, recipient->identity(), recipient->validationRevision() + 1,
        m_fee_estimate_request_id, amount, m_send_recipients->count(), automaticMaximumCapabilities()};
    const QPointer<WalletQmlModel> observer{this};
    const bool previous = std::exchange(m_updating_maximum, true);
    recipient->amount()->setSatoshi(amount);
    if (observer) m_updating_maximum = previous;
}
void WalletQmlModel::scheduleFeesForRecipientValidation()
{
    if (m_automatic_maximum && !m_stopping && available() && m_send_recipients) {
        const auto& original = *m_automatic_maximum;
        if (original.recipient && m_maximum_recipient == original.recipient &&
            original.recipient->identity() == original.token && original.recipient->validationRevision() == original.revision &&
            original.recipient->amount()->satoshi() == original.amount && m_send_recipients->count() == original.count &&
            automaticMaximumCapabilities() == original.capabilities &&
            m_fee_estimate_request_id == original.fee_request) {
            if (m_send_recipients->draftSnapshot() && !original.recipient->validationPending()) m_automatic_maximum.reset();
            return;
        }
    }
    invalidateAutomaticMaximum();
    scheduleFeeEstimates();
}
void WalletQmlModel::scheduleFeeEstimates()
{
    if (!observationsAllowed() || m_updating_maximum || m_fee_read_services.timer == nullptr) return;
    m_automatic_maximum.reset();
    const QPointer<WalletQmlModel> observer{this};
    const auto prior_request = m_fee_estimate_request_id;
    const auto timer = m_fee_read_services.timer;
    const auto prior_owns = [observer, prior_request] {
        return observer && observer->observationsAllowed() && observer->m_fee_estimate_request_id == prior_request;
    };
    requestSendFacts();
    if (!prior_owns()) return;
    timer->stop();
    if (!prior_owns()) return;

    if (!m_wallet || !m_send_recipients || !ReadyForFeePreview(m_send_recipients->draftSnapshot(), m_send_recipients->recipientToken(m_maximum_recipient))) {
        clearFeeEstimates();
        return;
    }
    const auto request = ++m_fee_estimate_request_id;
    const auto owns = [observer, request] {
        return observer && observer->observationsAllowed() && observer->m_fee_estimate_request_id == request;
    };
    invalidateFeeRead();
    if (!owns()) return;
    const bool estimates_changed = !m_fee_estimates.isEmpty() || m_custom_fee_estimate.has_value();
    const bool pending_changed = !m_fee_estimate_pending;
    m_fee_estimates.clear(); m_custom_fee_estimate.reset(); m_fee_estimate_pending = true;
    if (estimates_changed || pending_changed) ++m_fee_estimate_revision;
    if (estimates_changed) {
        Q_EMIT estimatedFeeChanged(); if (!owns()) return;
        Q_EMIT sendAmountExhaustsBalanceChanged(); if (!owns()) return;
    }
    if (pending_changed) { Q_EMIT feeEstimatePendingChanged(); if (!owns()) return; }
    if (estimates_changed || pending_changed) { Q_EMIT feeEstimateRevisionChanged(); if (!owns()) return; }
    timer->restart([observer] { if (observer) observer->requestFeeEstimatesNow(); });
}

void WalletQmlModel::requestFeeEstimatesNow()
{
    if (!observationsAllowed()) return;
    if (m_stopping || !m_wallet || !m_send_recipients || coinSelectionPending()) {
        clearFeeEstimates();
        return;
    }
    FeeReadInput input;
    input.coin_control = m_coin_control;
    input.selected = selectedCoins();
    input.draft = m_send_recipients->draftSnapshot();
    input.maximum_token = m_send_recipients->recipientToken(m_maximum_recipient);
    if (!ReadyForFeePreview(input.draft, input.maximum_token)) { clearFeeEstimates(); return; }
    input.custom_fee_enabled = m_custom_fee_enabled;
    input.custom_fee_rate = m_custom_fee_rate;
    const quint64 request_id = ++m_fee_estimate_request_id;
    const QPointer<WalletQmlModel> observer{this};
    const auto scheduler = m_fee_read_services.scheduler;
    const auto state = m_fee_read_state;
    const auto read = m_fee_read_services.read;
    const bool announce_pending = !m_fee_estimate_pending;
    if (announce_pending) {
        m_fee_estimate_pending = true;
        ++m_fee_estimate_revision;
    }
    const auto owns = [observer, request_id] {
        return observer && observer->observationsAllowed() && !observer->m_stopping &&
            observer->m_fee_estimate_request_id == request_id;
    };
    // Capture the original intent before admission or presentation handoffs.
    // Every state decision, including dispatch and completion, stays on Domain;
    // Core owns only the immutable read effect and returns its actual result.
    const auto publish = [post = presentation::ReliablePost(m_fee_read_services.post), observer, request_id](FeeReadResult result) {
        post([observer, request_id, result = std::move(result)] {
            if (observer) observer->applyFeeEstimates(result.estimates, result.custom, request_id);
        });
    };
    const FeeReadRevision revision{request_id};
    const auto complete = [state, revision, publish](FeeReadResult result) {
        if (state->apply({FeeReadState::Kind::Complete, revision})) publish(std::move(result));
    };
    const auto admitted = execution::Submit(*scheduler,
        [state, revision, scheduler, input = std::move(input), read, complete]() mutable {
            if (!state->apply({FeeReadState::Kind::Dispatch, revision})) return;
            const auto effect = execution::Submit(*scheduler,
                [input = std::move(input), read]() mutable {
                    if (input.draft) {
                        const auto maximum = input.draft->positions.find(input.maximum_token);
                        input.remainder_index = maximum == input.draft->positions.end() ? -1 : static_cast<int>(maximum->second);
                        input.recipients.reserve(input.draft->recipients.size());
                        for (const auto& recipient : input.draft->recipients) input.recipients.push_back({recipient.address.toStdString(), recipient.amount});
                    }
                    if (input.selected) for (const auto& outpoint : *input.selected) input.coin_control.Select(outpoint);
                    ApplySelectedInputsPolicy(input.coin_control);
                    return read(input);
                }, complete, [complete](std::exception_ptr) { complete({}); },
                {execution::Lane::Core, 2048, {}, execution::WorkClass::Observation});
            if (!effect) complete({});
        }, [] {}, [complete](std::exception_ptr) { complete({}); },
        {execution::Lane::Domain, 2048, {}, execution::WorkClass::Observation});
    if (!admitted) {
        if (owns()) observer->applyFeeEstimates({}, {}, request_id);
        return;
    }
    if (announce_pending && owns()) {
        Q_EMIT observer->feeEstimatePendingChanged();
        if (owns()) Q_EMIT observer->feeEstimateRevisionChanged();
    }
}

void WalletQmlModel::applyFeeEstimates(const QHash<unsigned int, SendFeePreview>& estimates,
                                       const std::optional<SendFeePreview>& custom_estimate,
                                       const quint64 request_id)
{
    if (!observationsAllowed() || m_stopping || request_id != m_fee_estimate_request_id) {
        return;
    }
    const QPointer<WalletQmlModel> observer{this};

    bool estimates_changed{m_fee_estimates != estimates};
    bool custom_estimate_changed{m_custom_fee_estimate != custom_estimate};
    if (estimates_changed) {
        m_fee_estimates = estimates;
    }
    if (custom_estimate_changed) {
        m_custom_fee_estimate = custom_estimate;
    }
    const auto preview = m_custom_fee_enabled ? m_custom_fee_estimate
        : (m_fee_estimates.contains(feeTargetBlocks()) ? std::make_optional(m_fee_estimates.value(feeTargetBlocks())) : std::nullopt);
    if (m_maximum_recipient && preview && preview->maximum_amount) {
        publishAutomaticMaximum(*preview->maximum_amount);
        if (!observer || !observationsAllowed() || m_stopping || request_id != m_fee_estimate_request_id) return;
    }
    if (estimates_changed || custom_estimate_changed) {
        Q_EMIT estimatedFeeChanged();
        if (!observer || !observationsAllowed() || m_stopping || request_id != m_fee_estimate_request_id) return;
        Q_EMIT sendAmountExhaustsBalanceChanged();
        if (!observer || !observationsAllowed() || m_stopping || request_id != m_fee_estimate_request_id) return;
    }

    bool pending_changed{m_fee_estimate_pending};
    if (pending_changed) {
        m_fee_estimate_pending = false;
        Q_EMIT feeEstimatePendingChanged();
        if (!observer || !observationsAllowed() || m_stopping || request_id != m_fee_estimate_request_id) return;
    }

    if (estimates_changed || custom_estimate_changed || pending_changed) {
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }
}

void WalletQmlModel::clearFeeEstimates()
{
    if (!observationsAllowed()) return;
    const QPointer<WalletQmlModel> observer{this};
    const auto request = ++m_fee_estimate_request_id;
    const auto owns = [observer, request] {
        return observer && observer->observationsAllowed() && observer->m_fee_estimate_request_id == request;
    };
    invalidateFeeRead();
    if (!owns()) return;
    const bool estimates_changed = !m_fee_estimates.isEmpty() || m_custom_fee_estimate.has_value();
    const bool pending_changed = m_fee_estimate_pending;
    m_fee_estimates.clear(); m_custom_fee_estimate.reset(); m_fee_estimate_pending = false;
    if (estimates_changed || pending_changed) ++m_fee_estimate_revision;
    if (estimates_changed) {
        Q_EMIT estimatedFeeChanged(); if (!owns()) return;
        Q_EMIT sendAmountExhaustsBalanceChanged(); if (!owns()) return;
    }
    if (pending_changed) { Q_EMIT feeEstimatePendingChanged(); if (!owns()) return; }
    if (estimates_changed || pending_changed) Q_EMIT feeEstimateRevisionChanged();
}

quint64 WalletQmlModel::requestPrepareTransaction()
{
    return requestPrepareTransactionInternal();
}

quint64 WalletQmlModel::requestPrepareTransactionWithPassphrase(const QString& passphrase)
{
    return requestPrepareTransactionInternal(passphrase);
}

void WalletQmlModel::invalidatePreparation()
{
    if (!m_updating_maximum) ++m_preparation_revision;
}

quint64 WalletQmlModel::requestPrepareTransactionInternal(std::optional<QString> passphrase)
{
    if (!observationsAllowed() || coinSelectionPending() || psbtActionBusy() || preparationBusy() || m_custom_target_pending || m_approval || (sendBusy() && m_send_review && m_send_review == m_current_transaction) || securityBusy() || m_sending_commit->load()) return 0;
    // Terminal outcomes have independent Native value ownership. Retire their
    // operation slots before reserving a successor so a capacity failure can
    // recover without requiring the wallet facade to be destroyed.
    m_preparation.reset(); m_preparation_lease.reset();
    const auto ticket = ++m_preparation_id;
    const QPointer<WalletQmlModel> observer{this};
    const auto reject = [observer, ticket](PreparationState state, const QString& error) {
        if (!observer || !observer->observationsAllowed() || observer->m_preparation_id != ticket) return;
        observer->m_preparation_state = state;
        const auto revision = observer->m_preparation_revision;
        observer->setTransactionStatus(error);
        if (observer && observer->observationsAllowed() && observer->m_preparation_id == ticket &&
            observer->m_preparation_revision == revision && observer->m_preparation_state == state) Q_EMIT observer->preparationChanged();
    };
    if (!available() || !m_opening || !m_send_recipients || !m_send_recipients->allValid()) {
        const bool live = available();
        const QString validation_error = m_send_recipients ? m_send_recipients->validationError() : QString{};
        reject(live ? PreparationState::PreparationFailed : PreparationState::PreparationClosed,
            !live ? tr("This wallet is closing.") : !validation_error.isEmpty() ? validation_error : tr("Enter at least one valid recipient to continue."));
        return ticket;
    }
    auto request = std::make_shared<walletprepare::Request>();
    request->control = m_coin_control;
    request->selected = selectedCoins();
    if (m_send_facts_known) request->available_balance = m_selected_send_balance;
    ApplySelectedInputsPolicy(request->control);
    request->draft = m_send_recipients->draftSnapshot();
    request->maximum_token = m_send_recipients->recipientToken(m_maximum_recipient);
    request->target_blocks = feeTargetBlocks();
    request->fee_rate = estimatedFeeRate();
    request->display_unit = m_display_unit;
    const auto opening = m_opening;
    const auto revision = m_preparation_revision;
    const QPointer<WalletQmlModelTransaction> previous{m_current_transaction};
    const bool had_previous = bool(previous);
    m_preparation_previous_review = previous;
    m_preparation_had_previous_review = had_previous;
    m_preparation_state = PreparationState::PreparationPending;
    const auto unchanged = [observer, opening, ticket, revision, previous, had_previous] {
        return observer && observer->observationsAllowed() && observer->m_preparation_id == ticket &&
            observer->m_opening == opening && opening->available() && observer->m_preparation_revision == revision &&
            observer->preparationBusy() &&
            (had_previous ? previous && observer->m_current_transaction == previous.data() : !observer->m_current_transaction);
    };
    const auto stop_if_changed = [observer, opening, ticket, revision, previous, had_previous, unchanged] {
        if (unchanged()) return false;
        if (observer && observer->observationsAllowed() && observer->m_preparation_id == ticket &&
            observer->m_opening == opening && observer->preparationBusy() &&
            (observer->m_preparation_revision != revision ||
             (had_previous ? !previous || observer->m_current_transaction != previous.data() : bool(observer->m_current_transaction)))) {
            observer->m_preparation_state = PreparationState::PreparationStale;
            Q_EMIT observer->preparationChanged();
        }
        return true;
    };
    if (passphrase) {
        auto captured = secretinput::Capture(opening->scheduler(), *passphrase);
        if (stop_if_changed()) return ticket;
        if (!captured) {
            reject(PreparationState::PreparationFailed, tr("The password input could not be queued. No transaction was prepared."));
            return ticket;
        }
        request->captured_passphrase = std::move(captured.input);
    }
    if (m_custom_fee_enabled) {
        const auto rate = ParseCustomFeeRatePerKvB(m_custom_fee_rate, m_minimum_relay_fee.value_or(DEFAULT_MIN_RELAY_TX_FEE));
        if (!rate) {
            reject(PreparationState::PreparationFailed, tr("Enter a valid custom fee rate."));
            return ticket;
        }
        request->control.m_confirm_target.reset();
        request->control.m_feerate = CFeeRate{*rate};
        request->control.fOverrideFeeRate = true;
    } else {
        request->control.m_feerate.reset();
        if (!request->control.m_confirm_target) request->control.m_confirm_target = DEFAULT_STANDARD_FEE_TARGET;
        ApplyRegtestStaticFeeOverride(request->control);
    }
    const size_t bytes{4096 + (request->passphrase ? request->passphrase->capacity() : 0)
        + secretinput::Bytes(request->captured_passphrase)};
    const auto action = opening->startAction();
    if (stop_if_changed()) return ticket;
    if (!action) {
        reject(PreparationState::PreparationFailed, tr("The transaction preparation could not be queued."));
        return ticket;
    }
    m_preparation_requested_revision = revision;
    auto operation = std::make_shared<execution::OwnedCommand<walletprepare::Result>>(opening->scheduler(),
        [post = m_fee_read_services.post, observer] {
            post([observer] { if (observer) observer->publishPreparation(); });
        });
    if (stop_if_changed()) return ticket;
    auto native_owner = std::make_shared<std::shared_ptr<execution::OwnedCommand<walletprepare::Result>>>(operation);
    auto hold = opening->holdValues([native_owner] { native_owner->reset(); });
    if (stop_if_changed()) return ticket;
    auto output = walletopening::RetainValues<walletprepare::Result>(opening, [] { return std::make_unique<walletprepare::Result>(); });
    if (stop_if_changed()) return ticket;
    if (!hold || !output) {
        reject(PreparationState::PreparationFailed, tr("Transaction review capacity is unavailable."));
        return ticket;
    }
    m_preparation = operation; m_preparation_lease.emplace(std::move(*hold.lease));
    const auto admitted = operation->startWith([opening, action, request, bytes, output](auto done, auto failed) {
        return opening->perform(action, [request, output](interfaces::Wallet& wallet) {
            return walletprepare::ExecuteInto(wallet, std::move(*request), output);
        }, std::move(done), std::move(failed), bytes);
    });
    if (stop_if_changed()) return ticket;
    if (!admitted) {
        reject(PreparationState::PreparationFailed, tr("The transaction preparation could not be queued."));
    } else {
        // Capture and admit the original request before a status handler can
        // edit the draft. Pending state is coherent before that handoff.
        clearTransactionStatus();
        if (unchanged()) Q_EMIT observer->preparationChanged();
    }
    return ticket;
}

void WalletQmlModel::publishPreparation()
{
    if (!observationsAllowed()) return;
    if (!m_preparation || !preparationBusy() || !available()) return;
    const auto result = m_preparation->result();
    if (!result) return;
    const bool same_review = m_preparation_had_previous_review
        ? m_preparation_previous_review && m_current_transaction == m_preparation_previous_review.data()
        : !m_current_transaction;
    const QPointer<WalletQmlModel> observer{this};
    const auto original_command = m_preparation;
    const auto original_opening = m_opening;
    const auto original_id = m_preparation_id;
    const auto original_revision = m_preparation_revision;
    const auto original_requested_revision = m_preparation_requested_revision;
    QPointer<WalletQmlModelTransaction> projected_review{m_current_transaction};
    bool had_projected_review{bool(projected_review)};
    const auto owns = [&] {
        return observer && observer->observationsAllowed() && observer->available() &&
            observer->m_opening == original_opening && observer->m_preparation == original_command &&
            observer->m_preparation_id == original_id && observer->m_preparation_revision == original_revision &&
            observer->m_preparation_requested_revision == original_requested_revision &&
            (had_projected_review ? projected_review && observer->m_current_transaction == projected_review : !observer->m_current_transaction);
    };
    if (m_preparation_requested_revision != m_preparation_revision || !same_review) {
        m_preparation_state = PreparationState::PreparationStale;
        Q_EMIT preparationChanged(); return;
    }
    auto value = result->value.value_or(walletprepare::Result{});
    if (result->error) value.error = walletprepare::Error::Failed;
    if (value.error != walletprepare::Error::None || !value.review) {
        m_preparation_state = value.error == walletprepare::Error::NeedsUnlock ? PreparationState::PreparationNeedsUnlock
            : value.error == walletprepare::Error::RelockFailed || value.error == walletprepare::Error::SweepUnknown ? PreparationState::PreparationUncertain : PreparationState::PreparationFailed;
        QString message = value.message;
        if (message.isEmpty()) {
            switch (value.error) {
            case walletprepare::Error::InsufficientBalance: message = tr("The wallet does not have enough balance for this transaction."); break;
            case walletprepare::Error::InsufficientSelectedBalance: message = tr("Selected inputs do not cover the amount plus fee"); break;
            case walletprepare::Error::NeedsUnlock: message = tr("Enter your wallet password to prepare this transaction."); break;
            case walletprepare::Error::IncorrectPassphrase: message = tr("The wallet password is incorrect."); break;
            case walletprepare::Error::RelockFailed: message = tr("The wallet lock could not be restored. Check its security state before retrying."); break;
            case walletprepare::Error::SweepUnknown: message = tr("The available wallet funds could not be verified. Refresh the wallet and try again."); break;
            default: message = tr("The transaction could not be prepared."); break;
            }
        }
        setTransactionStatus(message, value.error == walletprepare::Error::NeedsUnlock || value.error == walletprepare::Error::IncorrectPassphrase);
        if (!owns()) return;
        original_opening->refreshMetadata();
        if (!owns()) return;
        Q_EMIT preparationChanged(); return;
    }
    WalletQmlModelTransaction* prepared{nullptr};
    try { prepared = new WalletQmlModelTransaction(value.review, original_opening, m_fee_read_services.post, this); }
    catch (const std::exception&) {
        if (!owns()) return;
        m_preparation_state = PreparationState::PreparationFailed;
        setTransactionStatus(tr("Transaction review capacity is unavailable."));
        if (owns()) Q_EMIT preparationChanged();
        return;
    }
    if (!owns()) { if (observer) prepared->deleteLater(); return; }
    auto* previous = m_current_transaction;
    m_current_transaction = prepared;
    projected_review = prepared; had_projected_review = true;
    if (previous) previous->deleteLater();
    m_psbt_fee_request.reset();
    m_psbt_fee_pending = false;
    m_psbt_fee_proof = value.review->proof
        ? std::shared_ptr<const psbtfee::Analysis>{value.review, value.review->proof.get()}
        : std::shared_ptr<const psbtfee::Analysis>{};
    m_current_psbt.reset(); m_psbt_document_lease.reset();
    m_current_transaction_source = CurrentTransactionSource::SendDraft;
    m_current_transaction_sweeps_wallet = value.review->sweeps;
    m_current_transaction_can_send = value.review->can_send;
    m_current_transaction_can_broadcast = false;
    m_current_transaction_review_message.clear();
    prepared->setDisplayUnit(m_display_unit);
    if (!owns()) return;
    adoptCurrentFlow(value.review->flow);
    if (!owns()) return;
    m_preparation_state = PreparationState::PreparationReady;
    original_opening->refreshMetadata();
    if (!owns()) return;
    Q_EMIT currentTransactionChanged();
    if (!owns()) return;
    Q_EMIT preparationChanged();
}

quint64 WalletQmlModel::approveExternalSignerTransaction()
{
    if (!observationsAllowed() || signerApprovalState() == ApprovalState::ApprovalUncertain || psbtActionBusy() || m_approval ||
        preparationBusy() || sendBusy() || m_sending_commit->load() || !available() || !m_opening || !m_current_transaction ||
        !m_current_transaction->getWtx()) return 0;
    const QPointer<WalletQmlModel> observer{this};
    const QPointer<WalletQmlModelTransaction> review{m_current_transaction};
    const auto opening = m_opening;
    const auto scheduler = opening->scheduler();
    const auto post = presentation::ReliablePost(m_fee_read_services.post);
    const auto view = m_psbt_view_epoch;
    const bool fee_known = review->feeKnown();
    const walletsigning::Request request{review->getWtx(), m_current_psbt, m_psbt_fee_proof, review->reviewedFee()};
    const auto ticket = ++m_approval_id;
    const auto owns = [observer, opening, ticket] {
        return observer && observer->observationsAllowed() && observer->available() &&
            observer->m_opening == opening && observer->m_approval_id == ticket;
    };
    const auto owns_view = [owns, observer, review, view, reviewed = request.reviewed] {
        return owns() && review && observer->m_current_transaction == review && observer->m_psbt_view_epoch == view &&
            review->getWtx() == reviewed;
    };
    const auto rejected = [owns, owns_view, observer](const QString& error) {
        if (!owns()) return;
        // A withdrawn original view has no device effect and cannot receive an
        // old error. Close only this attempt's phase, without changing review.
        const bool same = owns_view();
        observer->m_approval_state = same ? ApprovalState::ApprovalFailed : ApprovalState::ApprovalInitial;
        observer->m_approval_error = same ? error : QString{};
        Q_EMIT observer->signerApprovalChanged();
    };
    if (!fee_known) {
        const auto error = tr("The transaction fee is missing or invalid.");
        rejected(error);
        if (owns_view()) Q_EMIT observer->externalSignerApprovalFailed(error, false);
        return ticket;
    }
    const auto gate = m_sending_commit;
    if (gate->exchange(true)) return 0;
    auto release = qScopeGuard([gate] { gate->store(false); });
    auto previous_transaction = std::move(m_approval_transaction);
    (void)previous_transaction;
    m_approval_state = ApprovalState::ApprovalWaiting;
    m_approval_error.clear();
    m_approval_review = review;
    m_approval_transaction = request.reviewed;
    const auto action = opening->startAction();
    if (!owns_view()) { rejected({}); return ticket; }
    if (!action) { rejected(tr("External signer approval could not be queued.")); return ticket; }
    auto command = std::make_shared<execution::OwnedCommand<walletsigning::Result>>(scheduler,
        [post, observer, gate, ticket] {
            gate->store(false);
            post([observer, ticket] {
                if (observer && observer->m_approval_id == ticket) observer->publishApproval();
            });
        });
    if (!owns_view()) { rejected({}); return ticket; }
    auto output = walletopening::RetainValues<walletsigning::Result>(opening, [] { return std::make_unique<walletsigning::Result>(); });
    if (!owns_view()) { rejected({}); return ticket; }
    if (!output) { rejected(tr("External signer result capacity is unavailable.")); return ticket; }
    m_approval = command;
    const auto admitted = command->startWith([opening, action, request, output](auto done, auto failed) {
        return opening->perform(action, [request, output](interfaces::Wallet& wallet) {
            return walletsigning::ApproveInto(wallet, request, output);
        }, std::move(done), std::move(failed));
    });
    // Admission may retire the observer. The accepted original device action
    // still owns its gate, immutable request and reserved output until done.
    if (admitted) release.dismiss();
    if (!owns()) return ticket;
    if (!admitted) {
        if (observer->m_approval == command) observer->m_approval.reset();
        rejected(tr("External signer approval could not be queued."));
        return ticket;
    }
    if (owns_view()) Q_EMIT observer->signerApprovalChanged();
    return ticket;
}

void WalletQmlModel::publishApproval()
{
    if (!observationsAllowed()) return;
    if (!m_approval) return;
    const auto result = m_approval->result();
    if (!result) return;
    if (!available()) { m_approval.reset(); m_approval_state = ApprovalState::ApprovalClosed; return; }
    if (m_approval_review != m_current_transaction || !m_current_transaction ||
        m_approval_transaction != m_current_transaction->getWtx()) {
        m_approval.reset();
        m_approval_state = ApprovalState::ApprovalInitial; m_approval_error.clear();
        Q_EMIT signerApprovalChanged(); return;
    }
    const auto value = result->value.value_or(walletsigning::Result{.error = walletsigning::Error::Uncertain});
    m_approval_error.clear();
    switch (value.error) {
    case walletsigning::Error::None: break;
    case walletsigning::Error::InvalidFee: m_approval_error = tr("The transaction fee changed after review."); break;
    case walletsigning::Error::ChangedTransaction: m_approval_error = tr("External signer changed the reviewed transaction."); break;
    case walletsigning::Error::SignerNotFound: m_approval_error = tr("External signer not found. Connect one device and try again."); break;
    case walletsigning::Error::SignerFailed: m_approval_error = tr("External signer failed to sign. Try again."); break;
    case walletsigning::Error::Fill: m_approval_error = value.fill_error ? PsbtQmlModel::PsbtErrorText(*value.fill_error) : tr("The transaction could not be signed."); break;
    case walletsigning::Error::Failed: m_approval_error = tr("External signer approval failed."); break;
    case walletsigning::Error::Uncertain: m_approval_error = tr("The signer outcome could not be confirmed. Reconcile the PSBT before retrying."); break;
    }
    if (value.document) m_current_psbt = value.document;
    const QPointer<WalletQmlModel> observer{this};
    if (!m_approval_error.isEmpty()) {
        m_approval.reset();
        m_approval_state = value.error == walletsigning::Error::Uncertain ? ApprovalState::ApprovalUncertain : ApprovalState::ApprovalFailed;
        const auto error = m_approval_error;
        Q_EMIT signerApprovalChanged();
        if (!observer || !available()) return;
        if (value.document) { Q_EMIT currentTransactionChanged(); if (!observer || !available()) return; }
        Q_EMIT externalSignerApprovalFailed(error, value.error == walletsigning::Error::SignerNotFound);
        return;
    }
    m_current_transaction_can_send = !value.partial;
    m_current_transaction_can_broadcast = false;
    m_current_transaction_review_message = value.partial ? tr("Signed on external signer. More signatures are required.") : QString{};
    if (value.signed_transaction) {
        m_current_transaction->setWtx(value.signed_transaction);
        if (!observer || !available()) return;
        m_approval_transaction = value.signed_transaction;
    }
    m_approval_state = value.partial ? ApprovalState::ApprovalPartiallySigned : ApprovalState::ApprovalSigned;
    Q_EMIT currentTransactionChanged();
    if (!observer || !available()) return;
    m_approval.reset();
    if (m_approval_review != m_current_transaction) {
        m_approval_state = ApprovalState::ApprovalInitial;
        Q_EMIT signerApprovalChanged(); return;
    }
    Q_EMIT signerApprovalChanged();
    if (!observer || !available()) return;
    if (value.partial) Q_EMIT externalSignerApprovalPartiallySucceeded();
    else Q_EMIT externalSignerApprovalSucceeded();
}

struct WalletQmlModel::SentLabelCapture {
    std::shared_ptr<walletrequests::Store> store;
    std::shared_ptr<const walletopening::Session::Action> action;
    std::shared_ptr<QMap<QString, QString>> labels;
    std::optional<execution::ResourceLease> label_lifetime;
    bool valid{false};
    std::shared_ptr<QObject> bridge;
    QPointer<WalletQmlModel> observer;
    std::shared_ptr<const int> ticket;
};

WalletQmlModel::SentLabelCapture WalletQmlModel::captureSentRecipientLabels(const std::shared_ptr<const walletopening::Session::Action>& action)
{
    if (m_current_transaction_source != CurrentTransactionSource::SendDraft || !m_current_transaction || m_current_transaction->recipientLabels().isEmpty()) return {};
    SentLabelCapture capture;
    capture.store = m_request_store; capture.action = action; capture.bridge = m_notification_bridge;
    capture.observer = this; capture.ticket = std::make_shared<const int>(0);
    capture.labels = std::make_shared<QMap<QString, QString>>();
    if (!m_current_transaction->recipientLabels().isEmpty()) {
        auto held = m_opening->holdValues([labels = capture.labels] { labels->clear(); });
        if (!held) return capture;
        capture.label_lifetime.emplace(std::move(*held.lease));
        *capture.labels = m_current_transaction->recipientLabels();
    }
    capture.valid = true;
    return capture;
}

void WalletQmlModel::publishSentRecipientLabels()
{
    if (!observationsAllowed()) return;
    if (m_admission_closed) return;
    if (m_sent_labels_operation) {
        const auto result = m_sent_labels_operation->result();
        if (result) {
            m_sent_labels_pending = false;
            m_sent_labels_error = result->error == walletrequests::Error::None ? QString{}
                : result->error == walletrequests::Error::Uncertain
                    ? tr("The transaction was committed, but its outgoing note results could not be confirmed.")
                    : tr("The transaction was committed, but some outgoing notes could not be saved.");
            m_sent_labels_operation.reset();
        }
    }
    Q_EMIT recipientLabelsChanged();
}


quint64 WalletQmlModel::requestSendTransaction()
{
    return requestSendTransactionInternal();
}

quint64 WalletQmlModel::requestSendTransactionWithPassphrase(const QString& passphrase)
{
    return requestSendTransactionInternal(passphrase);
}

quint64 WalletQmlModel::requestSendTransactionInternal(std::optional<QString> passphrase)
{
    if (psbtActionBusy() || m_approval || preparationBusy()) return 0;
    if ((m_send_state == SendState::SendCommitted || m_send_state == SendState::SendUncertain) && m_send_review && m_send_review == m_current_transaction) return 0;
    if (sendBusy() || m_sending_commit->exchange(true)) return 0;
    const auto request_id = ++m_send_request_id;
    const auto gate = m_sending_commit;
    auto rejected = qScopeGuard([gate] { gate->store(false); });
    const QPointer<WalletQmlModel> self{this};
    const QPointer<WalletQmlModelTransaction> approved{m_current_transaction};
    const auto approved_revision = m_preparation_revision;
    clearTransactionStatus();
    if (!self || !observationsAllowed() || approved != m_current_transaction || approved_revision != m_preparation_revision || request_id != m_send_request_id) return request_id;
    if (signerApprovalState() == ApprovalState::ApprovalUncertain) {
        m_send_state = SendState::SendUncertain;
        m_send_review = m_current_transaction;
        setTransactionStatus(tr("The signer outcome could not be confirmed. Reconcile the PSBT before retrying."));
        if (self) Q_EMIT sendOperationChanged();
        return request_id;
    }
    if (!available() || !m_current_transaction || !m_current_transaction->getWtx()) {
        m_send_state = available() ? SendState::SendFailed : SendState::SendClosed;
        setTransactionStatus(tr("Review a transaction before sending it."));
        if (self && observationsAllowed() && request_id == m_send_request_id) Q_EMIT sendOperationChanged();
        return request_id;
    }
    if (!m_current_transaction_can_send) {
        m_send_state = SendState::SendFailed;
        const QString reason = !m_current_transaction_review_message.isEmpty() ? m_current_transaction_review_message :
            (m_metadata_known && m_private_keys_disabled && !m_external_signer ? tr("This wallet cannot sign transactions.") : tr("This transaction is not ready to send."));
        setTransactionStatus(reason);
        if (self && observationsAllowed() && approved == m_current_transaction && approved_revision == m_preparation_revision && request_id == m_send_request_id) Q_EMIT sendOperationChanged();
        return request_id;
    }
    const auto action = m_opening->startAction();
    if (!action) { m_send_state = SendState::SendFailed; setTransactionStatus(tr("The send could not be queued.")); if (self && observationsAllowed() && request_id == m_send_request_id) Q_EMIT sendOperationChanged(); return request_id; }
    auto notes = captureSentRecipientLabels(action);
    if (notes.ticket && !notes.valid) { m_send_state = SendState::SendFailed; setTransactionStatus(tr("Outgoing notes could not be queued. Try again.")); if (self && observationsAllowed() && request_id == m_send_request_id) Q_EMIT sendOperationChanged(); return request_id; }
    walletsend::Request request;
    request.reviewed = m_current_transaction->getWtx(); request.psbt = m_current_psbt;
    request.assessed_complete = m_current_transaction_can_broadcast;
    request.proof = m_psbt_fee_proof; request.approved_fee = m_current_transaction->reviewedFee();
    if (passphrase) {
        auto captured = secretinput::Capture(m_opening->scheduler(), *passphrase);
        if (!captured) {
            m_send_state = SendState::SendFailed;
            setTransactionStatus(tr("The password input could not be queued. The transaction was not sent."));
            if (self) Q_EMIT sendOperationChanged();
            return request_id;
        }
        request.captured_passphrase = std::move(captured.input);
    }
    request.consent = std::make_shared<walletsend::CommitConsent>();
    m_send_consent = request.consent;
    if (notes.labels) request.notes = *notes.labels;
    const QPointer<WalletQmlModel> observer{this};
    const auto ticket = std::make_shared<const int>(0);
    auto notify = [gate, post = m_fee_read_services.post, observer, ticket](walletsend::Notice notice) {
        if (notice == walletsend::Notice::Finished) gate->store(false);
        post([observer, ticket, notice] {
            if (!observer || observer->m_stopping) return;
            if (notice == walletsend::Notice::Finished && observer->m_send_ticket == ticket) observer->publishSendOperation();
            else if (notice == walletsend::Notice::Notes && observer->m_sent_labels_ticket == ticket) observer->publishSentRecipientLabels();
        });
    };
    m_send_review = m_current_transaction; m_send_result_txid.clear(); m_send_submission = SendSubmission::SubmissionUnknown; m_send_ticket = ticket;
    m_send_operation = std::make_shared<walletsend::Operation>(m_opening, m_request_store, std::move(notify));
    m_send_state = SendState::SendPending;
    const auto admission = m_send_operation->start(std::move(request), action);
    if (admission) rejected.dismiss();
    else { m_send_state = SendState::SendFailed; setTransactionStatus(tr("The send could not be queued.")); }
    if (self && observationsAllowed() && request_id == m_send_request_id) Q_EMIT sendOperationChanged();
    return request_id;
}

void WalletQmlModel::publishSendOperation()
{
    if (!observationsAllowed()) return;
    if (m_admission_closed) return;
    if (!m_send_operation) return;
    const QPointer<WalletQmlModel> self{this};
    const auto result = m_send_operation->result(); if (!result) return;
    if (m_send_state != SendState::SendPending) {
        if (result->notes && result->notes == m_sent_labels_operation) publishSentRecipientLabels();
        return;
    }
    if (result->committed && result->notes_requested) {
        m_sent_labels_ticket = m_send_ticket;
        m_sent_labels_operation = result->notes;
        m_sent_labels_txid = QString::fromStdString(result->committed->GetHash().GetHex());
        m_sent_labels_pending = bool(result->notes);
        m_sent_labels_error = result->notes ? QString{}
            : result->notes_rejection == execution::Rejection::Transport
                ? tr("The transaction was committed, but its outgoing note results could not be confirmed.")
                : tr("The transaction was committed, but its outgoing notes could not be queued.");
        publishSentRecipientLabels();
        if (!self) return;
    }
    m_opening->refreshMetadata();
    // A consumed effect belongs to its original review. A successor gets no
    // old success, password prompt, mutation or transaction error.
    if (!available() || !m_send_review || m_send_review != m_current_transaction) {
        m_send_state = SendState::SendClosed; Q_EMIT sendOperationChanged(); return;
    }
    if (result->committed) {
        switch (result->submission) {
        case walletsend::Submission::Unknown: m_send_submission = SendSubmission::SubmissionUnknown; break;
        case walletsend::Submission::Mempool: m_send_submission = SendSubmission::SubmissionMempool; break;
        case walletsend::Submission::Confirmed: m_send_submission = SendSubmission::SubmissionConfirmed; break;
        case walletsend::Submission::NotInMempool: m_send_submission = SendSubmission::SubmissionNotInMempool; break;
        case walletsend::Submission::Conflicted: m_send_submission = SendSubmission::SubmissionConflicted; break;
        case walletsend::Submission::Abandoned: m_send_submission = SendSubmission::SubmissionAbandoned; break;
        }
        m_send_state = SendState::SendCommitted;
        m_send_result_txid = QString::fromStdString(result->committed->GetHash().GetHex());
        m_current_transaction_can_send = false; m_current_transaction_can_broadcast = false;
        m_current_transaction->setWtx(result->committed);
        if (!self || !m_send_review || m_send_review != m_current_transaction) return;
        m_current_psbt.reset(); m_psbt_document_lease.reset(); m_current_transaction_source = CurrentTransactionSource::None;
        m_current_transaction_can_send = false; m_current_transaction_can_broadcast = false;
        m_current_transaction_review_message.clear(); clearTransactionStatus();
        if (!self || !m_send_review || m_send_review != m_current_transaction) return;
        clearSelectedCoins();
        if (!self) return;
    } else {
        using Error = walletsend::Error;
        m_send_state = result->error == Error::NeedsUnlock || result->error == Error::IncorrectPassphrase ? SendState::SendNeedsUnlock
            : result->error == Error::Uncertain ? SendState::SendUncertain : SendState::SendFailed;
        QString message;
        switch (result->error) {
        case Error::NeedsUnlock: message = tr("Enter your wallet password to send this transaction."); break;
        case Error::IncorrectPassphrase: message = tr("The wallet password is incorrect."); break;
        case Error::CannotSign: message = tr("This wallet cannot sign transactions."); break;
        case Error::InvalidFee: message = tr("The transaction fee is missing or changed after review."); break;
        case Error::ChangedTransaction: message = tr("External signer changed the reviewed transaction."); break;
        case Error::Incomplete: message = tr("Only PSBTs this wallet can fully sign are supported right now."); break;
        case Error::Signer: message = result->signer_error ? PsbtQmlModel::PsbtErrorText(*result->signer_error) : tr("Signing failed."); break;
        case Error::Uncertain: message = tr("The transaction result could not be confirmed. Check wallet activity before retrying."); break;
        case Error::Cancelled: message = tr("The transaction review was discarded before sending."); break;
        case Error::Rejected: message = tr("The send could not be queued."); break;
        case Error::None: message = tr("The transaction was not committed."); break;
        }
        if (result->relock_failed) { m_send_state = SendState::SendUncertain; message = tr("The wallet could not be relocked. Check wallet status before retrying."); }
        if (m_send_state == SendState::SendUncertain) m_current_transaction_can_send = false;
        setTransactionStatus(message, m_send_state == SendState::SendNeedsUnlock);
        if (!self) return;
    }
    if (m_send_state == SendState::SendCommitted || m_send_state == SendState::SendUncertain) {
        Q_EMIT currentTransactionChanged();
        if (!self) return;
    }
    Q_EMIT sendOperationChanged();
}

quint64 WalletQmlModel::requestImportPsbtFromFile(const QString& path) { return startPsbtAction(PsbtActionKind::Import, path); }
quint64 WalletQmlModel::requestSaveCurrentTransactionAsPsbt(const QString& path) { return startPsbtAction(PsbtActionKind::Save, path); }
quint64 WalletQmlModel::requestBroadcastCurrentTransaction() { return startPsbtAction(PsbtActionKind::Broadcast); }
QString WalletQmlModel::psbtSavedPath() const
{
    if (!m_psbt_receipt) return {};
    std::lock_guard lock{m_psbt_receipt->mutex}; return m_psbt_receipt->saved ? m_psbt_receipt->path : QString{};
}
QString WalletQmlModel::psbtSubmittedTxid() const
{
    if (!m_psbt_receipt) return {};
    std::lock_guard lock{m_psbt_receipt->mutex}; return m_psbt_receipt->submitted ? m_psbt_receipt->txid : QString{};
}
quint64 WalletQmlModel::startPsbtAction(PsbtActionKind kind, const QString& requested_path)
{
    const bool replacing_import = kind == PsbtActionKind::Import && m_psbt_action_kind == PsbtActionKind::Import;
    if ((psbtActionBusy() && !replacing_import) || preparationBusy() || sendBusy() || securityBusy() || m_sending_commit->load() || m_approval || m_admission_closed || (m_wallet && !available())) return 0;
    const QPointer<WalletQmlModel> observer{this};
    const QPointer<WalletQmlModelTransaction> review{m_current_transaction};
    const bool had_review = bool(review);
    const auto opening = m_opening;
    const auto scheduler = opening ? opening->scheduler() : m_psbt_fee_services.scheduler;
    const auto post = presentation::ReliablePost(m_fee_read_services.post);
    const auto path = QString{requested_path};
    const auto document = m_current_psbt;
    const auto proof = m_psbt_fee_proof;
    const auto reviewed = review ? review->getWtx() : CTransactionRef{};
    const auto fee = review ? review->reviewedFee() : std::optional<CAmount>{};
    const bool fee_known = review && review->feeKnown();
    const bool fill = !document || (m_current_transaction_source != CurrentTransactionSource::ImportedPsbt && m_current_transaction_can_send);
    const int unit = m_display_unit;
    const psbtwork::Context context{m_psbt_fee_services.lookup, psbtwork::NodeBroadcast(m_node)};
    const bool missing_review = kind != PsbtActionKind::Import && !reviewed;
    const bool unavailable_broadcast = kind == PsbtActionKind::Broadcast && (!m_node || !document || !m_current_transaction_can_broadcast);
    // Keep previous owned packets alive locally until after all new handoffs;
    // releasing an old reservation can itself call an injected resource port.
    auto previous_action = std::move(m_psbt_action);
    auto previous_lease = std::move(m_psbt_action_lease);
    m_psbt_action_lease.reset(); // Destroy only the now-empty moved-from handle.
    (void)previous_action; (void)previous_lease;
    const auto id = ++m_psbt_action_id;
    m_psbt_action_kind = kind; m_psbt_action_state = PsbtActionState::PsbtPending;
    m_psbt_action_error.clear(); m_psbt_import_result = PsbtImportResult::PsbtUnsupported;
    if (kind == PsbtActionKind::Import) ++m_psbt_view_epoch;
    const auto view = m_psbt_view_epoch;
    m_psbt_action_view_epoch = view;
    m_psbt_action_review = review; m_psbt_action_had_review = had_review;
    m_psbt_action_document = document; m_psbt_action_had_document = bool(document);
    const auto owns_action = [observer, opening, id, kind] {
        return observer && observer->observationsAllowed() && observer->m_opening == opening &&
            observer->m_psbt_action_id == id && observer->m_psbt_action_kind == kind;
    };
    const auto owns_pending = [owns_action, observer, view, document, review, had_review] {
        if (!owns_action() || observer->m_psbt_action_state != PsbtActionState::PsbtPending || observer->m_psbt_view_epoch != view) return false;
        if (document) return observer->m_current_transaction && observer->m_current_psbt &&
            !document.owner_before(observer->m_current_psbt) && !observer->m_current_psbt.owner_before(document);
        return had_review ? review && observer->m_current_transaction == review : !observer->m_current_transaction;
    };
    const auto rejected = [owns_action, owns_pending, observer](const QString& error) {
        if (!owns_action() || observer->m_psbt_action_state != PsbtActionState::PsbtPending) return;
        const bool same = owns_pending();
        observer->m_psbt_action_state = same ? PsbtActionState::PsbtFailed : PsbtActionState::PsbtStale;
        observer->m_psbt_action_error = same ? error : QString{};
        Q_EMIT observer->psbtActionChanged();
    };
    auto receipt = std::make_shared<PsbtReceipt>(); m_psbt_receipt = receipt;
    if (missing_review || unavailable_broadcast) {
        const auto error = missing_review ? tr("No transaction is available for this action.")
            : !fee_known ? tr("The transaction fee is missing or invalid.") : tr("This transaction is not available for broadcast.");
        m_psbt_action_state = PsbtActionState::PsbtFailed; m_psbt_action_error = error;
        setTransactionStatus(error);
        if (owns_action() && m_psbt_view_epoch == view && m_psbt_action_state == PsbtActionState::PsbtFailed &&
            m_current_transaction == review) Q_EMIT observer->psbtActionChanged();
        return id;
    }
    constexpr size_t MAX_CAPTURE_BYTES{4 * 1024 * 1024};
    constexpr size_t FIXED_CAPTURE_BYTES{2048};
    if (static_cast<size_t>(path.size()) > (MAX_CAPTURE_BYTES - FIXED_CAPTURE_BYTES) / sizeof(QChar)) {
        rejected(tr("The PSBT action input is too large.")); return id;
    }
    const size_t capture_bytes{FIXED_CAPTURE_BYTES + static_cast<size_t>(path.size()) * sizeof(QChar)};
    auto weak = std::make_shared<std::weak_ptr<PsbtAction>>();
    auto operation = std::make_shared<PsbtAction>(scheduler, [weak, receipt, post, observer, id] {
        if (auto owner = weak->lock()) if (auto result = owner->result(); result && result->value) {
            std::lock_guard lock{receipt->mutex};
            receipt->path = result->value->effect.path; receipt->txid = result->value->effect.txid;
            receipt->saved = result->value->effect.saved; receipt->submitted = result->value->effect.submitted;
        }
        post([observer, id] { if (observer && observer->m_psbt_action_id == id) observer->publishPsbtAction(); });
    });
    if (!owns_pending()) { rejected({}); return id; }
    *weak = operation;
    auto owner = std::make_shared<std::shared_ptr<PsbtAction>>(operation);
    auto hold = [&]() -> execution::BorrowAdmission {
        if (opening) return opening->holdValues([owner] { owner->reset(); });
        if (!scheduler) return {{}, execution::Rejection::Closed};
        const auto key = scheduler->allocateKey();
        const auto cleanup = execution::Submit(*scheduler, [owner] { owner->reset(); }, [] {}, {}, {execution::Lane::Cleanup, 256, key}, true);
        if (!cleanup) return {{}, cleanup.rejection};
        auto held = scheduler->borrow(key); scheduler->activateCleanup(cleanup.id); return held;
    }();
    if (!owns_pending()) { rejected({}); return id; }
    if (!scheduler || !hold) { rejected(tr("PSBT result capacity is unavailable.")); return id; }
    std::shared_ptr<CTransactionRef> transaction_owner;
    if (kind == PsbtActionKind::Broadcast) {
        const auto factory = [] { return std::make_unique<CTransactionRef>(); };
        transaction_owner = opening ? walletopening::RetainValues<CTransactionRef>(opening, factory)
            : execution::RetainValues<CTransactionRef>(scheduler, factory);
        if (!owns_pending()) { rejected({}); return id; }
        if (!transaction_owner) { rejected(tr("PSBT transaction outcome capacity is unavailable.")); return id; }
    }
    const auto output_factory = [] { return std::make_unique<PsbtActionValue>(); };
    auto output = opening ? walletopening::RetainValues<PsbtActionValue>(opening, output_factory)
        : execution::RetainValues<PsbtActionValue>(scheduler, output_factory);
    if (!owns_pending()) { rejected({}); return id; }
    if (!output) { rejected(tr("PSBT outcome capacity is unavailable.")); return id; }
    m_psbt_action = operation; m_psbt_action_lease.emplace(std::move(*hold.lease));
    auto effect = [kind, path, document, proof, reviewed, fee, fill, unit, context, transaction_owner](interfaces::Wallet* wallet) {
        PsbtActionValue value;
        if (kind == PsbtActionKind::Import) {
            CMutableTransaction empty; PartiallySignedTransaction imported{empty, 2};
            value.error = psbtwork::LoadFile(path, imported);
            if (value.error.isEmpty()) {
                if (wallet) value.imported = psbtwork::PrepareReview(*wallet, std::make_shared<const PartiallySignedTransaction>(std::move(imported)), context, unit);
                else value.error = QStringLiteral("No wallet is loaded.");
            }
        } else if (kind == PsbtActionKind::Save) {
            if (!wallet) { value.error = QStringLiteral("No wallet is loaded."); return value; }
            const auto exported = psbtwork::ExportReview(*wallet, document, reviewed, fill, proof, fee);
            value.error = exported.error;
            if (value.error.isEmpty()) {
                psbtwork::Request request; request.kind = psbtwork::Kind::Save; request.path = path; request.document = exported.document;
                value.effect = psbtwork::Execute(wallet, std::move(request), context);
            }
        } else {
            psbtwork::Request request; request.kind = psbtwork::Kind::Broadcast;
            request.document = document; request.proof = proof; request.approved_fee = fee;
            value.effect = psbtwork::Execute(wallet, std::move(request), context);
            if (value.effect.transaction) {
                *transaction_owner = value.effect.transaction;
                value.effect.transaction = CTransactionRef{transaction_owner, transaction_owner->get()};
            }
        }
        return value;
    };
    auto work = [effect, output](interfaces::Wallet* wallet) {
        *output = effect(wallet);
        auto value = *output;
        value.lifetime = output;
        // The production review constructor consumes an already-retained
        // snapshot. Keep its last owner on the original Native output lease,
        // rather than exposing PrepareReview's ordinary shared_ptr deleter.
        if (value.imported.review) value.imported.review = std::shared_ptr<const walletreview::Snapshot>{output, value.imported.review.get()};
        if (value.imported.document) value.imported.document = std::shared_ptr<const PartiallySignedTransaction>{output, value.imported.document.get()};
        return value;
    };
    execution::Admission admission;
    if (opening) {
        const auto action = kind == PsbtActionKind::Import ? nullptr : opening->startAction();
        if (!owns_pending()) { rejected({}); return id; }
        if (kind == PsbtActionKind::Import) admission = operation->startWith([opening, work, capture_bytes](auto done, auto failed) {
            return opening->read([work](interfaces::Wallet& wallet) { return work(&wallet); }, std::move(done), std::move(failed), execution::WorkClass::Observation, capture_bytes);
        });
        else if (action) admission = operation->startWith([opening, action, work, capture_bytes](auto done, auto failed) {
            return opening->perform(action, [work](interfaces::Wallet& wallet) { return work(&wallet); }, std::move(done), std::move(failed), capture_bytes);
        });
    } else admission = operation->start([work] { return work(nullptr); }, {execution::Lane::Core, capture_bytes});
    // Effect admission can close/delete/replace the observer. The original
    // command owns its actual outcome independently; never adopt a successor.
    if (!owns_action() || observer->m_psbt_action != operation || observer->m_psbt_action_state != PsbtActionState::PsbtPending) return id;
    if (!admission) rejected(tr("The PSBT action could not be queued."));
    else if (owns_pending()) Q_EMIT observer->psbtActionChanged();
    return id;
}

void WalletQmlModel::publishPsbtAction()
{
    if (!observationsAllowed()) return;
    if (!m_psbt_action || !psbtActionBusy()) return;
    const auto result = m_psbt_action->result(); if (!result) return;
    if (m_admission_closed) return;
    const QPointer<WalletQmlModel> observer{this};
    const auto original_action = m_psbt_action;
    const auto original_id = m_psbt_action_id;
    const auto original_kind = m_psbt_action_kind;
    const auto original_view = m_psbt_action_view_epoch;
    const auto original_preparation = m_preparation_id, original_send = m_send_request_id;
    const auto owns_action = [&] {
        return observer && observationsAllowed() && m_psbt_action == original_action && m_psbt_action_id == original_id;
    };
    QPointer<WalletQmlModelTransaction> projected_review{m_current_transaction};
    bool had_projected_review{bool(projected_review)};
    bool document_view{original_kind != PsbtActionKind::Import && m_psbt_action_had_document};
    bool original_review_matches{true};
    const auto same_document = [&] {
        return m_current_psbt && !m_psbt_action_document.owner_before(m_current_psbt) &&
            !m_current_psbt.owner_before(m_psbt_action_document);
    };
    const auto owns_view = [&] {
        return owns_action() && original_review_matches && m_psbt_view_epoch == original_view &&
            (document_view ? m_current_transaction && same_document()
                : had_projected_review ? projected_review && m_current_transaction == projected_review.data() : !m_current_transaction);
    };
    const bool same_review = m_psbt_action_had_document
        ? m_current_transaction && same_document()
        : m_psbt_action_had_review ? m_psbt_action_review && m_psbt_action_review == m_current_transaction : !m_current_transaction;
    original_review_matches = original_kind == PsbtActionKind::Import || same_review;
    // These commands own a frozen reviewed document, not the mutable send
    // draft. Import can replace its prior view after internal fee refinement;
    // Save/Broadcast can only project onto their original review and view.
    const bool same = original_view == m_psbt_view_epoch &&
        (original_kind == PsbtActionKind::Import || same_review);
    if (result->error || !result->value) {
        bool not_started{false};
        if (result->error) {
            try { std::rethrow_exception(result->error); }
            catch (const execution::NotStarted&) { not_started = true; }
            catch (...) {}
        }
        m_psbt_action_state = not_started ? PsbtActionState::PsbtFailed : PsbtActionState::PsbtUncertain;
        m_psbt_action_error = not_started ? tr("The PSBT action did not start.") : tr("The PSBT action ended without a confirmed outcome. Reconcile before retrying.");
        if (!not_started && m_psbt_action_kind == PsbtActionKind::Broadcast && m_psbt_action_review == m_current_transaction) m_current_transaction_can_broadcast = false;
    } else {
        const auto& value = *result->value;
        m_psbt_action_error = !value.error.isEmpty() ? value.error : value.effect.error;
        if (value.effect.submitted) {
            m_psbt_action_state = PsbtActionState::PsbtSubmitted;
            if (same) {
                m_current_transaction_can_send = false; m_current_transaction_can_broadcast = false;
                if (value.effect.transaction) { m_current_transaction->setWtx(value.effect.transaction); if (!owns_view()) return; }
                m_current_psbt.reset(); m_psbt_document_lease.reset();
                document_view = false; projected_review = m_current_transaction;
                had_projected_review = bool(projected_review);
                m_current_transaction_source = CurrentTransactionSource::None;
                m_current_transaction_review_message.clear();
                clearSelectedCoins(); if (!owns_view()) return;
            }
        } else if (value.effect.saved) m_psbt_action_state = PsbtActionState::PsbtSaved;
        else if (value.effect.code == psbtwork::Error::Uncertain) {
            m_psbt_action_state = PsbtActionState::PsbtUncertain;
            if (same && m_psbt_action_kind == PsbtActionKind::Broadcast) m_current_transaction_can_broadcast = false;
        } else if (!same) m_psbt_action_state = PsbtActionState::PsbtStale;
        else if (!m_psbt_action_error.isEmpty()) m_psbt_action_state = PsbtActionState::PsbtFailed;
        else if (m_psbt_action_kind == PsbtActionKind::Import) {
            const auto& imported = value.imported;
            if (imported.disposition == psbtwork::ImportResult::Disposition::Known) {
                m_psbt_import_result = PsbtImportResult::TransactionAlreadyKnown;
                m_psbt_action_state = PsbtActionState::PsbtKnown;
                m_imported_psbt_model->setMatchedTxid(imported.txid);
                if (!observer) return;
            } else if (imported.review) {
                WalletQmlModelTransaction* transaction{nullptr};
                try { transaction = new WalletQmlModelTransaction(imported.review, m_opening, m_fee_read_services.post, this); }
                catch (const std::exception&) {
                    if (!owns_view()) return;
                    m_psbt_action_state = PsbtActionState::PsbtFailed;
                    m_psbt_action_error = tr("Transaction review capacity is unavailable.");
                    Q_EMIT psbtActionChanged(); return;
                }
                if (!owns_view()) { if (observer) transaction->deleteLater(); return; }
                auto* previous = m_current_transaction; m_current_transaction = transaction;
                projected_review = transaction; had_projected_review = true;
                if (previous) previous->deleteLater();
                m_current_psbt.reset(); m_psbt_document_lease.reset();
                m_current_psbt = imported.document;
                if (m_psbt_action_lease) { m_psbt_document_lease.emplace(std::move(*m_psbt_action_lease)); m_psbt_action_lease.reset(); }
                m_psbt_fee_proof = imported.review->proof
                    ? std::shared_ptr<const psbtfee::Analysis>{imported.review, imported.review->proof.get()}
                    : std::shared_ptr<const psbtfee::Analysis>{};
                m_psbt_fee_request.reset(); m_psbt_fee_pending = false;
                m_psbt_key_can_send = imported.key_can_send; m_psbt_can_commit_complete = imported.can_commit_complete;
                m_current_transaction_source = CurrentTransactionSource::ImportedPsbt;
                m_current_transaction_can_send = imported.review->can_send;
                m_current_transaction_can_broadcast = imported.key_can_broadcast && imported.review->proof->amount.has_value();
                m_current_transaction_review_message = imported.message;
                m_psbt_import_result = imported.review->can_send ? PsbtImportResult::WalletCanSign : PsbtImportResult::WalletCannotSign;
                m_psbt_action_state = PsbtActionState::PsbtImported;
                const QPointer<WalletQmlModelTransaction> imported_review{transaction};
                const auto owns_import = [&] {
                    return observer && observationsAllowed() && m_psbt_action == original_action &&
                        m_psbt_action_id == original_id && m_psbt_view_epoch == original_view &&
                        m_psbt_action_view_epoch == original_view &&
                        imported_review && m_current_transaction == imported_review;
                };
                adoptCurrentFlow(imported.review->flow);
                if (!owns_import()) return;
                if (m_preparation_id == original_preparation && m_send_request_id == original_send) {
                    clearTransactionStatus();
                    if (!owns_import()) return;
                }
            } else { m_psbt_action_state = PsbtActionState::PsbtFailed; m_psbt_action_error = imported.message; }
        } else m_psbt_action_state = PsbtActionState::PsbtFailed;
    }
    if (!owns_action()) return;
    const auto owns_status = [&] {
        return owns_action() && m_psbt_view_epoch == original_view && m_preparation_id == original_preparation && m_send_request_id == original_send;
    };
    if (!m_psbt_action_error.isEmpty() && same && owns_status()) {
        setTransactionStatus(m_psbt_action_error); if (!owns_view()) return;
        if (owns_status() && original_kind == PsbtActionKind::Import && m_imported_psbt_model) { m_imported_psbt_model->setError(m_psbt_action_error); if (!owns_view()) return; }
    }
    if (!owns_view()) return;
    const auto notified_review = QPointer<WalletQmlModelTransaction>{m_current_transaction};
    const bool had_notified_review = bool(notified_review);
    Q_EMIT currentTransactionChanged();
    if (!owns_action() || m_psbt_view_epoch != original_view ||
        (had_notified_review ? !notified_review || m_current_transaction != notified_review : bool(m_current_transaction))) return;
    Q_EMIT psbtActionChanged();
}

QVariantMap WalletQmlModel::currentTransactionFlow() const { return m_current_flow; }

QVariantMap WalletQmlModel::currentTransactionFlowPreview() const
{
    return {{QStringLiteral("presentation"), m_current_flow.value(QStringLiteral("presentation"))}};
}

void WalletQmlModel::publishFlowDetails()
{
    if (!observationsAllowed()) return;
    if (!m_flow_details) return;
    if (!available() || !m_current_transaction || m_current_flow.isEmpty()) { m_flow_details->clearSource(); return; }
    auto anchor = std::make_shared<std::pair<std::shared_ptr<const void>, std::shared_ptr<const void>>>(
        m_flow_lifetime, m_current_transaction->retainedValues());
    m_flow_details->publishSource(m_current_flow, m_current_transaction->reviewedRecipients(),
        m_current_transaction, m_flow_revision, std::move(anchor), m_flow_unit);
}

void WalletQmlModel::adoptCurrentFlow(QVariantMap flow)
{
    m_flow_review = m_current_transaction;
    m_flow_transaction = m_current_transaction ? m_current_transaction->getWtx() : CTransactionRef{};
    m_flow_document = m_current_psbt;
    m_flow_proof = m_psbt_fee_proof;
    m_flow_unit = m_display_unit;
    m_current_flow = std::move(flow);
    ++m_flow_revision;
    m_flow_lifetime = m_current_transaction ? m_current_transaction->retainedValues() : nullptr;
    m_flow_pending = false;
    m_flow_error.clear();
    { std::lock_guard lock{m_flow_input->mutex}; ++m_flow_input->revision; }
    const QPointer<WalletQmlModel> observer{this};
    publishFlowDetails();
    if (!observer || !available()) return;
    Q_EMIT currentTransactionFlowChanged();
}

void WalletQmlModel::requestCurrentFlow()
{
    if (!available() || !m_opening) return;
    if (!m_current_transaction || !m_current_transaction->getWtx()) { adoptCurrentFlow({}); return; }
    const auto transaction = m_current_transaction->getWtx();
    if (m_flow_review == m_current_transaction && m_flow_transaction == transaction &&
        m_flow_document == m_current_psbt && m_flow_proof == m_psbt_fee_proof && m_flow_unit == m_display_unit) return;
    m_flow_review = m_current_transaction;
    m_flow_transaction = transaction;
    m_flow_document = m_current_psbt;
    m_flow_proof = m_psbt_fee_proof;
    m_flow_unit = m_display_unit;
    {
        std::lock_guard lock{m_flow_input->mutex};
        m_flow_input->transaction = transaction;
        m_flow_input->document = m_current_psbt;
        m_flow_input->proof = m_psbt_fee_proof;
        m_flow_input->recipients = m_current_transaction->reviewedAddresses();
        m_flow_input->notes = m_current_transaction->recipientLabels();
        m_flow_input->unit = m_display_unit;
        ++m_flow_input->revision;
    }
    if (!m_flow_query) {
        const QPointer<WalletQmlModel> observer{this};
        m_flow_query = std::make_shared<execution::SnapshotQuery<ReviewFlowResult>>(m_opening->scheduler(),
            [opening = m_opening, input = m_flow_input] {
                CTransactionRef transaction;
                std::shared_ptr<const PartiallySignedTransaction> document;
                std::shared_ptr<const psbtfee::Analysis> proof;
                QSet<QString> recipients;
                QMap<QString, QString> notes;
                int unit; quint64 revision;
                {
                    std::lock_guard lock{input->mutex};
                    transaction = input->transaction; document = input->document; proof = input->proof;
                    recipients = input->recipients; notes = input->notes; unit = input->unit; revision = input->revision;
                }
                ReviewFlowResult result; result.revision = revision;
                try {
                    auto wallet = opening->borrow();
                    if (!wallet) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
                    result.flow = psbtwork::CollectReviewFlow(*wallet, transaction, document, proof, recipients, unit, notes);
                    auto retained = walletopening::RetainValues<QVariantMap>(opening, [&] {
                        return std::make_unique<QVariantMap>(result.flow);
                    });
                    if (!retained) throw std::runtime_error{"Transaction flow cleanup capacity is unavailable"};
                    result.lifetime = std::move(retained);
                } catch (...) {
                    result.flow.clear(); // Rejected ownership retires the full Native value here.
                    result.lifetime.reset();
                    result.error = tr("The transaction flow could not be refreshed.");
                }
                return result;
            }, [post = m_fee_read_services.post, observer] {
                post([observer] { if (observer) observer->publishCurrentFlow(); });
            });
    }
    m_flow_pending = true;
    m_flow_error.clear();
    const QPointer<WalletQmlModel> observer{this};
    if (m_flow_details) m_flow_details->clearSource();
    if (!observer || !available()) return;
    m_flow_query->request();
    Q_EMIT currentTransactionFlowChanged();
}

void WalletQmlModel::publishCurrentFlow()
{
    if (!observationsAllowed()) return;
    const auto result = m_flow_query->take();
    if (!available() || !result) return;
    if (result->snapshot) {
        std::lock_guard lock{m_flow_input->mutex};
        if (result->snapshot->revision != m_flow_input->revision) return;
    }
    m_flow_pending = false;
    m_current_flow = result->snapshot ? result->snapshot->flow : QVariantMap{};
    ++m_flow_revision;
    m_flow_lifetime = result->snapshot ? result->snapshot->lifetime : nullptr;
    m_flow_error = result->snapshot ? result->snapshot->error : tr("The transaction flow could not be refreshed.");
    const QPointer<WalletQmlModel> observer{this};
    publishFlowDetails();
    if (!observer || !available()) return;
    Q_EMIT currentTransactionFlowChanged();
}

void WalletQmlModel::discardCurrentTransaction()
{
    if (!observationsAllowed()) return;
    const QPointer<WalletQmlModel> observer{this};
    const auto view_epoch = ++m_psbt_view_epoch;
    if (m_current_transaction && m_send_review == m_current_transaction && m_send_consent) m_send_consent->withdraw();
    invalidatePreparation();
    const auto revision = m_preparation_revision, preparation_id = m_preparation_id, send_id = m_send_request_id;
    m_psbt_fee_request.reset();
    m_psbt_fee_proof.reset();
    m_psbt_fee_pending = false;
    const bool had_transaction_state{
        m_current_transaction ||
        m_current_psbt ||
        m_current_transaction_can_send ||
        m_current_transaction_can_broadcast ||
        !m_current_transaction_review_message.isEmpty()};

    auto* previous = std::exchange(m_current_transaction, nullptr);
    m_current_psbt.reset(); m_psbt_document_lease.reset();
    m_current_transaction_source = CurrentTransactionSource::None;
    m_current_transaction_can_send = false;
    m_current_transaction_can_broadcast = false;
    m_current_transaction_review_message.clear();
    // Publish a coherent empty review before destruction can invoke a direct
    // QObject handler. A reentrant successor owns its own form and status.
    const auto current = [&] {
        return observer && observationsAllowed() && !m_current_transaction && revision == m_preparation_revision
            && preparation_id == m_preparation_id && send_id == m_send_request_id && view_epoch == m_psbt_view_epoch;
    };
    delete previous;
    if (!observer || !observationsAllowed()) return;
    if (!current()) { if (had_transaction_state) Q_EMIT currentTransactionChanged(); return; }
    clearTransactionStatus();
    if (!observer || !observationsAllowed()) return;
    if (!current()) { if (had_transaction_state) Q_EMIT currentTransactionChanged(); return; }
    m_send_recipients->clear();
    if (!observer || !observationsAllowed()) return;

    if (had_transaction_state) {
        Q_EMIT currentTransactionChanged();
    }
}

void WalletQmlModel::selectCoin(const COutPoint& output)
{
    if (!m_admission_closed && m_coins_list_model) m_coins_list_model->requestSelectionDelta(output, true);
}

void WalletQmlModel::unselectCoin(const COutPoint& output)
{
    if (!m_admission_closed && m_coins_list_model) m_coins_list_model->requestSelectionDelta(output, false);
}

bool WalletQmlModel::isSelectedCoin(const COutPoint& output)
{
    const auto selection = selectedCoins();
    return selection && selection->contains(output);
}

std::shared_ptr<const CoinsListModel::Selection> WalletQmlModel::selectedCoins() const
{
    return m_coins_list_model ? m_coins_list_model->selectionSnapshot() : nullptr;
}

bool WalletQmlModel::coinSelectionPending() const
{
    return m_coins_list_model && m_coins_list_model->selectionPending();
}

void WalletQmlModel::clearSelectedCoins()
{
    if (m_admission_closed) return;
    const QPointer<WalletQmlModel> observer{this};
    const bool had_maximum = m_maximum_recipient != nullptr;
    setMaximumRecipient(nullptr);
    if (observer && !m_admission_closed && m_coins_list_model) {
        const auto selection = selectedCoins();
        // Pending commands can change an apparently empty projection. An
        // explicit clear must still follow them; a stable empty fixed draft
        // needs neither a new selection command nor another facts read.
        if (!had_maximum && !coinSelectionPending() && selection && selection->empty()) return;
        m_coins_list_model->requestReplaceSelection({});
    }
}

unsigned int WalletQmlModel::feeTargetBlocks() const
{
    return m_coin_control.m_confirm_target.value_or(DEFAULT_STANDARD_FEE_TARGET);
}

void WalletQmlModel::setFeeTargetBlocks(unsigned int target_blocks)
{
    if (m_coin_control.m_confirm_target != target_blocks) {
        const QPointer<WalletQmlModel> observer{this};
        m_coin_control.m_confirm_target = target_blocks;
        if (m_custom_target_pending && m_pending_custom_target != target_blocks) {
            m_custom_target_pending = false;
            Q_EMIT customFeeTargetPendingChanged();
            if (!observer || m_stopping) return;
        }
        updateMaximumAmount();
        if (!observer || m_stopping) return;
        Q_EMIT feeTargetBlocksChanged();
        if (!observer || m_stopping) return;
        Q_EMIT estimatedFeeChanged();
        if (!observer || m_stopping) return;
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }
}

void WalletQmlModel::setCustomFeeEnabled(const bool enabled)
{
    if (m_custom_fee_enabled != enabled) {
        m_custom_fee_enabled = enabled;
        if (!enabled && std::exchange(m_custom_target_pending, false)) Q_EMIT customFeeTargetPendingChanged();
        Q_EMIT customFeeEnabledChanged();
        Q_EMIT estimatedFeeChanged();
        Q_EMIT sendAmountExhaustsBalanceChanged();
        scheduleFeeEstimates();
    }
}

void WalletQmlModel::setCustomFeeRate(const QString& fee_rate)
{
    const bool was_pending = std::exchange(m_custom_target_pending, false);
    const QPointer<WalletQmlModel> observer{this};
    if (was_pending) Q_EMIT customFeeTargetPendingChanged();
    if (observer) setCustomFeeRateInternal(fee_rate, true);
}

void WalletQmlModel::setCustomFeeRateInternal(const QString& fee_rate, bool infer_target)
{
    const QString trimmed_fee_rate = fee_rate.size() > 32 ? fee_rate.left(33) : fee_rate.trimmed();
    const bool was_valid = customFeeRateValid();

    if (m_custom_fee_rate == trimmed_fee_rate) {
        return;
    }

    m_custom_fee_rate = trimmed_fee_rate;
    m_custom_fee_estimate.reset();

    if (infer_target && m_wallet) {
        if (const auto requested_rate = ParseCustomFeeRatePerKvB(m_custom_fee_rate, m_minimum_relay_fee.value_or(DEFAULT_MIN_RELAY_TX_FEE))) {
            std::array<CAmount, CUSTOM_FEE_TARGETS.size()> target_rates{};
            for (size_t i = 0; i < CUSTOM_FEE_TARGETS.size(); ++i) {
                target_rates[i] = m_custom_target_rates.value(CUSTOM_FEE_TARGETS[i], 0);
            }

            // When the estimator has no target-specific data, moving the thumb
            // would imply a confirmation estimate we do not actually have.
            if (std::any_of(target_rates.begin() + 1, target_rates.end(), [&](CAmount rate) {
                    return rate != target_rates.front();
                })) {
                unsigned int inferred_target = CUSTOM_FEE_TARGETS.back();
                for (size_t i = 0; i < CUSTOM_FEE_TARGETS.size(); ++i) {
                    if (target_rates[i] > 0 && *requested_rate >= target_rates[i]) {
                        inferred_target = CUSTOM_FEE_TARGETS[i];
                        break;
                    }
                }
                setFeeTargetBlocks(inferred_target);
            }
        }
    }

    Q_EMIT customFeeRateChanged();
    if (was_valid != customFeeRateValid()) {
        Q_EMIT customFeeRateValidChanged();
    }
    Q_EMIT estimatedFeeChanged();
    Q_EMIT sendAmountExhaustsBalanceChanged();
    scheduleFeeEstimates();
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
        Q_EMIT walletContentsChanged();
    }
}



void WalletQmlModel::refreshSecurityState()
{
    if (m_opening) m_opening->refreshMetadata();
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
    if (!observationsAllowed()) return;
    const bool error_changed = m_transaction_error != error;
    const bool unlock_changed = m_transaction_needs_unlock != needs_unlock;
    if (!error_changed && !unlock_changed) return;
    const auto revision = ++m_transaction_status_revision;
    m_transaction_error = error; m_transaction_needs_unlock = needs_unlock;
    const QPointer<WalletQmlModel> observer{this};
    if (error_changed) Q_EMIT transactionErrorChanged();
    if (!observer || !observationsAllowed() || revision != m_transaction_status_revision) return;
    if (unlock_changed) Q_EMIT transactionNeedsUnlockChanged();
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
