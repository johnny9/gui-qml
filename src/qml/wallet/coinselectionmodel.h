// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_COINSELECTIONMODEL_H
#define BITCOIN_QML_WALLET_COINSELECTIONMODEL_H

#include <primitives/transaction.h>
#include <QAbstractListModel>
#include <QTimer>
#include <set>
#include <vector>

class WalletSession;

struct WalletCoin {
    COutPoint outpoint;
    QString address;
    CAmount amount{0};
    int confirmations{0};
    bool locked{false};
    bool operator==(const WalletCoin&) const = default;
};

/** Input policy and a projection of Core's available/locked coins. */
class CoinSelectionModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY changed)
    Q_PROPERTY(bool manual READ manual WRITE setManual NOTIFY changed)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY changed)
public:
    enum Role { CoinKeyRole = Qt::UserRole + 1, AddressRole, AmountRole, ConfirmationsRole, SelectedRole, LockedRole };
    explicit CoinSelectionModel(WalletSession& session, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(m_coins.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int selectedCount() const { return int(m_selected.size()); }
    bool manual() const { return m_manual; }
    void setManual(bool manual);
    bool active() const { return m_timer.isActive(); }
    void setActive(bool active);
    bool busy() const { return m_loading || m_session_action; }
    QString error() const { return m_error; }
    int displayUnit() const { return m_display_unit; }
    void setDisplayUnit(int unit);
    std::vector<COutPoint> selected() const { return {m_selected.begin(), m_selected.end()}; }
    Q_INVOKABLE void select(const QString& key, bool selected);
    Q_INVOKABLE void setLocked(const QString& key, bool locked);
    Q_INVOKABLE void refresh();
    void clear();
Q_SIGNALS:
    void changed();
    void inputChanged();
private:
    static QString key(const COutPoint& outpoint);
    void publish(std::vector<WalletCoin> coins);
    WalletSession& m_session;
    std::vector<WalletCoin> m_coins;
    std::set<COutPoint> m_selected;
    QTimer m_timer;
    QString m_error;
    int m_display_unit{0};
    bool m_manual{false};
    bool m_loading{false};
    bool m_refresh_pending{false};
    bool m_session_action{false};
};

#endif // BITCOIN_QML_WALLET_COINSELECTIONMODEL_H
