// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_FEESELECTIONMODEL_H
#define BITCOIN_QML_WALLET_FEESELECTIONMODEL_H

#include <qml/wallet/feepolicy.h>
#include <qml/wallet/senddraftsnapshot.h>
#include <QObject>
#include <QTimer>
#include <functional>
#include <optional>

struct FeePreview {
    quint64 session_id{0};
    quint64 session_generation{0};
    quint64 revision{0};
    quint64 request_id{0};
    std::optional<CAmount> fee;
    bool affordable{false};
    bool fallback{false};
    QString error;
};

/** Fee policy and derived preview only; no mutable recipients or transaction. */
class FeeSelectionModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int target READ target WRITE setTarget NOTIFY policyChanged)
    Q_PROPERTY(bool custom READ custom WRITE setCustom NOTIFY policyChanged)
    Q_PROPERTY(QString customRate READ customRate WRITE setCustomRate NOTIFY policyChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY policyChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY previewChanged)
    Q_PROPERTY(QString estimatedFee READ estimatedFee NOTIFY previewChanged)
    Q_PROPERTY(bool affordable READ affordable NOTIFY previewChanged)
    Q_PROPERTY(QString error READ error NOTIFY previewChanged)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY previewChanged)
public:
    using Complete = std::function<void(FeePreview)>;
    using Estimate = std::function<void(SendDraftSnapshot, quint64, Complete)>;
    explicit FeeSelectionModel(Estimate estimate, QObject* parent = nullptr, int debounce_ms = 250);
    int target() const { return int(m_target); }
    void setTarget(int target);
    bool custom() const { return m_custom; }
    void setCustom(bool custom);
    QString customRate() const { return m_custom_rate; }
    void setCustomRate(const QString& rate);
    bool valid() const { return !m_custom || ParseCustomFeeRate(m_custom_rate).has_value(); }
    FeePolicy policy() const { return {m_target, m_custom ? ParseCustomFeeRate(m_custom_rate) : std::nullopt}; }
    bool pending() const { return m_latest.has_value() && (m_timer.isActive() || m_in_flight); }
    QString estimatedFee() const;
    bool affordable() const { return m_result.fee.has_value() && m_result.affordable; }
    QString error() const { return valid() ? m_result.error : tr("Enter a positive fee rate with at most three decimal places."); }
    int displayUnit() const { return m_display_unit; }
    void setDisplayUnit(int unit);
    const FeePreview& result() const { return m_result; }
    void setDraft(SendDraftSnapshot draft);
    void invalidate();
Q_SIGNALS:
    void policyChanged();
    void previewChanged();
private:
    void launch();
    void policyEdited();
    Estimate m_estimate;
    QTimer m_timer;
    unsigned int m_target{2};
    bool m_custom{false};
    QString m_custom_rate;
    int m_display_unit{0};
    quint64 m_request_id{0};
    std::optional<SendDraftSnapshot> m_latest;
    FeePreview m_result;
    bool m_in_flight{false};
};

#endif // BITCOIN_QML_WALLET_FEESELECTIONMODEL_H
