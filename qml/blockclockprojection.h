// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BLOCKCLOCKPROJECTION_H
#define BITCOIN_QML_BLOCKCLOCKPROJECTION_H

#include <qml/snapshotquery.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <QList>
#include <QMetaType>

namespace blockclock {
struct FractionWaiter {
    std::atomic_bool notified{false};
    const std::function<void()> wake;
    explicit FractionWaiter(std::function<void()> callback) : wake(std::move(callback)) {}
    void notify() noexcept {
        if (notified.exchange(true)) return;
        try { wake(); } catch (...) {} // Observation handoff cannot escape Native retirement.
    }
};
struct ProjectionBudget {
    std::mutex mutex;
    size_t frames{0}, bytes{0};
    const size_t max_frames, max_bytes;
    std::array<std::weak_ptr<FractionWaiter>, 128> waiters;
    explicit ProjectionBudget(size_t frames_limit = 128, size_t bytes_limit = 32 * 1024 * 1024)
        : max_frames(frames_limit), max_bytes(bytes_limit) {}
};
struct FractionReservation {
    std::shared_ptr<ProjectionBudget> budget;
    size_t frames{0}, bytes{0};
    ~FractionReservation() {
        if (!frames) return;
        std::array<std::shared_ptr<FractionWaiter>, 128> wake;
        {
            std::lock_guard lock{budget->mutex}; budget->frames -= frames; budget->bytes -= bytes;
            for (size_t index = 0; index < budget->waiters.size(); ++index) {
                wake[index] = budget->waiters[index].lock();
                budget->waiters[index].reset();
            }
        }
        // The final strong waiter and its client captures also retire unlocked.
        for (const auto& waiter : wake) if (waiter) waiter->notify();
    }
};
enum class FractionWait { Available, Registered, Full };
inline FractionWait WaitForFractions(const std::shared_ptr<ProjectionBudget>& budget, size_t bytes,
                                    const std::shared_ptr<FractionWaiter>& waiter)
{
    std::lock_guard lock{budget->mutex};
    if (budget->frames < budget->max_frames && bytes <= budget->max_bytes - budget->bytes) return FractionWait::Available;
    for (auto& slot : budget->waiters) if (slot.expired()) { slot = waiter; return FractionWait::Registered; }
    return FractionWait::Full;
}
inline std::shared_ptr<ProjectionBudget> FRACTION_BUDGET = std::make_shared<ProjectionBudget>();
inline std::shared_ptr<FractionReservation> ReserveFractions(const std::shared_ptr<ProjectionBudget>& budget, size_t bytes, size_t frames = 1)
{
    auto hold = std::make_shared<FractionReservation>(); hold->budget = budget;
    std::lock_guard lock{budget->mutex};
    if (frames > budget->max_frames - budget->frames || bytes > budget->max_bytes - budget->bytes) return {};
    budget->frames += frames; budget->bytes += bytes; hold->frames = frames; hold->bytes = bytes;
    return hold;
}
struct Projection {
    // Destruction runs in reverse order: all array capacity is released before
    // either inventory reservation returns its count/bytes to admission.
    std::array<std::shared_ptr<FractionReservation>, 2> reservations;
    qint64 period{0};
    quint64 revision{0}, interest{0}, serial{0}, previous_serial{0}, history_generation{0};
    Projection() = default;
    Projection(const Projection&) = delete;
    Projection& operator=(const Projection&) = delete;
    QList<qreal> fractions;
    bool changed{false}, failed{false};
};
using Snapshot = std::shared_ptr<const Projection>;
using WeakSnapshot = std::weak_ptr<const Projection>;
struct Defaults { execution::SnapshotServices services; std::shared_ptr<std::atomic_bool> gate; };
inline std::mutex DEFAULT_MUTEX;
inline Defaults DEFAULTS{{}, std::make_shared<std::atomic_bool>(false)};
inline Defaults GetDefaults() { std::lock_guard lock{DEFAULT_MUTEX}; return DEFAULTS; }
inline void SetDefaults(execution::SnapshotServices services) {
    std::lock_guard lock{DEFAULT_MUTEX};
    if (services.scheduler && DEFAULTS.gate->load()) DEFAULTS.gate = std::make_shared<std::atomic_bool>(false);
    DEFAULTS.services = std::move(services);
}
} // namespace blockclock
Q_DECLARE_METATYPE(blockclock::WeakSnapshot)
#endif // BITCOIN_QML_BLOCKCLOCKPROJECTION_H
