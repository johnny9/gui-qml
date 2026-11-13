// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/secretinput.h>
#include <qml/retainedvalue.h>
#include <support/cleanse.h>
#include <QByteArray>
#include <limits>

namespace secretinput {
bool Inventory::reserve(size_t bytes)
{
    if (bytes > m_max_bytes) return false;
    const auto inputs = m_inputs.fetch_add(1);
    if (inputs >= m_max_inputs) { m_inputs.fetch_sub(1); return false; }
    const auto owned = m_bytes.fetch_add(bytes);
    if (owned > m_max_bytes - bytes) { m_bytes.fetch_sub(bytes); m_inputs.fetch_sub(1); return false; }
    return true;
}
void Inventory::release(size_t bytes) { m_bytes.fetch_sub(bytes); m_inputs.fetch_sub(1); }
class Reservation final {
public:
    Reservation(std::shared_ptr<Inventory> inventory, size_t bytes) : m_inventory(std::move(inventory)), m_bytes(bytes) {
        if (!m_inventory->reserve(bytes)) throw Failure{Error::Unavailable};
    }
    ~Reservation() { m_inventory->release(m_bytes); }
private:
    std::shared_ptr<Inventory> m_inventory;
    size_t m_bytes;
};
Input::Input(QString text, size_t bytes, std::unique_ptr<Reservation> reservation)
    : m_reservation(std::move(reservation)), m_text(std::move(text)), m_bytes(bytes) {}
Input::~Input() = default;
CaptureResult Capture(const std::shared_ptr<execution::Scheduler>& scheduler, const QString& text, size_t transport_bytes, std::shared_ptr<Inventory> inventory)
{
    // Peak conservative accounting: retained UTF16 plus temporary UTF8 and
    // secure UTF8 allocation. No scan of the original literal is necessary.
    constexpr size_t BYTES_PER_CODE_UNIT{8};
    constexpr size_t OVERHEAD{512};
    const auto units = static_cast<size_t>(text.size());
    if (units > (std::numeric_limits<size_t>::max() - OVERHEAD) / BYTES_PER_CODE_UNIT)
        return {{}, Error::TransportBudget};
    const size_t bytes = OVERHEAD + units * BYTES_PER_CODE_UNIT;
    if (bytes > transport_bytes) return {{}, Error::TransportBudget, bytes};
    if (!scheduler) return {{}, Error::Unavailable, bytes};
    // Shared process inventory remains charged through actual Native teardown,
    // including ordinary-command refusal and a stalled Cleanup lane.
    std::shared_ptr<Input> retained;
    try {
        static const auto process_inventory = std::make_shared<Inventory>();
        if (!inventory) inventory = process_inventory;
        auto reservation = std::make_unique<Reservation>(inventory, bytes);
        retained = execution::RetainValues<Input>(scheduler, [&text, bytes, reservation = std::move(reservation)]() mutable {
            return std::make_unique<Input>(text, bytes, std::move(reservation));
        });
    } catch (...) { return {{}, Error::Unavailable, bytes}; }
    const auto error = retained ? Error::None : Error::Unavailable;
    return {std::move(retained), error, bytes};
}
size_t Bytes(const Captured& input) { return input ? input->bytes() : 0; }
SecureString Materialize(const Captured& input)
{
    if (!input) throw Failure{Error::Unavailable};
    return input->materialize();
}
SecureString Input::materialize() const
{
    // Native-only boundary. Manual executors intentionally control this same
    // action in tests without relying on a physical GUI/thread identity check.
    QByteArray bytes;
    try {
        bytes = m_text.toUtf8();
        SecureString secret;
        try { secret.assign(bytes.constData(), bytes.constData() + bytes.size()); }
        catch (...) { if (!bytes.isEmpty()) memory_cleanse(bytes.data(), static_cast<size_t>(bytes.size())); throw; }
        if (!bytes.isEmpty()) memory_cleanse(bytes.data(), static_cast<size_t>(bytes.size()));
        return secret;
    } catch (...) {
        throw Failure{Error::Materialization};
    }
}
}
