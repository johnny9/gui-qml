// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletsession.h>

#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <QPointer>

WalletSession::WalletSession(std::shared_ptr<interfaces::Wallet> wallet, quint64 id, WalletOperationExecutor& executor, QObject* parent)
    : QObject(parent), m_wallet(std::move(wallet)), m_id(id), m_name(QString::fromStdString(m_wallet->getWalletName())), m_executor(executor)
{
    m_handlers.push_back(m_wallet->handleUnload([this] {
        QMetaObject::invokeMethod(this, &WalletSession::invalidate, Qt::QueuedConnection);
    }));
    m_handlers.push_back(m_wallet->handleStatusChanged([this] {
        QMetaObject::invokeMethod(this, [this] { if (m_available) Q_EMIT changed(); }, Qt::QueuedConnection);
    }));
    m_handlers.push_back(m_wallet->handleTransactionChanged([this](const Txid&, ChangeType) {
        QMetaObject::invokeMethod(this, [this] {
            if (!m_available) return;
            Q_EMIT activityChanged();
            Q_EMIT changed();
        }, Qt::QueuedConnection);
    }));
    m_handlers.push_back(m_wallet->handleAddressBookChanged([this](const CTxDestination&, const std::string&, bool, wallet::AddressPurpose, ChangeType) {
        QMetaObject::invokeMethod(this, [this] { if (m_available) Q_EMIT addressesChanged(); }, Qt::QueuedConnection);
    }));
    m_handlers.push_back(m_wallet->handleCanGetAddressesChanged([this] {
        QMetaObject::invokeMethod(this, [this] { if (m_available) Q_EMIT changed(); }, Qt::QueuedConnection);
    }));
    // A new block can change confirmations/maturity without changing a wallet
    // transaction. Observe the tip processed by this wallet, not the node tip
    // which may arrive before the wallet has processed its notification.
    m_tip_timer.setInterval(1000);
    connect(&m_tip_timer, &QTimer::timeout, this, [this] { pollProcessedTip(); });
    // Queue the baseline before any child model's initial read. Discovering the
    // starting tip is not a change that should invalidate a prepared review.
    pollProcessedTip(true);
    m_tip_timer.start();
}

WalletSession::~WalletSession()
{
    for (auto& handler : m_handlers) handler->disconnect();
}

interfaces::Wallet& WalletSession::wallet() const
{
    Q_ASSERT(m_available);
    return *m_wallet;
}

void WalletSession::invalidate()
{
    if (!m_available) return;
    m_available = false;
    m_tip_timer.stop();
    ++m_generation;
    for (auto& handler : m_handlers) handler->disconnect();
    Q_EMIT invalidated();
}

void WalletSession::pollProcessedTip(bool initial)
{
    if (!m_available || m_tip_read_pending) return;
    struct Snapshot { uint256 tip; bool ready{false}; };
    auto snapshot{std::make_shared<Snapshot>()};
    const QPointer<WalletSession> self{this};
    m_tip_read_pending = true;
    if (!runRead([snapshot](interfaces::Wallet& wallet) {
        interfaces::WalletBalances ignored;
        snapshot->ready = wallet.tryGetBalances(ignored, snapshot->tip);
        return WalletOperationResult{};
    }, [self, snapshot, initial](WalletOperationResult result) {
        if (!self) return;
        self->m_tip_read_pending = false;
        if (result.code != WalletOperationResult::Success || !snapshot->ready || snapshot->tip == self->m_processed_tip) return;
        self->m_processed_tip = snapshot->tip;
        // Suppress only the constructor's successful baseline, not the first
        // eventual success: a failed initial read may precede stale child data.
        if (initial) return;
        Q_EMIT self->activityChanged();
        Q_EMIT self->changed();
    })) m_tip_read_pending = false;
}

bool WalletSession::runAction(std::function<WalletOperationResult(interfaces::Wallet&)> work, WalletOperationExecutor::Completion completion)
{
    if (!m_available || m_action_busy) return false;
    m_action_busy = true;
    Q_EMIT actionBusyChanged();
    const quint64 generation{m_generation};
    const QPointer<WalletSession> self{this};
    const bool accepted = m_executor.submit([backend = m_wallet, work = std::move(work)] { return work(*backend); },
        [self, generation, completion = std::move(completion)](WalletOperationResult result) mutable {
            if (!self) return;
            self->m_action_busy = false;
            Q_EMIT self->actionBusyChanged();
            if (!self->m_available || self->m_generation != generation) return;
            Q_EMIT self->changed();
            completion(std::move(result));
        });
    if (!accepted) {
        m_action_busy = false;
        Q_EMIT actionBusyChanged();
    }
    return accepted;
}

bool WalletSession::runRead(std::function<WalletOperationResult(interfaces::Wallet&)> work, WalletOperationExecutor::Completion completion)
{
    if (!m_available) return false;
    const quint64 generation{m_generation};
    const QPointer<WalletSession> self{this};
    return m_executor.submit([backend = m_wallet, work = std::move(work)] { return work(*backend); },
        [self, generation, completion = std::move(completion)](WalletOperationResult result) mutable {
            if (self && self->m_available && self->m_generation == generation) completion(std::move(result));
        });
}
