// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bumptransactionmodel.h>
#include <qml/backendexecutor.h>

#include <interfaces/wallet.h>
#include <qml/bitcoinamount.h>
#include <qml/models/walletunlock.h>
#include <qml/util.h>
#include <wallet/coincontrol.h>

namespace {
QString FormatFee(CAmount amount)
{
    BitcoinAmount bitcoin_amount;
    bitcoin_amount.setSatoshi(amount);
    return bitcoin_amount.displayWithUnit();
}
} // namespace

BumpTransactionModel::BumpTransactionModel(std::shared_ptr<interfaces::Wallet> wallet, std::shared_ptr<BackendExecutor> executor, QObject* parent)
    : QObject(parent)
    , m_wallet(std::move(wallet))
    , m_executor(executor ? std::move(executor) : std::make_shared<BackendExecutor>())
{
}

void BumpTransactionModel::detachWallet()
{
    ++m_generation;
    m_wallet.reset();
    m_security_state_changed_fn = {};
}

QString BumpTransactionModel::oldFee() const
{
    return FormatFee(m_old_fee);
}

QString BumpTransactionModel::newFee() const
{
    return FormatFee(m_new_fee);
}

QString BumpTransactionModel::feeIncrease() const
{
    return FormatFee(m_new_fee - m_old_fee);
}

QString BumpTransactionModel::oldTxid() const
{
    return QString::fromStdString(m_original_txid.GetHex());
}

void BumpTransactionModel::setState(State state)
{
    if (m_state != state) {
        m_state = state;
        Q_EMIT stateChanged();
    }
}

void BumpTransactionModel::setActionType(ActionType type)
{
    if (m_action_type != type) {
        m_action_type = type;
        Q_EMIT actionTypeChanged();
    }
}

void BumpTransactionModel::setNeedsUnlock(bool needs_unlock)
{
    if (m_needs_unlock != needs_unlock) {
        m_needs_unlock = needs_unlock;
        Q_EMIT needsUnlockChanged();
    }
}

void BumpTransactionModel::setError(const QString& error)
{
    m_error = error;
    setNeedsUnlock(false);
    setState(Failed);
    Q_EMIT resultChanged();
}

void BumpTransactionModel::setUnlockRequired(const QString& error)
{
    m_error = error;
    setNeedsUnlock(true);
    Q_EMIT resultChanged();
}

void BumpTransactionModel::setSecurityStateChangedFn(std::function<void()> fn)
{
    m_security_state_changed_fn = std::move(fn);
}

void BumpTransactionModel::prepareFeeBump(const QString& txid, unsigned int targetBlocks)
{
    if (m_state == Committing) return;
    if (!m_wallet) { setError(tr("No wallet available.")); return; }
    const auto parsed = uint256::FromHex(txid.toStdString());
    if (!parsed) { setError(tr("Invalid transaction ID.")); return; }
    const auto original = Txid::FromUint256(*parsed);
    const auto generation = ++m_generation;
    setActionType(SpeedUp);
    setNeedsUnlock(false);
    m_error.clear();
    setState(Preparing);
    struct Result { CAmount old_fee{0}; CAmount new_fee{0}; CMutableTransaction tx; QString error; };
    if (!m_executor->submit(this, [wallet = m_wallet, original, targetBlocks] {
        Result out;
        wallet::CCoinControl control;
        control.m_signal_bip125_rbf = true;
        control.m_confirm_target = targetBlocks;
        std::vector<bilingual_str> errors;
        if (!wallet->createBumpTransaction(original, control, errors, out.old_fee, out.new_fee, out.tx)) {
            out.error = errors.empty() ? tr("Failed to create bump transaction.") : QString::fromStdString(errors.front().translated);
        }
        return out;
    }, [this, generation, original](Result out) {
        if (generation != m_generation) return;
        if (!out.error.isEmpty()) { setError(out.error); return; }
        m_original_txid = original;
        m_old_fee = out.old_fee;
        m_new_fee = out.new_fee;
        m_bump_mtx = std::move(out.tx);
        setState(NeedsConfirmation);
        Q_EMIT resultChanged();
    }, [this, generation](std::exception_ptr) {
        if (generation == m_generation) setError(tr("Failed to create bump transaction."));
    })) setError(tr("The wallet is closing."));
}

