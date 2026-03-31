
// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletqmlmodel.h>

#include <qml/bitcoinamount.h>
#include <qml/models/activitylistmodel.h>
#include <qml/models/paymentrequest.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletqmlmodeltransaction.h>

#include <consensus/amount.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <addresstype.h>
#include <outputtype.h>
#include <qml/bitcoinunits.h>
#include <serialize.h>
#include <streams.h>
#include <util/threadnames.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>

#include <QDateTime>
#include <QMetaObject>

#include <array>
#include <optional>

namespace {
constexpr unsigned int DEFAULT_STANDARD_FEE_TARGET{2};
constexpr int FEE_ESTIMATE_DEBOUNCE_MS{250};
constexpr std::array<unsigned int, 3> STANDARD_FEE_TARGETS{1, DEFAULT_STANDARD_FEE_TARGET, 6};

QString FailureReasonString(const wallet::ImportDescriptorResult::FailureReason reason)
{
    switch (reason) {
    case wallet::ImportDescriptorResult::FailureReason::NONE:
        return QStringLiteral("none");
    case wallet::ImportDescriptorResult::FailureReason::INVALID_DESCRIPTOR:
        return QStringLiteral("invalid_descriptor");
    case wallet::ImportDescriptorResult::FailureReason::INVALID_PARAMETER:
        return QStringLiteral("invalid_parameter");
    case wallet::ImportDescriptorResult::FailureReason::WALLET_ERROR:
        return QStringLiteral("wallet_error");
    case wallet::ImportDescriptorResult::FailureReason::MISC_ERROR:
        return QStringLiteral("misc_error");
    }
    return QStringLiteral("unknown");
}

QVariantMap ImportDescriptorError(const QString& error)
{
    return {
        {QStringLiteral("success"), false},
        {QStringLiteral("error"), error},
        {QStringLiteral("warnings"), QStringList{}},
        {QStringLiteral("usedDefaultRange"), false},
        {QStringLiteral("reason"), QStringLiteral("invalid_parameter")},
    };
}

QVariantMap ImportDescriptorResultMap(const wallet::ImportDescriptorResult& result)
{
    QStringList warnings;
    warnings.reserve(static_cast<qsizetype>(result.warnings.size()));
    for (const auto& warning : result.warnings) {
        warnings.append(QString::fromStdString(warning));
    }

    return {
        {QStringLiteral("success"), result.success},
        {QStringLiteral("error"), QString::fromStdString(result.error)},
        {QStringLiteral("warnings"), warnings},
        {QStringLiteral("usedDefaultRange"), result.used_default_range},
        {QStringLiteral("reason"), FailureReasonString(result.reason)},
    };
}

std::optional<interfaces::ImportDescriptorRequest> ParseImportDescriptorRequest(const QVariant& value, QString& error)
{
    const QVariantMap request_map = value.toMap();
    if (request_map.isEmpty() && !value.canConvert<QVariantMap>()) {
        error = QStringLiteral("Each descriptor import request must be an object.");
        return std::nullopt;
    }

    const QVariant descriptor_value = request_map.contains(QStringLiteral("descriptor"))
        ? request_map.value(QStringLiteral("descriptor"))
        : request_map.value(QStringLiteral("desc"));
    const QString descriptor = descriptor_value.toString().trimmed();
    if (descriptor.isEmpty()) {
        error = QStringLiteral("Each descriptor import request needs a non-empty 'descriptor' string.");
        return std::nullopt;
    }

    const QVariant timestamp_value = request_map.value(QStringLiteral("timestamp"));
    bool ok = false;
    const qlonglong timestamp = timestamp_value.toLongLong(&ok);
    if (!timestamp_value.isValid() || !ok) {
        error = QStringLiteral("Each descriptor import request needs an integer 'timestamp'.");
        return std::nullopt;
    }

    interfaces::ImportDescriptorRequest request;
    request.descriptor = descriptor.toStdString();
    request.timestamp = timestamp;

    const QVariant active_value = request_map.value(QStringLiteral("active"));
    if (active_value.isValid() && !active_value.isNull()) {
        request.active = active_value.toBool();
    }

    const QVariant internal_value = request_map.value(QStringLiteral("internal"));
    if (internal_value.isValid() && !internal_value.isNull()) {
        request.internal = internal_value.toBool();
    }

    const QVariant label_value = request_map.value(QStringLiteral("label"));
    if (label_value.isValid() && !label_value.isNull()) {
        request.label = label_value.toString().toStdString();
    }

    const QVariant range_value = request_map.value(QStringLiteral("range"));
    if (range_value.isValid() && !range_value.isNull()) {
        const QVariantList range_list = range_value.toList();
        if (range_list.size() != 2) {
            error = QStringLiteral("Descriptor import 'range' must be a two-element array.");
            return std::nullopt;
        }

        bool start_ok = false;
        bool end_ok = false;
        const qlonglong range_start = range_list.at(0).toLongLong(&start_ok);
        const qlonglong range_end = range_list.at(1).toLongLong(&end_ok);
        if (!start_ok || !end_ok) {
            error = QStringLiteral("Descriptor import 'range' values must be integers.");
            return std::nullopt;
        }
        request.range = std::make_pair<int64_t, int64_t>(range_start, range_end);
    }

    const QVariant next_index_value = request_map.contains(QStringLiteral("nextIndex"))
        ? request_map.value(QStringLiteral("nextIndex"))
        : request_map.value(QStringLiteral("next_index"));
    if (next_index_value.isValid() && !next_index_value.isNull()) {
        bool next_ok = false;
        const qlonglong next_index = next_index_value.toLongLong(&next_ok);
        if (!next_ok) {
            error = QStringLiteral("Descriptor import 'nextIndex' must be an integer.");
            return std::nullopt;
        }
        request.next_index = next_index;
    }

    return request;
}

QString FormatFeeEstimate(CAmount amount)
{
    BitcoinAmount bitcoin_amount;
    bitcoin_amount.setSatoshi(amount);
    return bitcoin_amount.toDisplay() + QStringLiteral(" ") + bitcoin_amount.unitLabel();
}

std::optional<std::vector<wallet::CRecipient>> BuildRecipients(const SendRecipientsListModel& recipients)
{
    std::vector<wallet::CRecipient> vec_send;
    vec_send.reserve(recipients.recipients().size());

    for (auto* recipient : recipients.recipients()) {
        if (recipient == nullptr || !recipient->isValid()) {
            return std::nullopt;
        }

        const CTxDestination destination = DecodeDestination(recipient->address()->address().toStdString());
        if (!IsValidDestination(destination)) {
            return std::nullopt;
        }

        vec_send.push_back({destination, recipient->cAmount(), recipient->subtractFeeFromAmount()});
    }

    if (vec_send.empty()) {
        return std::nullopt;
    }

    return vec_send;
}

struct QmlReceiveRequestRecipient
{
    static constexpr int CURRENT_VERSION{1};
    int nVersion{CURRENT_VERSION};
    std::string address;
    std::string label;
    CAmount amount{0};
    std::string message;
    std::string sPaymentRequest;
    std::string authenticatedMerchant;

