// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_SECRETINPUT_H
#define BITCOIN_QML_SECRETINPUT_H
#include <qml/execution.h>
#include <support/allocators/secure.h>
#include <QString>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <utility>

namespace secretinput {
enum class Error { None, Unavailable, TransportBudget, Materialization };
class Inventory final {
public:
    explicit Inventory(size_t inputs = 128, size_t bytes = 16 * 1024 * 1024) : m_max_inputs(inputs), m_max_bytes(bytes) {}
    size_t ownedInputs() const { return static_cast<size_t>(m_inputs.load()); }
    size_t ownedBytes() const { return static_cast<size_t>(m_bytes.load()); }
private:
    friend class Reservation;
    bool reserve(size_t bytes);
    void release(size_t bytes);
    const uint64_t m_max_inputs, m_max_bytes;
    std::atomic<uint64_t> m_inputs{0}, m_bytes{0};
};
class Reservation;
class Failure final : public std::exception {
public:
    explicit Failure(Error error) : m_error(error) {}
    Error error() const noexcept { return m_error; }
    const char* what() const noexcept override { return "The secret input could not be materialized"; }
private:
    Error m_error;
};
// Immutable Qt-native editor handoff. All UTF8 allocation/conversion, cleansing
// and retirement run inside the original Native action, never on GUI.
class Input final {
public:
    Input(QString text, size_t bytes, std::unique_ptr<Reservation> reservation);
    ~Input();
    size_t bytes() const { return m_bytes; }
    SecureString materialize() const;
private:
    // Declared first so inventory is returned after the Native QString teardown.
    std::unique_ptr<Reservation> m_reservation;
    const QString m_text;
    const size_t m_bytes;
};
using Captured = std::shared_ptr<const Input>;
struct CaptureResult {
    Captured input;
    Error error{Error::None};
    size_t bytes{0};
    explicit operator bool() const { return bool(input) && error == Error::None; }
};
// O(1) metadata admission, using the app transport budget rather than a password
// domain length restriction. Charge result.bytes in the original action too.
CaptureResult Capture(const std::shared_ptr<execution::Scheduler>& scheduler, const QString& text,
    size_t transport_bytes = execution::Limits::Bounded().ordinary.payload_bytes,
    std::shared_ptr<Inventory> inventory = {});
size_t Bytes(const Captured& input);
SecureString Materialize(const Captured& input);
}
#endif // BITCOIN_QML_SECRETINPUT_H
