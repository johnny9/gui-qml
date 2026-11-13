// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/sendrecipient.h>
#include <qml/guipresentation.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/retainedvalue.h>
#include <key_io.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <QPointer>
#include <atomic>
#include <mutex>
#include <stdexcept>

namespace {
std::atomic<quint64> NEXT_RECIPIENT{0};
bool SameText(const QString& a, const QString& b) { return a.size() == b.size() && (a.isEmpty() || a.constData() == b.constData()); }
}
struct SendRecipient::Input {
    std::mutex mutex;
    Snapshot draft;
    bool initialized{false}, amount_set{false}, wallet{false}, balance_known{false}, editor_pending{false};
    QString editor_error;
    CAmount balance{0};
    CFeeRate dust{DUST_RELAY_TX_FEE};
};
struct SendRecipient::Retired {
    std::shared_ptr<Input> input;
    std::shared_ptr<execution::SnapshotQuery<std::shared_ptr<const Snapshot>>> query;
    std::shared_ptr<const Snapshot> validated;
    QString address_error, amount_error, label, message, request_label, request_address;
};
SendRecipient::SendRecipient(WalletQmlModel* wallet, QObject* parent)
    : QObject(parent), m_wallet(wallet), m_identity(++NEXT_RECIPIENT), m_input(std::make_shared<Input>()),
      m_address(new BitcoinAddress(this)), m_amount(new BitcoinAmount(this))
{
    connect(m_address, &BitcoinAddress::addressChanged, this, [this] {
        const QPointer<SendRecipient> guard{this};
        if (!m_request_address.isEmpty() && m_request_address != m_address->address()) {
            m_request_address.clear(); m_request_label.clear(); m_message.clear();
            Q_EMIT paymentRequestChanged(); if (!guard || admissionClosed()) return;
            Q_EMIT messageChanged(); if (!guard || admissionClosed()) return;
        }
        Q_EMIT addressChanged(); if (guard && !admissionClosed()) requestValidation();
    });
    connect(m_address, &BitcoinAddress::normalizationChanged, this, &SendRecipient::requestValidation);
    connect(m_amount, &BitcoinAmount::amountChanged, this, &SendRecipient::requestValidation);
    connect(m_amount, &BitcoinAmount::inputChanged, this, &SendRecipient::requestValidation);
    if (m_wallet) {
        connect(m_wallet, &WalletQmlModel::balanceChanged, this, &SendRecipient::requestValidation);
        connect(m_wallet, &WalletQmlModel::balanceObservationChanged, this, &SendRecipient::requestValidation);
        connect(m_wallet, &WalletQmlModel::sendFactsChanged, this, &SendRecipient::requestValidation);
    }
}
SendRecipient::~SendRecipient() { closeAdmission(); }
void SendRecipient::setOpening(std::shared_ptr<walletopening::Session> opening, execution::SnapshotServices services)
{
    if (admissionClosed() || (m_opening && m_opening != opening) || m_query) return;
    m_opening = std::move(opening); m_services = std::move(services);
    const auto setup = ++m_setup_revision;
    if (!m_services.scheduler && m_opening) m_services.scheduler = m_opening->scheduler();
    const QPointer<SendRecipient> observer{this};
    if (!m_services.post) m_services.post = presentation::QueuedPost();
    const auto original_opening = m_opening;
    const auto ports = m_services;
    const auto input = m_input;
    const auto current = [observer, setup, original_opening, scheduler = ports.scheduler] {
        return observer && !observer->admissionClosed() && observer->m_setup_revision == setup &&
            observer->m_opening == original_opening && observer->m_services.scheduler == scheduler;
    };
    m_address->setServices(ports);
    if (!current() || !ports.scheduler) return;
    auto retired = execution::RetainValues<Retired>(ports.scheduler, [] { return std::make_unique<Retired>(); });
    if (!current()) return;
    if (!retired) { m_addressError = tr("Recipient cleanup is unavailable."); return; }
    m_retired = std::move(retired);
    m_query = std::make_shared<execution::SnapshotQuery<std::shared_ptr<const Snapshot>>>(ports.scheduler,
        [input, scheduler = ports.scheduler] {
            Snapshot draft; bool amount_set, wallet, balance_known, editor_pending; QString editor_error; CAmount balance; CFeeRate dust;
            { std::lock_guard lock{input->mutex}; draft = input->draft; amount_set = input->amount_set; wallet = input->wallet;
              balance_known = input->balance_known; balance = input->balance; dust = input->dust; editor_pending = input->editor_pending; editor_error = input->editor_error; }
            auto value = execution::RetainValues<Snapshot>(scheduler, [&] {
                auto result = std::make_unique<Snapshot>(std::move(draft));
                result->destination = DecodeDestination(result->address.toStdString());
                result->validation_pending = editor_pending;
                const bool decoded_valid = IsValidDestination(result->destination);
                result->address_valid = !editor_pending && editor_error.isEmpty() && decoded_valid;
                if (!result->address.isEmpty() && !decoded_valid) {
                    if (IsValidDestinationString(result->address.toStdString(), *CChainParams::Main())) result->address_error = tr("Address is valid for mainnet, not the current network");
                    else if (IsValidDestinationString(result->address.toStdString(), *CChainParams::TestNet())) result->address_error = tr("Address is valid for testnet, not the current network");
                    else result->address_error = tr("Invalid address format");
                }
                if (!editor_error.isEmpty()) result->address_error = editor_error;
                if (amount_set) {
                    if (result->amount <= 0) result->amount_error = tr("Amount must be greater than zero");
                    else if (result->amount > MAX_MONEY) result->amount_error = tr("Amount exceeds maximum limit of 21,000,000 BTC");
                    else if (result->address_valid && IsDust(CTxOut{result->amount, GetScriptForDestination(result->destination)}, dust)) result->amount_error = tr("Amount is too small to send.");
                    else if (wallet && balance_known && result->amount > balance) result->amount_error = tr("Amount exceeds available balance");
                }
                result->valid = !editor_pending && editor_error.isEmpty() && (!wallet || balance_known) && result->input_acceptable && result->address_valid && result->amount_error.isEmpty() && result->amount > 0;
                return result;
            });
            if (!value) throw std::runtime_error{"Recipient validation cleanup unavailable"};
            return std::shared_ptr<const Snapshot>{std::move(value)};
        }, [post = ports.post, observer, setup] { post([observer, setup] { if (observer && observer->m_setup_revision == setup) observer->publishValidation(); }); }, execution::Lane::Domain);
    { std::lock_guard lock{input->mutex}; input->initialized = false; }
    requestValidation();
}
void SendRecipient::closeAdmission()
{
    if (m_closed) return;
    m_closed = true; if (m_query) m_query->close(); m_address->closeAdmission(); m_amount->closeAdmission();
    if (auto retired = std::move(m_retired)) {
        retired->input = std::move(m_input); retired->query = std::move(m_query); retired->validated = std::move(m_validated);
        retired->address_error = std::move(m_addressError); retired->amount_error = std::move(m_amountError);
        retired->label = std::move(m_label); retired->message = std::move(m_message);
        retired->request_label = std::move(m_request_label); retired->request_address = std::move(m_request_address);
    }
}
void SendRecipient::retryPublications()
{
    if (admissionClosed()) return;
    const QPointer<SendRecipient> guard{this}; m_address->retryPublication(); if (!guard || admissionClosed()) return;
    publishValidation(); if (!guard || admissionClosed()) return;
    if (m_query && !m_pending && (!m_validated || m_validated->revision != m_revision)) {
        const QPointer<SendRecipient> retry_guard{this};
        const auto query = m_query;
        const auto revision = m_revision, setup = m_setup_revision;
        m_pending = true; query->request();
        if (!retry_guard || admissionClosed() || m_revision != revision || m_setup_revision != setup) return;
        Q_EMIT draftChanged(); if (retry_guard && !admissionClosed()) Q_EMIT isValidChanged();
    }
}
SendRecipient::Snapshot SendRecipient::draft() const
{
    if (!validationPending() && m_validated && m_validated->revision == m_revision) return *m_validated;
    return editorDraft();
}
SendRecipient::Snapshot SendRecipient::editorDraft() const
{
    Snapshot value; value.token = m_identity; value.revision = m_revision; value.address = m_address->address();
    value.label = m_label; value.message = m_message; value.request_label = m_request_label; value.payment_request = hasPaymentRequest();
    value.validation_pending = validationPending();
    value.amount = m_amount->satoshi(); value.input_acceptable = m_amount->inputAcceptable(); value.input_error = m_amount->inputError();
    return value;
}
void SendRecipient::requestValidation()
{
    if (admissionClosed() || (m_opening && !m_opening->available())) return;
    // Capture this editor intent before comparing it with the last request.
    // The published draft still belongs to the previous validated revision.
    auto value = editorDraft();
    value.valid = value.address_valid = false; value.destination = CNoDestination{};
    value.address_error.clear(); value.amount_error.clear();
    const bool amount_set = m_amount->isSet(), wallet = bool(m_wallet);
    const bool known = wallet && m_wallet->balanceKnown();
    const auto balance = wallet ? m_wallet->displayedBalanceSatoshi() : 0;
    const auto dust = wallet ? m_wallet->dustRelayFee() : CFeeRate{DUST_RELAY_TX_FEE};
    const bool editor_pending = m_address->normalizationPending();
    const auto editor_error = m_address->normalizationError();
    {
        std::lock_guard lock{m_input->mutex};
        const auto& old = m_input->draft;
        if (m_input->initialized && old.address == value.address && SameText(old.label, value.label) && SameText(old.message, value.message) &&
            SameText(old.request_label, value.request_label) && old.payment_request == value.payment_request && old.amount == value.amount &&
            old.input_acceptable == value.input_acceptable && old.input_error == value.input_error &&
            m_input->amount_set == amount_set && m_input->wallet == wallet && m_input->balance_known == known &&
            m_input->balance == balance && m_input->dust.GetFeePerK() == dust.GetFeePerK() && m_input->editor_pending == editor_pending && m_input->editor_error == editor_error) return;
        value.revision = ++m_revision; m_pending = true;
        m_input->draft = std::move(value); m_input->initialized = true;
        m_input->amount_set = amount_set; m_input->wallet = wallet; m_input->balance_known = known;
        m_input->balance = balance; m_input->dust = dust; m_input->editor_pending = editor_pending; m_input->editor_error = editor_error;
    }
    const QPointer<SendRecipient> guard{this};
    const auto query = m_query;
    const auto revision = m_revision, setup = m_setup_revision;
    if (query) query->request();
    if (!guard || admissionClosed() || m_revision != revision || m_setup_revision != setup) return;
    Q_EMIT draftChanged(); if (!guard || admissionClosed()) return;
    Q_EMIT amountErrorChanged(); if (guard && !admissionClosed()) Q_EMIT isValidChanged();
}
void SendRecipient::publishValidation()
{
    if (admissionClosed() || !m_query) return;
    const auto result = m_query->take(); if (!result) return;
    const auto value = result->snapshot ? *result->snapshot : std::shared_ptr<const Snapshot>{};
    if (value && value->revision != m_revision) return;
    if (!m_pending && value && m_validated && value->revision == m_validated->revision) return;
    m_pending = false; m_validated = value;
    m_addressError = value ? value->address_error : tr("Recipient validation could not be completed.");
    m_amountError = value ? value->amount_error : QString{};
    const QPointer<SendRecipient> guard{this};
    Q_EMIT addressErrorChanged(); if (!guard || admissionClosed()) return;
    Q_EMIT amountErrorChanged(); if (!guard || admissionClosed()) return;
    Q_EMIT draftChanged(); if (guard && !admissionClosed()) Q_EMIT isValidChanged();
}
BitcoinAddress* SendRecipient::address() const { return m_address; }
void SendRecipient::setAddress(const QString& address) { if (!admissionClosed()) m_address->setAddress(address, 0); }
QString SendRecipient::addressError() const { return m_address->normalizationError().isEmpty() ? m_addressError : m_address->normalizationError(); }
void SendRecipient::setAddressError(const QString& error) { if (!admissionClosed() && m_addressError != error) { m_addressError = error; Q_EMIT addressErrorChanged(); } }
QString SendRecipient::label() const { return m_label; }
void SendRecipient::setLabel(const QString& label) { if (!admissionClosed() && !SameText(m_label, label)) { m_label = label; const QPointer<SendRecipient> guard{this}; Q_EMIT labelChanged(); if (guard && !admissionClosed()) requestValidation(); } }
BitcoinAmount* SendRecipient::amount() const { return m_amount; }
void SendRecipient::setAmount(const QString& amount) { if (!admissionClosed()) m_amount->fromDisplay(amount); }
QString SendRecipient::amountError() const { return m_amount->inputAcceptable() ? m_amountError : m_amount->inputError(); }
void SendRecipient::setAmountError(const QString& error) { if (!admissionClosed() && m_amountError != error) { m_amountError = error; Q_EMIT amountErrorChanged(); } }
QString SendRecipient::message() const { return m_message; }
void SendRecipient::setMessage(const QString& message) { if (!admissionClosed() && !SameText(m_message, message)) { m_message = message; const QPointer<SendRecipient> guard{this}; Q_EMIT messageChanged(); if (guard && !admissionClosed()) requestValidation(); } }
CAmount SendRecipient::cAmount() const { return m_amount->satoshi(); }
void SendRecipient::applyPaymentRequest(const QString& address, const QString& pay_to, const QString& message)
{
    if (admissionClosed()) return;
    const QPointer<SendRecipient> guard{this}; setAddress(address); if (!guard || admissionClosed()) return;
    m_request_address = m_address->address(); m_request_label = pay_to; m_message = message;
    Q_EMIT messageChanged(); if (!guard || admissionClosed()) return;
    Q_EMIT paymentRequestChanged(); if (guard && !admissionClosed()) requestValidation();
}
void SendRecipient::applyParsedPaymentRequest(const QVariantMap& parsed)
{
    if (admissionClosed() || (m_opening && !m_opening->available()) || !parsed.value("success").toBool()) return;
    const QPointer<SendRecipient> guard{this};
    applyPaymentRequest(parsed.value("address").toString(), parsed.value("hasLabel").toBool() ? parsed.value("label").toString() : QString{}, parsed.value("hasMessage").toBool() ? parsed.value("uriMessage").toString() : QString{});
    if (!guard || admissionClosed()) return;
    if (parsed.value("hasAmount").toBool()) m_amount->setSatoshi(parsed.value("amountSats").toLongLong());
    if (!guard || admissionClosed()) return;
    const auto note = parsed.value("privateNote").toString();
    if (!note.isEmpty()) setLabel(note);
}
void SendRecipient::clear()
{
    if (admissionClosed()) return;
    m_request_address.clear(); m_request_label.clear(); m_label.clear(); m_message.clear();
    const QPointer<SendRecipient> guard{this};
    m_address->setAddress("", 0); if (!guard || admissionClosed()) return;
    m_amount->clear(); if (!guard || admissionClosed()) return;
    Q_EMIT paymentRequestChanged(); if (!guard || admissionClosed()) return;
    Q_EMIT labelChanged(); if (!guard || admissionClosed()) return;
    Q_EMIT messageChanged(); if (guard && !admissionClosed()) requestValidation();
}
void SendRecipient::validateAddress() { requestValidation(); }
void SendRecipient::validateAmount() { requestValidation(); }
bool SendRecipient::isValid() const { return !admissionClosed() && (!m_wallet || m_wallet->available()) && !validationPending() && m_validated && m_validated->revision == m_revision && m_validated->valid; }