    SERIALIZE_METHODS(QmlReceiveRequestRecipient, obj)
    {
        READWRITE(obj.nVersion, obj.address, obj.label, obj.amount, obj.message, obj.sPaymentRequest, obj.authenticatedMerchant);
    }
};

struct QmlRecentRequestEntry
{
    static constexpr int CURRENT_VERSION{1};
    int nVersion{CURRENT_VERSION};
    int64_t id{0};
    QDateTime date;
    QmlReceiveRequestRecipient recipient;

    SERIALIZE_METHODS(QmlRecentRequestEntry, obj)
    {
        unsigned int date_timet;
        SER_WRITE(obj, date_timet = obj.date.toSecsSinceEpoch());
        READWRITE(obj.nVersion, obj.id, date_timet, obj.recipient);
        SER_READ(obj, obj.date = QDateTime::fromSecsSinceEpoch(date_timet));
    }
};
} // namespace

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, QObject *parent)
    : QObject(parent)
{
    m_wallet = std::move(wallet);
    m_activity_list_model = new ActivityListModel(this);
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    m_current_payment_request = new PaymentRequest(this);
    initializeFeeEstimator();
}

WalletQmlModel::WalletQmlModel(QObject* parent)
    : QObject(parent)
{
    m_activity_list_model = new ActivityListModel(this);
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    m_current_payment_request = new PaymentRequest(this);
    initializeFeeEstimator();
}

WalletQmlModel::~WalletQmlModel()
{
    if (m_fee_estimation_timer) {
        m_fee_estimation_timer->stop();
    }
    if (m_fee_estimation_thread) {
        m_fee_estimation_thread->quit();
        m_fee_estimation_thread->wait();
    }
    delete m_fee_estimation_worker;
    delete m_activity_list_model;
    delete m_coins_list_model;
    delete m_send_recipients;
    delete m_current_payment_request;
    if (m_current_transaction) {
        delete m_current_transaction;
    }
}

