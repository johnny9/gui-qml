// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletreceivemodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/walletunlockcontext.h>
#include <qml/wallet/walletpassphrase.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <outputtype.h>
#include <util/translation.h>
#include <QCryptographicHash>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <algorithm>
#include <limits>

WalletReceiveModel::WalletReceiveModel(WalletSession& session, const QString& network, QObject* parent)
    : QObject(parent), m_session{session}, m_draft{this}, m_history{this},
      m_settings_key{QStringLiteral("receiveAddressTypes/%1/%2").arg(network,
          QString::fromLatin1(QCryptographicHash::hash(session.identity().toUtf8(), QCryptographicHash::Sha256).toHex()))}
{
    connect(&m_draft, &PaymentRequest::changed, this, [this] { ++m_draft_revision; });
    connect(&session, &WalletSession::changed, this, &WalletReceiveModel::reload);
    connect(&session, &WalletSession::addressesChanged, this, &WalletReceiveModel::reload);
    connect(&session, &WalletSession::actionBusyChanged, this, &WalletReceiveModel::changed);
    connect(&session, &WalletSession::invalidated, this, [this] {
        m_loading = false;
        m_error = tr("The wallet is unavailable.");
        Q_EMIT changed();
    });
    QTimer::singleShot(0, this, &WalletReceiveModel::reload);
}

bool WalletReceiveModel::available() const { return m_session.available() && !m_address_types.empty(); }
bool WalletReceiveModel::busy() const { return m_loading || m_session.actionBusy(); }

void WalletReceiveModel::setDefaultAddressType(const QString& type)
{
    if (!m_address_types.contains(type) || type == m_default_type) return;
    m_default_type = type;
    QSettings{}.setValue(m_settings_key, type);
    Q_EMIT changed();
}

void WalletReceiveModel::reload()
{
    if (!m_session.available()) return;
    if (m_loading) { m_reload_pending = true; return; }
    m_loading = true;
    Q_EMIT changed();
    struct Snapshot { std::vector<QmlRecentRequestEntry> records; QStringList types; QString default_type; };
    auto snapshot{std::make_shared<Snapshot>()};
    const QPointer<WalletReceiveModel> self{this};
    if (!m_session.runRead([snapshot](interfaces::Wallet& wallet) {
        snapshot->records = ReceiveRequestHistoryModel::DeserializeEntries(wallet.getAddressReceiveRequests());
        for (const auto type : wallet.getAvailableAddressTypes()) snapshot->types.push_back(QString::fromStdString(FormatOutputType(type)));
        snapshot->default_type = QString::fromStdString(FormatOutputType(wallet.getDefaultAddressType()));
        return WalletOperationResult{};
    }, [self, snapshot](WalletOperationResult result) {
        if (!self) return;
        self->m_loading = false;
        self->m_read_error = result.error;
        if (result.code == WalletOperationResult::Success) {
            self->m_history.setEntries(std::move(snapshot->records));
            self->m_last_id = std::max(self->m_last_id, self->m_history.maxId());
            self->m_address_types = snapshot->types;
            const QString saved{QSettings{}.value(self->m_settings_key,
                QSettings{}.value(QStringLiteral("receiveAddressTypes/%1").arg(self->m_session.name()))).toString()};
            self->m_default_type = snapshot->types.contains(saved) ? saved :
                snapshot->types.contains(snapshot->default_type) ? snapshot->default_type : snapshot->types.value(0);
        }
        Q_EMIT self->changed();
        if (self->m_reload_pending) { self->m_reload_pending = false; self->reload(); }
    })) { m_loading = false; Q_EMIT changed(); }
}

