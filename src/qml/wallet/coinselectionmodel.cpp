// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/coinselectionmodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/bitcoinunits.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <QPointer>
#include <algorithm>

CoinSelectionModel::CoinSelectionModel(WalletSession& session, QObject* parent)
    : QAbstractListModel(parent), m_session(session)
{
    m_timer.setInterval(2000); // Core lock/unlock RPCs need not emit transaction notifications.
    connect(&m_timer, &QTimer::timeout, this, &CoinSelectionModel::refresh);
    connect(&session, &WalletSession::activityChanged, this, &CoinSelectionModel::refresh);
    connect(&session, &WalletSession::invalidated, this, [this] {
        m_timer.stop();
        m_loading = m_refresh_pending = m_session_action = false;
        publish({});
    });
    refresh();
}

QString CoinSelectionModel::key(const COutPoint& outpoint)
{
    return QString::fromStdString(outpoint.hash.ToString()) + ":" + QString::number(outpoint.n);
}

QVariant CoinSelectionModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& coin = m_coins[index.row()];
    switch (role) {
    case CoinKeyRole: return key(coin.outpoint);
    case AddressRole: return coin.address;
    case AmountRole: {
        const auto unit = QmlBitcoinUnits::fromDisplayUnit(m_display_unit);
        return QString(QmlBitcoinUnits::format(unit, coin.amount) + " " + QmlBitcoinUnits::label(unit));
    }
    case ConfirmationsRole: return coin.confirmations;
    case SelectedRole: return m_selected.contains(coin.outpoint);
    case LockedRole: return coin.locked;
    }
    return {};
}

QHash<int, QByteArray> CoinSelectionModel::roleNames() const
{
    return {{CoinKeyRole, "coinKey"}, {AddressRole, "coinAddress"}, {AmountRole, "coinAmount"},
            {ConfirmationsRole, "confirmations"}, {SelectedRole, "coinSelected"}, {LockedRole, "coinLocked"}};
}

void CoinSelectionModel::setManual(bool manual)
{
    if (m_manual == manual) return;
    m_manual = manual;
    m_selected.clear();
    if (rowCount()) Q_EMIT dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
    Q_EMIT changed();
    Q_EMIT inputChanged();
}

void CoinSelectionModel::setActive(bool active)
{
    if (this->active() == active) return;
    if (active && m_session.available()) { m_timer.start(); refresh(); }
    else m_timer.stop();
    Q_EMIT changed();
}

void CoinSelectionModel::setDisplayUnit(int unit)
{
    if (m_display_unit == unit) return;
    m_display_unit = unit;
    if (rowCount()) Q_EMIT dataChanged(index(0), index(rowCount() - 1), {AmountRole});
    Q_EMIT changed();
}

void CoinSelectionModel::select(const QString& coin_key, bool selected)
{
    if (!m_session.available() || m_session.actionBusy()) return;
    const auto it = std::ranges::find_if(m_coins, [&](const auto& coin) { return key(coin.outpoint) == coin_key; });
    if (it == m_coins.end() || it->locked || m_selected.contains(it->outpoint) == selected) return;
    m_manual = true;
    if (selected) m_selected.insert(it->outpoint);
    else m_selected.erase(it->outpoint);
    const auto row = int(it - m_coins.begin());
    Q_EMIT dataChanged(index(row), index(row), {SelectedRole});
    Q_EMIT changed();
    Q_EMIT inputChanged();
}

void CoinSelectionModel::setLocked(const QString& coin_key, bool locked)
{
    const auto it = std::ranges::find_if(m_coins, [&](const auto& coin) { return key(coin.outpoint) == coin_key; });
    if (!m_session.available() || m_session.actionBusy() || it == m_coins.end()) return;
    const auto outpoint = it->outpoint;
    const QPointer<CoinSelectionModel> self(this);
    m_error.clear();
    m_session_action = true;
    const bool accepted = m_session.runAction([outpoint, locked](interfaces::Wallet& backend) {
        const auto coins = backend.getCoins({outpoint});
        if (coins.size() != 1 || coins[0].is_spent || coins[0].depth_in_main_chain < 0)
            return WalletOperationResult::failure(WalletOperationResult::CoreError, tr("This coin is no longer available."));
        const bool success = locked ? backend.lockCoin(outpoint, true) : backend.unlockCoin(outpoint);
        return success ? WalletOperationResult{} : WalletOperationResult::failure(WalletOperationResult::CoreError, tr("The wallet could not change the coin lock."));
    }, [self](WalletOperationResult result) {
        if (!self) return;
        self->m_session_action = false;
        self->m_error = result.error;
        self->refresh();
        Q_EMIT self->inputChanged();
        Q_EMIT self->changed();
    });
    if (!accepted) {
        m_session_action = false;
        m_error = tr("Another wallet action is in progress.");
    }
    Q_EMIT changed();
}

void CoinSelectionModel::refresh()
{
    if (!m_session.available()) return;
    if (m_loading) { m_refresh_pending = true; return; }
    m_loading = true;
    const auto coins = std::make_shared<std::vector<WalletCoin>>();
    const QPointer<CoinSelectionModel> self(this);
    const bool accepted = m_session.runRead([coins](interfaces::Wallet& backend) {
        for (const auto& [destination, group] : backend.listCoins()) {
            for (const auto& [outpoint, output] : group) {
                coins->push_back({outpoint, QString::fromStdString(EncodeDestination(destination)),
                                  output.txout.nValue, output.depth_in_main_chain, backend.isLockedCoin(outpoint)});
            }
        }
        std::ranges::sort(*coins, std::less<COutPoint>{}, &WalletCoin::outpoint);
        return WalletOperationResult{};
    }, [self, coins](WalletOperationResult result) {
        if (!self) return;
        self->m_loading = false;
        if (result.code == WalletOperationResult::Success) self->publish(std::move(*coins));
        else self->m_error = result.error;
        Q_EMIT self->changed();
        if (std::exchange(self->m_refresh_pending, false)) self->refresh();
    });
    if (!accepted) m_loading = false;
    Q_EMIT changed();
}

void CoinSelectionModel::publish(std::vector<WalletCoin> coins)
{
    if (coins == m_coins) return;
    beginResetModel();
    m_coins = std::move(coins);
    std::erase_if(m_selected, [this](const auto& outpoint) {
        return std::ranges::none_of(m_coins, [&](const auto& coin) { return coin.outpoint == outpoint && !coin.locked; });
    });
    // Keep manual mode if the last selected coin disappears: never silently
    // broaden an explicit selected-only policy to the rest of the wallet.
    endResetModel();
    Q_EMIT inputChanged();
    Q_EMIT changed();
}

void CoinSelectionModel::clear()
{
    setManual(false);
    m_error.clear();
    Q_EMIT changed();
}
