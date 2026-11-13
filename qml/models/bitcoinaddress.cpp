// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bitcoinaddress.h>
#include <qml/guipresentation.h>
#include <QPointer>
#include <algorithm>
#include <limits>
#include <mutex>

namespace {
constexpr int MAX_BITCOIN_ADDRESS_LENGTH{90};
constexpr qsizetype EDITOR_BUDGET{512};
std::mutex DEFAULT_MUTEX;
execution::SnapshotServices DEFAULT_SERVICES;
std::shared_ptr<std::atomic_bool> DEFAULT_GATE;
struct DefaultsValue { execution::SnapshotServices services; std::shared_ptr<std::atomic_bool> gate; };
bool IsBitcoinAddressChar(QChar c) {
    const ushort ch = c.unicode();
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z' && ch != 'I' && ch != 'O');
}
DefaultsValue Defaults() { std::lock_guard lock{DEFAULT_MUTEX}; return {DEFAULT_SERVICES, DEFAULT_GATE}; }
}
BitcoinAddress::BitcoinAddress(QObject* parent) : QObject(parent) { auto defaults = Defaults(); m_gate = std::move(defaults.gate); setServices(std::move(defaults.services)); }
BitcoinAddress::BitcoinAddress(const QString& address, QObject* parent) : BitcoinAddress(parent) { setAddress(address, 0); }
BitcoinAddress::~BitcoinAddress() { closeAdmission(); }
void BitcoinAddress::setDefaultServices(execution::SnapshotServices services)
{
    std::lock_guard lock{DEFAULT_MUTEX};
    if (DEFAULT_GATE) DEFAULT_GATE->store(true);
    DEFAULT_GATE = services.scheduler ? std::make_shared<std::atomic_bool>(false) : nullptr;
    DEFAULT_SERVICES = std::move(services);
}
void BitcoinAddress::setServices(execution::SnapshotServices services)
{
    if (admissionClosed() || m_pending) return;
    if (!m_gate) m_gate = Defaults().gate;
    m_services = std::move(services);
    if (!m_services.post) m_services.post = presentation::QueuedPost();
}
void BitcoinAddress::closeAdmission() { m_closed = true; ++m_revision; m_pending = false; }
QString BitcoinAddress::address() const { return m_address; }
bool BitcoinAddress::isEmpty() const { return m_address.isEmpty(); }
QString BitcoinAddress::formattedAddress() const { return m_formattedAddress; }
QString BitcoinAddress::ellipsesAddress() const { return ellipsesAddress(m_address); }
BitcoinAddress::Normalized BitcoinAddress::NormalizeEditorInput(const QString& input, int cursor, qsizetype budget)
{
    Normalized value; value.address.reserve(MAX_BITCOIN_ADDRESS_LENGTH);
    const auto limit = std::min(input.size(), budget);
    int before{0}; qsizetype processed{0};
    for (; processed < limit && value.address.size() < MAX_BITCOIN_ADDRESS_LENGTH; ++processed) {
        if (!IsBitcoinAddressChar(input[processed])) continue;
        value.address += input[processed];
        if (processed < std::max(cursor, 0)) ++before;
    }
    value.complete = processed == input.size() || value.address.size() == MAX_BITCOIN_ADDRESS_LENGTH;
    value.formatted = formattedAddress(value.address);
    int seen{0};
    while (value.cursor < value.formatted.size() && seen < before) {
        if (value.formatted[value.cursor] != QLatin1Char{' '}) ++seen;
        ++value.cursor;
    }
    return value;
}
int BitcoinAddress::apply(Normalized value, bool force)
{
    const bool changed = value.address != m_address || value.formatted != m_formattedAddress;
    m_address = std::move(value.address); m_formattedAddress = std::move(value.formatted); m_cursor = value.cursor;
    m_pending = false; m_error.clear();
    const QPointer<BitcoinAddress> guard{this};
    if (changed) { Q_EMIT addressChanged(); if (!guard || admissionClosed()) return value.cursor; }
    if (changed || force) { Q_EMIT formattedAddressChanged(); if (!guard || admissionClosed()) return value.cursor; }
    if (changed) { Q_EMIT ellipsesAddressChanged(); if (!guard || admissionClosed()) return value.cursor; }
    Q_EMIT normalizationChanged();
    return value.cursor;
}
int BitcoinAddress::setAddress(const QString& input, int cursor)
{
    if (admissionClosed()) return -1;
    ++m_revision;
    auto value = NormalizeEditorInput(input, cursor, EDITOR_BUDGET);
    if (value.complete) { const bool force = input != value.formatted; return apply(std::move(value), force); }
    return enqueue(input, cursor);
}
int BitcoinAddress::pasteAddress(const QString& text, int start, int end)
{
    if (admissionClosed() || m_pending) return -1;
    start = std::clamp(start, 0, static_cast<int>(m_formattedAddress.size()));
    end = std::clamp(end, start, static_cast<int>(m_formattedAddress.size()));
    const auto prefix = m_formattedAddress.left(start), suffix = m_formattedAddress.mid(end);
    if (text.size() <= EDITOR_BUDGET) return setAddress(prefix + text + suffix, start + static_cast<int>(text.size()));
    ++m_revision;
    return enqueue(text, start + static_cast<int>(std::min<qsizetype>(text.size(), std::numeric_limits<int>::max() - start)), prefix, suffix);
}
int BitcoinAddress::enqueue(const QString& input, int cursor, const QString& prefix, const QString& suffix)
{
    if (admissionClosed()) return -1;
    if (!m_services.scheduler) {
        m_pending = false; m_error = tr("Address normalization services are unavailable."); Q_EMIT normalizationChanged(); return -1;
    }
    const auto revision = m_revision;
    const QPointer<BitcoinAddress> observer{this};
    auto operation = std::make_shared<execution::OwnedCommand<Normalized>>(m_services.scheduler,
        [post = m_services.post, observer] { post([observer] { if (observer) observer->retryPublication(); }); });
    const auto bytes = static_cast<size_t>(input.size()) * sizeof(QChar) + 2048;
    const auto admitted = operation->start([input, cursor, prefix, suffix] {
        const auto value = prefix.isEmpty() && suffix.isEmpty() ? input : prefix + input + suffix;
        return NormalizeEditorInput(value, cursor, std::numeric_limits<qsizetype>::max());
    }, {execution::Lane::Domain, bytes, {}, execution::WorkClass::Observation});
    // The accepted original operation owns its Domain result even when the
    // admission port closes, destroys or edits its presentation observer.
    if (!observer || observer->admissionClosed() || observer->m_revision != revision) return -1;
    if (!admitted) {
        m_pending = false; m_error = tr("Address normalization could not be queued. Try again."); Q_EMIT normalizationChanged(); return -1;
    }
    m_operation = std::move(operation); m_operation_revision = revision; m_pending = true; m_error.clear();
    Q_EMIT normalizationChanged(); return -1;
}
bool BitcoinAddress::cancelNormalization(quint64 id)
{
    if (!m_pending || id != m_revision) return false;
    ++m_revision; m_pending = false; m_error.clear(); m_operation.reset();
    if (!admissionClosed()) Q_EMIT normalizationChanged();
    return true;
}
void BitcoinAddress::retryPublication()
{
    if (admissionClosed() || !m_pending || m_operation_revision != m_revision || !m_operation) return;
    const auto result = m_operation->result(); if (!result) return;
    if (result->error || !result->value) { m_pending = false; m_error = tr("Address normalization could not be completed."); Q_EMIT normalizationChanged(); return; }
    apply(*result->value, true);
}
QString BitcoinAddress::formattedAddress(const QString& address)
{
    QString fmt; fmt.reserve(address.size() + address.size() / 4);
    for (qsizetype i = 0; i < address.size(); ++i) { if (i > 0 && i % 4 == 0) fmt += QLatin1Char{' '}; fmt += address[i]; }
    return fmt;
}
QString BitcoinAddress::ellipsesAddress(const QString& address)
{
    if (address.size() <= 8) return address;
    return address.left(4) + ' ' + address.mid(4, 4) + " ... " + address.mid(address.size() - 8, 4) + ' ' + address.right(4);
}