bool WalletReceiveModel::save(const QString& passphrase)
{
    m_error.clear();
    m_needs_unlock = false;
    const auto draft{m_draft.validatedEntry()};
    const QString type_name{m_draft.addressType().isEmpty() ? m_default_type : m_draft.addressType()};
    const auto type{ParseOutputType(type_name.toStdString())};
    if (!draft || !m_session.available() || (draft->recipient.address.empty() && (!type || !m_address_types.contains(type_name)))) {
        m_error = tr("Enter a valid amount and an available address type.");
        Q_EMIT changed();
        return false;
    }
    auto entry{std::make_shared<QmlRecentRequestEntry>(*draft)};
    auto needs_unlock{std::make_shared<bool>(false)};
    const quint64 revision{m_draft_revision};
    const QPointer<WalletReceiveModel> self{this};
    const bool accepted{m_session.runAction([entry, id_floor = m_last_id, type = type.value_or(OutputType::BECH32), secret = WalletPassphrase(passphrase), needs_unlock](interfaces::Wallet& wallet) {
        const auto stored{ReceiveRequestHistoryModel::DeserializeEntries(wallet.getAddressReceiveRequests())};
        if (entry->id) {
            const auto found{std::ranges::find_if(stored, [&](const auto& item) {
                return item.id == entry->id && item.recipient.address == entry->recipient.address;
            })};
            if (found == stored.end()) return WalletOperationResult::failure(WalletOperationResult::Unavailable, QObject::tr("The request no longer exists."));
        } else {
            int64_t maximum{id_floor};
            for (const auto& item : stored) maximum = std::max(maximum, item.id);
            if (maximum == std::numeric_limits<int64_t>::max()) return WalletOperationResult::failure(WalletOperationResult::CoreError, QObject::tr("No request identifiers remain."));
            entry->id = maximum + 1;
            entry->date = QDateTime::currentDateTimeUtc();
        }
        std::unique_ptr<WalletUnlockContext> unlock;
        if (entry->recipient.address.empty()) {
            if (!secret.empty()) {
                unlock = std::make_unique<WalletUnlockContext>(wallet, secret);
                if (!unlock->valid()) return WalletOperationResult::failure(WalletOperationResult::CoreError, unlock->error());
            }
            const auto destination{wallet.getNewDestination(type, entry->recipient.label)};
            if (!destination) {
                *needs_unlock = wallet.isCrypted() && wallet.isLocked();
                return WalletOperationResult::failure(WalletOperationResult::CoreError, QString::fromStdString(util::ErrorString(destination).translated));
            }
            entry->recipient.address = EncodeDestination(*destination);
        }
        const auto destination{DecodeDestination(entry->recipient.address)};
        if (!IsValidDestination(destination)) return WalletOperationResult::failure(WalletOperationResult::InvalidInput, QObject::tr("The request address is invalid."));
        if (!wallet.setAddressReceiveRequest(destination, std::to_string(entry->id), ReceiveRequestHistoryModel::SerializeEntry(*entry))) {
            return WalletOperationResult::failure(WalletOperationResult::CoreError, QObject::tr("The request could not be saved."));
        }
        return WalletOperationResult{};
    }, [self, entry, needs_unlock, revision](WalletOperationResult result) {
        if (!self) return;
        self->m_error = result.error;
        self->m_needs_unlock = *needs_unlock;
        if (result.code == WalletOperationResult::Success) {
            self->m_last_id = std::max(self->m_last_id, entry->id);
            self->m_history.prependOrReplace(*entry);
            if (self->m_draft_revision == revision) self->m_draft.setEntry(*entry);
            Q_EMIT self->requestSaved(QString::number(entry->id));
        }
        Q_EMIT self->changed();
    })};
    if (!accepted) { m_error = tr("The wallet is busy or unavailable."); Q_EMIT changed(); }
    return accepted;
}

bool WalletReceiveModel::remove(const QString& id)
{
    const auto entry{m_history.entryById(id)};
    if (!entry || !m_session.available()) return false;
    const QPointer<WalletReceiveModel> self{this};
    return m_session.runAction([entry](interfaces::Wallet& wallet) {
        if (!wallet.setAddressReceiveRequest(DecodeDestination(entry->recipient.address), std::to_string(entry->id), {})) {
            return WalletOperationResult::failure(WalletOperationResult::CoreError, QObject::tr("The request could not be removed."));
        }
        return WalletOperationResult{};
    }, [self, id](WalletOperationResult result) {
        if (!self) return;
        self->m_error = result.error;
        if (result.code == WalletOperationResult::Success) {
            self->m_history.removeByRequestId(id);
            if (self->m_draft.id() == id) self->m_draft.clear();
        }
        Q_EMIT self->changed();
    });
}

bool WalletReceiveModel::edit(const QString& id)
{
    const auto entry{m_history.entryById(id)};
    if (!entry || busy()) return false;
    m_draft.setEntry(*entry);
    return true;
}

bool WalletReceiveModel::useAsTemplate(const QString& id, bool reuse_address)
{
    const auto entry{m_history.entryById(id)};
    if (!entry || busy()) return false;
    m_draft.useAsTemplate(*entry, reuse_address);
    return true;
}

void WalletReceiveModel::clear()
{
    if (busy()) return;
    m_draft.clear();
    m_error.clear();
    m_needs_unlock = false;
    Q_EMIT changed();
}
