// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletrequests.h>

#include <key_io.h>
#include <outputtype.h>
#include <wallet/types.h>
#include <QScopeGuard>
#include <QSettings>

namespace walletrequests {
namespace {
QString SettingsKey(interfaces::Wallet& wallet)
{
    return QStringLiteral("receiveAddressTypes/%1/").arg(QString::fromStdString(wallet.getWalletName()));
}
std::optional<OutputType> DestinationType(const CTxDestination& destination)
{
    if (std::holds_alternative<PKHash>(destination)) return OutputType::LEGACY;
    if (std::holds_alternative<ScriptHash>(destination)) return OutputType::P2SH_SEGWIT;
    if (std::holds_alternative<WitnessV0KeyHash>(destination) || std::holds_alternative<WitnessV0ScriptHash>(destination)) return OutputType::BECH32;
    if (std::holds_alternative<WitnessV1Taproot>(destination)) return OutputType::BECH32M;
    return {};
}
Error TransportError(std::exception_ptr error)
{
    try { std::rethrow_exception(error); } catch (const execution::NotStarted&) { return Error::Rejected; } catch (...) { return Error::Uncertain; }
}
}

ReservationStorage SettingsReservationStorage()
{
    return {
        [](interfaces::Wallet& wallet, const QString& candidate) {
            QSettings settings;
            settings.sync();
            const auto key = SettingsKey(wallet);
            const auto address = candidate.isEmpty() ? settings.value(key + QStringLiteral("address")).toString() : candidate;
            return StoredAddress{address, settings.value(key + QStringLiteral("retired/") + address, false).toBool(), settings.status() == QSettings::NoError};
        },
        [](interfaces::Wallet& wallet, const QString& address) {
            QSettings settings;
            settings.setValue(SettingsKey(wallet) + QStringLiteral("address"), address);
            settings.sync(); return settings.status() == QSettings::NoError;
        },
        [](interfaces::Wallet& wallet, const QString& address) {
            QSettings settings;
            settings.setValue(SettingsKey(wallet) + QStringLiteral("retired/") + address, true);
            settings.sync(); return settings.status() == QSettings::NoError;
        },
        [](interfaces::Wallet& wallet, const QString& address) {
            QSettings settings;
            const QString key = SettingsKey(wallet) + QStringLiteral("address");
            if (address.isEmpty() || settings.value(key).toString() == address) settings.remove(key);
            settings.sync(); return settings.status() == QSettings::NoError;
        },
    };
}

struct Store::AddressWork {
    Queued queued;
    std::shared_ptr<const AddressIntent> intent;
    std::optional<OutputType> requested_type;
    OutputType type{OutputType::BECH32};
    StoredAddress stored;
    CTxDestination candidate;
    bool owned{false}, used{false}, paid{false}, force_reuse{false}, fresh{false};
    std::string label;
    std::set<interfaces::WalletTx> history;
    std::set<interfaces::WalletTx>::const_iterator transaction;
    size_t output{0};
};

bool Store::currentAddressIntent(const std::shared_ptr<const AddressIntent>& intent)
{
    if (!intent || intent->opening != m_opening->identity()) return false;
    std::lock_guard lock{m_mutex};
    const auto found = m_latest_address_intent.find(intent->view);
    return found != m_latest_address_intent.end() && found->second == intent->token;
}

void Store::clearSecret(const std::shared_ptr<SecureString>& secret)
{
    if (secret) SecureString{}.swap(*secret);
}

void Store::reserve(Queued queued)
{
    const auto intent = queued.operation->address_intent;
    const auto reject = [&](Error error) { clearSecret(queued.command.secret); fail(queued.operation, error); };
    if (!intent || intent->opening != m_opening->identity()) { reject(Error::Changed); return; }
    if (!m_loaded) { reject(Error::Loading); return; }
    if (m_uncertain) { reject(Error::Uncertain); return; }
    const auto self = shared_from_this();
    if (queued.command.kind == Kind::ForgetAddress) {
        if (queued.command.clear_reservation) m_fresh_address_views.insert(intent->view);
        else m_fresh_address_views.erase(intent->view);
        m_address_questions.erase(intent->view); m_reservations.erase(intent->view); m_interests.erase(intent->view);
        updateWatch();
        if (!queued.command.clear_reservation) { finish(queued.operation, {}); return; }
        const auto admitted = m_opening->perform(queued.action, [self, intent](interfaces::Wallet& wallet) {
            return self->m_storage.clear && self->m_storage.clear(wallet, intent->draft.address);
        }, [self, operation = queued.operation](bool saved) { self->fail(operation, saved ? Error::None : Error::ReservationPersistence); },
        [self, operation = queued.operation](std::exception_ptr error) { self->fail(operation, TransportError(error)); });
        if (!admitted) fail(queued.operation, Error::Rejected);
        return;
    }
    if (!currentAddressIntent(intent)) { reject(Error::Changed); return; }
    // Selection opens the authoritative saved row when a preceding command
    // already created it. The GUI's address-list snapshot is only a hint.
    if (intent->select && m_current) {
        const auto found = m_current->by_address.find(intent->draft.address);
        if (found != m_current->by_address.end() && !found->second.empty()) {
            const auto* record = m_current->find(found->second.front());
            Result result; result.request_loaded = true; result.id = record->entry.id;
            result.record = std::make_shared<const Record>(*record);
            finish(queued.operation, std::move(result)); return;
        }
    }
    const bool resume = queued.command.kind == Kind::ResumeAddress;
    if (resume) {
        const auto question = m_address_questions.find(intent->view);
        if (question == m_address_questions.end() || question->second != intent) { reject(Error::Changed); return; }
    } else {
        m_address_questions.erase(intent->view);
    }
    if (intent->create && !MoneyRange(intent->draft.amount)) { reject(Error::Invalid); return; }
    auto work = std::make_shared<AddressWork>();
    work->queued = std::move(queued); work->intent = intent;
    work->fresh = !intent->select && m_fresh_address_views.contains(intent->view);
    if (!intent->draft.address_type.isEmpty()) {
        work->requested_type = ParseOutputType(intent->draft.address_type.toLower().toStdString());
        if (!work->requested_type) { addressReady(work, {}, Error::Invalid); return; }
    }
    QString candidate = intent->draft.address;
    if (const auto previous = m_reservations.find(intent->view); previous != m_reservations.end()) {
        if ((resume && previous->second.intent == intent && !previous->second.address.persisted) ||
            (!intent->next && candidate.isEmpty())) {
            candidate = previous->second.address.address;
            work->force_reuse = resume;
        }
    }
    // Capture Core/storage facts only. Reuse, type and payment decisions are
    // reduced on Domain in bounded record/output turns below.
    struct Capture {
        StoredAddress stored;
        CTxDestination candidate;
        OutputType type{OutputType::BECH32};
        bool owned{false};
        Error error{Error::None};
        std::string label;
        std::set<interfaces::WalletTx> history;
    };
    const auto admitted = m_opening->perform(work->queued.action, [self, work, candidate](interfaces::Wallet& wallet) {
        Capture capture;
        if (!self->m_opening->available()) { capture.error = Error::Closed; return capture; }
        if (!self->currentAddressIntent(work->intent)) { capture.error = Error::Changed; return capture; }
        capture.type = work->requested_type.value_or(wallet.getDefaultAddressType());
        if (!work->intent->create && !work->intent->select && (!wallet.canGetAddresses() ||
            (capture.type == OutputType::BECH32M && !wallet.taprootEnabled()))) { capture.error = Error::Unsupported; return capture; }
        if (work->intent->create) capture.stored.address = candidate;
        else if (self->m_storage.read) capture.stored = self->m_storage.read(wallet, candidate);
        else { capture.error = Error::Unsupported; return capture; }
        if (!capture.stored.ok) { capture.error = Error::ReservationPersistence; return capture; }
        capture.candidate = DecodeDestination(capture.stored.address.toStdString());
        if (((!work->intent->next && !work->fresh) || work->force_reuse) && IsValidDestination(capture.candidate)) {
            if (work->intent->create) capture.owned = true;
            else {
                wallet::AddressPurpose purpose{};
                capture.owned = wallet.getAddress(capture.candidate, &capture.label, &purpose) && purpose == wallet::AddressPurpose::RECEIVE;
                if (capture.owned) capture.history = wallet.getWalletTxs();
            }
        }
        return capture;
    }, [self, work](Capture capture) {
        if (capture.error != Error::None) { self->addressReady(work, {}, capture.error); return; }
        work->stored = std::move(capture.stored); work->candidate = std::move(capture.candidate);
        work->owned = capture.owned; work->type = capture.type;
        work->label = std::move(capture.label);
        work->history = std::move(capture.history); work->transaction = work->history.cbegin();
        work->paid = self->m_observed.contains(work->stored.address);
        work->used = self->m_consumed_addresses.contains(work->stored.address);
        const auto current = self->m_reservations.find(work->intent->view);
        const bool same_owner = current != self->m_reservations.end() && current->second.address.address == work->stored.address;
        if (work->stored.retired && !same_owner) work->used = true;
        for (const auto& [view, reservation] : self->m_reservations) {
            if (view != work->intent->view && reservation.address.address == work->stored.address) work->used = true;
        }
        // An explicit sibling request keeps its captured destination. It is
        // not an automatic single-use receiving reservation.
        if (work->intent->create) { work->used = false; work->paid = false; }
        self->scanAddress(work);
    }, [self, work](std::exception_ptr error) { self->addressReady(work, {}, TransportError(error)); });
    if (!admitted) addressReady(work, {}, Error::Rejected);
}

void Store::scanAddress(const std::shared_ptr<AddressWork>& work)
{
    size_t budget{256};
    while (budget-- && !work->paid && work->transaction != work->history.cend()) {
        const auto& tx = *work->transaction;
        if (!tx.tx || work->output == tx.tx->vout.size()) { ++work->transaction; work->output = 0; continue; }
        const size_t index = work->output++;
        const auto& output = tx.tx->vout[index];
        if (index >= tx.txout_is_mine.size() || !tx.txout_is_mine[index] || output.nValue <= 0) continue;
        CTxDestination destination;
        if (ExtractDestination(output.scriptPubKey, destination) && destination == work->candidate) work->paid = true;
    }
    if (!work->paid && work->transaction != work->history.cend()) {
        const auto self = shared_from_this();
        if (!m_opening->planAction(work->queued.action, [self, work] { self->scanAddress(work); })) addressReady(work, {}, Error::Rejected);
        return;
    }
    if (work->paid) noteNativePayment(work->stored.address);
    const auto actual_type = DestinationType(work->candidate);
    if (work->intent->select && actual_type) work->type = *actual_type;
    const bool same_type = !work->intent->explicit_type || work->intent->select || actual_type == work->type;
    const bool reuse = ((!work->intent->next && !work->fresh) || work->force_reuse) && work->owned && !work->used && !work->paid && same_type;
    if (work->intent->select && !reuse) { addressReady(work, {}, work->paid ? Error::Paid : work->used ? Error::AlreadySaved : Error::Invalid); return; }
    allocateAddress(work, reuse);
}

void Store::allocateAddress(const std::shared_ptr<AddressWork>& work, bool reuse)
{
    struct Generated { std::optional<CTxDestination> destination; Error error{Error::None}; };
    const auto self = shared_from_this();
    const auto secret = std::exchange(work->queued.command.secret, {});
    auto captured_secret = std::exchange(work->queued.command.captured_secret, {});
    const std::string label = work->intent->create ? work->intent->draft.note.toStdString() : std::string{};
    const auto admitted = m_opening->perform(work->queued.action, [self, work, secret, captured_secret = std::move(captured_secret), reuse, label](interfaces::Wallet& wallet) mutable {
        const auto wipe = qScopeGuard([secret] { clearSecret(secret); });
        Generated generated;
        if (!self->m_opening->available()) { generated.error = Error::Closed; return generated; }
        if (!self->currentAddressIntent(work->intent)) { generated.error = Error::Changed; return generated; }
        bool relock{false};
        try {
            const bool has_secret = secret || captured_secret;
            SecureString materialized;
            if (captured_secret) {
                materialized = secretinput::Materialize(captured_secret);
                captured_secret.reset();
            }
            if (has_secret && wallet.isCrypted() && wallet.isLocked()) {
                relock = true;
                const auto unlocked = wallet.unlock(secret ? *secret : materialized);
                if (!unlocked) {
                    relock = false;
                    generated.error = unlocked.error().code == wallet::WalletErrorCode::PassphraseIncorrect ? Error::IncorrectPassphrase : Error::AddressCreation;
                    return generated;
                }
            }
            SecureString{}.swap(materialized);
            clearSecret(secret);
            if (work->paid && self->m_storage.retire && !self->m_storage.retire(wallet, work->stored.address)) {
                generated.error = Error::ReservationPersistence;
            } else if (reuse) generated.destination = work->candidate;
            else {
                const auto destination = wallet.getNewDestination(work->type, label);
                if (destination) generated.destination = *destination;
                else generated.error = wallet.isCrypted() && wallet.isLocked() ? Error::NeedsUnlock : Error::AddressCreation;
            }
        } catch (const secretinput::Failure&) { generated.error = Error::Rejected; }
        catch (...) { generated.error = Error::Uncertain; }
        if (relock) {
            try { if (!wallet.lock()) generated.error = Error::Uncertain; } catch (...) { generated.error = Error::Uncertain; }
        }
        return generated;
    }, [self, work, reuse](Generated generated) {
        self->m_opening->refreshMetadata();
        if (!generated.destination || !IsValidDestination(*generated.destination)) { self->addressReady(work, {}, generated.error == Error::None ? Error::AddressCreation : generated.error); return; }
        ReservedAddress address{*generated.destination, QString::fromStdString(EncodeDestination(*generated.destination)),
            QString::fromStdString(FormatOutputType(work->type)),
            reuse ? QString::fromStdString(work->label) : QString{}, work->intent->create};
        if (generated.error != Error::None || work->intent->create) { self->addressReady(work, std::move(address), generated.error); return; }
        const auto admitted = self->m_opening->perform(work->queued.action, [self, address](interfaces::Wallet& wallet) {
            return self->m_storage.write && self->m_storage.write(wallet, address.address);
        }, [self, work, address](bool saved) mutable {
            address.persisted = saved;
            self->addressReady(work, std::move(address), saved ? Error::None : Error::ReservationPersistence);
        }, [self, work, address](std::exception_ptr error) { self->addressReady(work, address, TransportError(error)); });
        if (!admitted) self->addressReady(work, std::move(address), Error::Rejected);
    }, [self, work, secret](std::exception_ptr error) { clearSecret(secret); self->addressReady(work, {}, TransportError(error)); }, work->queued.bytes);
    if (!admitted) { clearSecret(secret); addressReady(work, {}, Error::Rejected); }
}

void Store::addressReady(const std::shared_ptr<AddressWork>& work, ReservedAddress address, Error error)
{
    clearSecret(work->queued.command.secret);
    const auto& intent = work->intent;
    const bool current = currentAddressIntent(intent) && !m_closed.load();
    const bool has_address = !address.address.isEmpty();
    if (error == Error::Uncertain) m_uncertain = true;
    if (current && (error == Error::NeedsUnlock || error == Error::IncorrectPassphrase || error == Error::ReservationPersistence)) {
        m_address_questions[intent->view] = intent;
    } else if (current) m_address_questions.erase(intent->view);
    if (has_address && current && !intent->create) {
        m_reservations[intent->view] = {intent, address};
        m_interests[intent->view] = address.address;
        if (error == Error::None && address.persisted) {
            m_fresh_address_views.erase(intent->view);
            if (m_persistence_error == Error::ReservationPersistence) m_persistence_error = Error::None;
        }
    }
    if (!current && error == Error::None) error = Error::Changed;
    if (intent->create && has_address && error == Error::None) {
        auto queued = std::move(work->queued);
        queued.command.kind = Kind::Create;
        queued.command.draft = intent->draft; queued.command.draft.address = address.address;
        queued.prepared_address = std::move(address);
        planMutation(std::move(queued)); return;
    }
    Result result; result.error = error;
    if (has_address) result.address = std::move(address);
    finish(work->queued.operation, std::move(result));
}
} // namespace walletrequests