void WalletQmlModel::initializeFeeEstimator()
{
    m_fee_estimation_worker = new QObject;
    m_fee_estimation_thread = new QThread(this);
    m_fee_estimation_worker->moveToThread(m_fee_estimation_thread);
    m_fee_estimation_thread->start();
    QTimer::singleShot(0, m_fee_estimation_worker, []() {
        util::ThreadRename("qml-fee-est");
    });

    m_fee_estimation_timer = new QTimer(this);
    m_fee_estimation_timer->setSingleShot(true);
    m_fee_estimation_timer->setInterval(FEE_ESTIMATE_DEBOUNCE_MS);
    connect(m_fee_estimation_timer, &QTimer::timeout, this, &WalletQmlModel::requestFeeEstimatesNow);
}

QString WalletQmlModel::balance() const
{
    if (!m_wallet) {
        return "0";
    }
    return QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, m_wallet->getBalance());
}

CAmount WalletQmlModel::balanceSatoshi() const
{
    if (!m_wallet) {
        return 0;
    }
    return m_wallet->getBalance();
}

QString WalletQmlModel::estimatedFee() const
{
    return estimatedFeeForTarget(feeTargetBlocks());
}

QString WalletQmlModel::estimatedFeeForTarget(const unsigned int target_blocks) const
{
    const QString estimate = m_fee_estimates.value(target_blocks);
    if (!estimate.isEmpty()) {
        return estimate;
    }

    return m_fee_estimate_pending ? QStringLiteral("…") : QStringLiteral("—");
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
    if (!m_wallet) {
        return QString();
    }
    return QString::fromStdString(m_wallet->getWalletName());
}

QString WalletQmlModel::newAddress(QString label)
{
    if (!m_wallet) {
        return QString();
    }
    OutputType output_type = m_wallet->getDefaultAddressType();
    util::Result<CTxDestination> dest{m_wallet->getNewDestination(output_type, label.toStdString())};
    return QString::fromStdString(EncodeDestination(dest.value()));
}

QVariantList WalletQmlModel::importDescriptors(const QVariantList& requests)
{
    QVariantList response;
    if (!m_wallet) {
        return response;
    }

    std::vector<interfaces::ImportDescriptorRequest> parsed_requests;
    parsed_requests.reserve(static_cast<size_t>(requests.size()));
    std::vector<int> request_indexes;
    request_indexes.reserve(static_cast<size_t>(requests.size()));
    response.reserve(requests.size());

    for (int i = 0; i < requests.size(); ++i) {
        QString error;
        std::optional<interfaces::ImportDescriptorRequest> request = ParseImportDescriptorRequest(requests.at(i), error);
        if (!request) {
            response.append(ImportDescriptorError(error));
            continue;
        }

        request_indexes.push_back(i);
        parsed_requests.push_back(std::move(*request));
        response.append(QVariantMap{});
    }

    if (parsed_requests.empty()) {
        return response;
    }

    const std::vector<wallet::ImportDescriptorResult> results = m_wallet->importDescriptors(parsed_requests);
    for (size_t i = 0; i < results.size() && i < request_indexes.size(); ++i) {
        response[request_indexes[i]] = ImportDescriptorResultMap(results[i]);
    }

    for (size_t i = results.size(); i < request_indexes.size(); ++i) {
        response[request_indexes[i]] = ImportDescriptorError(QStringLiteral("Descriptor import returned no result."));
    }

    return response;
}

void WalletQmlModel::commitPaymentRequest()
{
    if (!m_wallet || !m_current_payment_request) {
        return;
    }

    if (m_current_payment_request->id().isEmpty()) {
        m_current_payment_request->setId(nextPaymentRequestId());
    }

    if (m_current_payment_request->address().isEmpty()) {
        const OutputType output_type = m_wallet->getDefaultAddressType();
        const auto destination{m_wallet->getNewDestination(output_type, m_current_payment_request->label().toStdString())};
        if (!destination) {
            return;
        }
        m_current_payment_request->setDestination(destination.value());
    }

    bool parse_ok{false};
    const int64_t request_id{m_current_payment_request->id().toLongLong(&parse_ok)};
    if (!parse_ok || request_id <= 0) {
        return;
    }

    QmlRecentRequestEntry request_entry;
    request_entry.id = request_id;
    request_entry.date = QDateTime::currentDateTime();
    request_entry.recipient.address = m_current_payment_request->address().toStdString();
    request_entry.recipient.label = m_current_payment_request->label().toStdString();
    request_entry.recipient.amount = m_current_payment_request->amount()->satoshi();
    request_entry.recipient.message = m_current_payment_request->message().toStdString();

    DataStream ss{};
    ss << request_entry;

    m_wallet->setAddressReceiveRequest(
        m_current_payment_request->destination(),
        m_current_payment_request->id().toStdString(),
        ss.str());
}

