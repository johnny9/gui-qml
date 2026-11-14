// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/walletpublication.h>

#include <utility>

namespace walletpublication {
bool NativeObservation::capture(std::weak_ptr<interfaces::Wallet> wallet, std::shared_ptr<std::atomic<uint64_t>> unloaded, std::shared_ptr<const void> observation, uint64_t ingress)
{
    std::lock_guard lock{m_mutex};
    if (m_entries.size() == 64) { m_failed = true; return false; }
    m_entries.push_back({std::move(wallet), std::move(unloaded), std::move(observation), ingress}); return true;
}
walletopening::NativeEvents NativeObservation::resolve(const QString& id, const std::function<QString(interfaces::Wallet&)>& read_name) const
{
    std::vector<Entry> entries;
    { std::lock_guard lock{m_mutex}; entries = m_entries; }
    const Entry* first{nullptr};
    for (const auto& entry : entries) {
        if (const auto wallet = entry.wallet.lock()) {
            if (read_name(*wallet) == id && (!first || entry.ingress < first->ingress)) first = &entry;
        }
    }
    return first ? walletopening::NativeEvents{m_clock, first->unloaded, first->observation} : walletopening::NativeEvents{};
}
void Release::whenReleased(std::function<void()> callback)
{
    {
        std::lock_guard lock{m_mutex};
        if (!m_released) { m_notifications.push_back(std::move(callback)); return; }
    }
    callback();
}
void Release::release()
{
    std::list<std::function<void()>> callbacks;
    { std::lock_guard lock{m_mutex}; if (std::exchange(m_released, true)) return; callbacks.swap(m_notifications); }
    for (auto& callback : callbacks) callback(); // Enqueue GUI only; never wait.
}
} // namespace walletpublication