bool BumpTransactionModel::confirmFeeBump()
{
    return confirmFeeBumpInternal(std::nullopt);
}

bool BumpTransactionModel::confirmFeeBumpWithPassphrase(const QString& passphrase)
{
    return confirmFeeBumpInternal(std::optional<SecureString>{QmlUtil::SecureStringFromQString(passphrase)});
}

bool BumpTransactionModel::confirmFeeBumpInternal(std::optional<SecureString> passphrase)
{
    if (!m_wallet || m_state != NeedsConfirmation) return false;
    setNeedsUnlock(false);
    setState(Committing);
    const auto generation = m_generation;
    struct Result { QString txid; QString error; bool needs_unlock{false}; };
    const bool accepted = m_executor->submit(this,
        [wallet = m_wallet, original = m_original_txid, tx = m_bump_mtx, passphrase = std::move(passphrase)]() mutable {
            Result out;
            if (!wallet->privateKeysDisabled() && wallet->isCrypted() && wallet->isLocked() && !passphrase) {
                out.error = tr("Enter your wallet password to update this transaction.");
                out.needs_unlock = true;
                return out;
            }
            bool relock{false};
            if (passphrase) {
                const auto unlocked = TryUnlockWithPassphrase(*wallet, *passphrase);
                passphrase.reset();
                if (unlocked == WalletUnlockResult::IncorrectPassphrase) {
                    out.error = tr("The wallet password you entered was incorrect.");
                    out.needs_unlock = true;
                    return out;
                }
                relock = unlocked == WalletUnlockResult::UnlockedNowRelockRequired;
            }
            WalletRelockGuard guard{*wallet, [] {}, relock};
            if (!wallet->transactionCanBeBumped(original)) {
                out.error = tr("Transaction can no longer be bumped.");
                return out;
            }
            if (!wallet->signBumpTransaction(tx)) {
                out.error = tr("Failed to sign transaction.");
                return out;
            }
            std::vector<bilingual_str> errors;
            Txid bumped;
            if (!wallet->commitBumpTransaction(original, std::move(tx), errors, bumped)) {
                out.error = errors.empty() ? tr("Failed to commit transaction.") : QString::fromStdString(errors.front().translated);
                return out;
            }
            out.txid = QString::fromStdString(bumped.GetHex());
            return out;
        }, [this, generation](Result out) {
            if (m_security_state_changed_fn) m_security_state_changed_fn();
            if (generation != m_generation) return;
            if (out.needs_unlock) {
                setState(NeedsConfirmation);
                setUnlockRequired(out.error);
            } else if (!out.error.isEmpty()) {
                setError(out.error);
            } else {
                m_new_txid = out.txid;
                setState(Succeeded);
                Q_EMIT resultChanged();
            }
            Q_EMIT operationFinished(out.error.isEmpty());
        }, [this, generation](std::exception_ptr) {
            if (m_security_state_changed_fn) m_security_state_changed_fn();
            if (generation != m_generation) return;
            setError(tr("Failed to update transaction."));
            Q_EMIT operationFinished(false);
        });
    if (!accepted) setError(tr("The wallet is closing."));
    return accepted;
}

void BumpTransactionModel::reset()
{
    if (m_state == Committing) return;
    ++m_generation;
    m_state = Idle;
    m_action_type = SpeedUp;
    m_old_fee = 0;
    m_new_fee = 0;
    m_bump_mtx = CMutableTransaction{};
    m_original_txid = Txid{};
    m_new_txid.clear();
    m_error.clear();
    m_needs_unlock = false;

    Q_EMIT stateChanged();
    Q_EMIT actionTypeChanged();
    Q_EMIT resultChanged();
    Q_EMIT needsUnlockChanged();
}