unsigned int WalletQmlModel::nextPaymentRequestId() const
{
    if (!m_wallet) {
        return 1;
    }

    int64_t max_id{0};
    for (const std::string& request : m_wallet->getAddressReceiveRequests()) {
        std::vector<uint8_t> data(request.begin(), request.end());
        DataStream ss{data};
        QmlRecentRequestEntry entry;
        try {
            ss >> entry;
        } catch (const std::ios_base::failure&) {
            continue;
        }
        if (entry.id > max_id) {
            max_id = entry.id;
        }
    }

    if (max_id <= 0 || max_id >= std::numeric_limits<unsigned int>::max() - 1) {
        return 1;
    }

    return static_cast<unsigned int>(max_id + 1);
}

std::set<interfaces::WalletTx> WalletQmlModel::getWalletTxs() const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->getWalletTxs();
}

interfaces::WalletTx WalletQmlModel::getWalletTx(const uint256& hash) const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->getWalletTx(Txid::FromUint256(hash));
}

bool WalletQmlModel::tryGetTxStatus(const uint256& txid,
                                    interfaces::WalletTxStatus& tx_status,
                                    int& num_blocks,
                                    int64_t& block_time) const
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->tryGetTxStatus(Txid::FromUint256(txid), tx_status, num_blocks, block_time);
}

QString WalletQmlModel::getAddressLabel(const QString& address) const
{
    if (!m_wallet || address.isEmpty()) {
        return {};
    }

    const CTxDestination destination = DecodeDestination(address.toStdString());
    if (!IsValidDestination(destination)) {
        return {};
    }

    std::string label;
    if (!m_wallet->getAddress(destination, &label, nullptr, nullptr)) {
        return {};
    }

    return QString::fromStdString(label);
}

std::unique_ptr<interfaces::Handler> WalletQmlModel::handleTransactionChanged(TransactionChangedFn fn)
{
    if (!m_wallet) {
        return nullptr;
    }
    return m_wallet->handleTransactionChanged(fn);
}

void WalletQmlModel::scheduleFeeEstimates()
{
    if (m_fee_estimation_timer == nullptr) {
        return;
    }

    if (!m_wallet || !m_send_recipients) {
        clearFeeEstimates();
        return;
    }

    m_fee_estimation_timer->start();
}

QString WalletQmlModel::ensurePreviewChangeAddress()
{
    if (!m_wallet || !m_preview_change_address.isEmpty()) {
        return m_preview_change_address;
    }

    const auto destination = m_wallet->getNewDestination(m_wallet->getDefaultAddressType(), "qml-fee-preview");
    if (!destination) {
        return {};
    }

    m_preview_change_address = QString::fromStdString(EncodeDestination(destination.value()));
    return m_preview_change_address;
}

void WalletQmlModel::requestFeeEstimatesNow()
{
    if (!m_wallet || !m_send_recipients) {
        clearFeeEstimates();
        return;
    }

    const auto recipients = BuildRecipients(*m_send_recipients);
    if (!recipients.has_value()) {
        clearFeeEstimates();
        return;
    }

    const quint64 request_id = ++m_fee_estimate_request_id;
    const QString preview_change_address = ensurePreviewChangeAddress();
    const wallet::CCoinControl base_coin_control{m_coin_control};
    interfaces::Wallet* const wallet = m_wallet.get();

    if (!m_fee_estimate_pending) {
        m_fee_estimate_pending = true;
        Q_EMIT feeEstimatePendingChanged();
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }

    QTimer::singleShot(0, m_fee_estimation_worker, [this, request_id, recipients = *recipients, base_coin_control, preview_change_address, wallet]() {
        QHash<unsigned int, QString> estimates;

        for (const unsigned int target : STANDARD_FEE_TARGETS) {
            wallet::CCoinControl coin_control{base_coin_control};
            coin_control.m_feerate.reset();
            coin_control.m_confirm_target = target;

            if (!preview_change_address.isEmpty()) {
                const CTxDestination change_destination = DecodeDestination(preview_change_address.toStdString());
                if (IsValidDestination(change_destination)) {
                    coin_control.destChange = change_destination;
                }
            }

            int change_position{-1};
            CAmount fee{0};
            const auto result = wallet->createTransaction(recipients, coin_control, /*sign=*/false, change_position, fee);
            if (result) {
                estimates.insert(target, FormatFeeEstimate(fee));
            }
        }

        QMetaObject::invokeMethod(this, [this, estimates, request_id]() {
            applyFeeEstimates(estimates, request_id);
        }, Qt::QueuedConnection);
    });
}

void WalletQmlModel::applyFeeEstimates(const QHash<unsigned int, QString>& estimates, const quint64 request_id)
{
    if (request_id != m_fee_estimate_request_id) {
        return;
    }

    bool estimates_changed{m_fee_estimates != estimates};
    if (estimates_changed) {
        m_fee_estimates = estimates;
        Q_EMIT estimatedFeeChanged();
    }

    bool pending_changed{m_fee_estimate_pending};
    if (pending_changed) {
        m_fee_estimate_pending = false;
        Q_EMIT feeEstimatePendingChanged();
    }

    if (estimates_changed || pending_changed) {
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }
}

void WalletQmlModel::clearFeeEstimates()
{
    ++m_fee_estimate_request_id;

    bool estimates_changed{!m_fee_estimates.isEmpty()};
    if (estimates_changed) {
        m_fee_estimates.clear();
        Q_EMIT estimatedFeeChanged();
    }

    bool pending_changed{m_fee_estimate_pending};
    if (pending_changed) {
        m_fee_estimate_pending = false;
        Q_EMIT feeEstimatePendingChanged();
    }

    if (estimates_changed || pending_changed) {
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }
}

bool WalletQmlModel::prepareTransaction()
{
    if (!m_wallet || !m_send_recipients || m_send_recipients->recipients().empty()) {
        return false;
    }

    const auto vec_send = BuildRecipients(*m_send_recipients);
    if (!vec_send.has_value()) {
        return false;
    }

    CAmount total = 0;
    for (const auto& recipient : *vec_send) {
        total += recipient.nAmount;
    }

    m_coin_control.m_feerate.reset();
    if (!m_coin_control.m_confirm_target.has_value()) {
        m_coin_control.m_confirm_target = DEFAULT_STANDARD_FEE_TARGET;
    }

    CAmount balance = m_wallet->getBalance();
    if (balance < total) {
        return false;
    }

    int nChangePosRet = -1;
    CAmount nFeeRequired = 0;
    const auto& res = m_wallet->createTransaction(*vec_send, m_coin_control, true, nChangePosRet, nFeeRequired);
    if (res) {
        if (m_current_transaction) {
            delete m_current_transaction;
        }
        CTransactionRef newTx = *res;
        m_current_transaction = new WalletQmlModelTransaction(m_send_recipients, this);
        m_current_transaction->setWtx(newTx);
        m_current_transaction->setTransactionFee(nFeeRequired);
        Q_EMIT currentTransactionChanged();
        return true;
    } else {
        return false;
    }
}

void WalletQmlModel::sendTransaction()
{
    if (!m_wallet || !m_current_transaction) {
        return;
    }

    CTransactionRef newTx = m_current_transaction->getWtx();
    if (!newTx) {
        return;
    }

    interfaces::WalletValueMap value_map;
    interfaces::WalletOrderForm order_form;
    m_wallet->commitTransaction(newTx, value_map, order_form);
}

interfaces::Wallet::CoinsList WalletQmlModel::listCoins() const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->listCoins();
}

bool WalletQmlModel::lockCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->lockCoin(output, true);
}

bool WalletQmlModel::unlockCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->unlockCoin(output);
}

bool WalletQmlModel::isLockedCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->isLockedCoin(output);
}

void WalletQmlModel::listLockedCoins(std::vector<COutPoint>& outputs)
{
    if (!m_wallet) {
        return;
    }
    m_wallet->listLockedCoins(outputs);
}

void WalletQmlModel::selectCoin(const COutPoint& output)
{
    m_coin_control.Select(output);
    scheduleFeeEstimates();
}

void WalletQmlModel::unselectCoin(const COutPoint& output)
{
    m_coin_control.UnSelect(output);
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

unsigned int WalletQmlModel::feeTargetBlocks() const
{
    return m_coin_control.m_confirm_target.value_or(DEFAULT_STANDARD_FEE_TARGET);
}

void WalletQmlModel::setFeeTargetBlocks(unsigned int target_blocks)
{
    if (m_coin_control.m_confirm_target != target_blocks) {
        m_coin_control.m_confirm_target = target_blocks;
        Q_EMIT feeTargetBlocksChanged();
        Q_EMIT estimatedFeeChanged();
        scheduleFeeEstimates();
    }
}
