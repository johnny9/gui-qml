// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/feeselectionmodel.h>
#include <qml/bitcoinunits.h>
#include <QPointer>

FeeSelectionModel::FeeSelectionModel(Estimate estimate, QObject* parent, int debounce_ms)
    : QObject(parent), m_estimate(std::move(estimate))
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(debounce_ms);
    connect(&m_timer, &QTimer::timeout, this, &FeeSelectionModel::launch);
}

void FeeSelectionModel::policyEdited()
{
    invalidate();
    Q_EMIT policyChanged();
}

void FeeSelectionModel::setTarget(int target)
{
    if ((target != 1 && target != 2 && target != 6) || m_target == unsigned(target)) return;
    m_target = target;
    policyEdited();
}

void FeeSelectionModel::setCustom(bool custom)
{
    if (m_custom == custom) return;
    m_custom = custom;
    policyEdited();
}

void FeeSelectionModel::setCustomRate(const QString& rate)
{
    if (m_custom_rate == rate) return;
    m_custom_rate = rate;
    policyEdited();
}

QString FeeSelectionModel::estimatedFee() const
{
    if (!m_result.fee) return {};
    const auto unit = QmlBitcoinUnits::fromDisplayUnit(m_display_unit);
    return QmlBitcoinUnits::format(unit, *m_result.fee) + " " + QmlBitcoinUnits::label(unit);
}

void FeeSelectionModel::setDisplayUnit(int unit)
{
    if (m_display_unit == unit) return;
    m_display_unit = unit;
    Q_EMIT previewChanged(); // Formatting is never an input-policy edit.
}

void FeeSelectionModel::invalidate()
{
    ++m_request_id;
    m_latest.reset();
    m_timer.stop();
    m_result = {};
    Q_EMIT previewChanged();
}

void FeeSelectionModel::setDraft(SendDraftSnapshot draft)
{
    invalidate();
    if (!valid() || draft.recipients.empty()) return;
    m_latest = std::move(draft);
    m_timer.start();
    Q_EMIT previewChanged();
}

void FeeSelectionModel::launch()
{
    if (m_in_flight || !m_latest || !valid()) return;
    m_in_flight = true;
    const auto requested = *m_latest;
    const quint64 request_id = m_request_id;
    const QPointer<FeeSelectionModel> self(this);
    m_estimate(requested, request_id, [self, requested, request_id](FeePreview result) {
        if (!self) return;
        self->m_in_flight = false;
        if (self->m_latest && request_id == self->m_request_id &&
            result.request_id == request_id && result.session_id == requested.session_id &&
            result.session_generation == requested.session_generation && result.revision == requested.revision) {
            self->m_result = std::move(result);
        }
        Q_EMIT self->previewChanged();
        // The timer may already have expired during the running request.
        // Only the newest pending snapshot is ever dispatched next.
        if (self->m_latest && request_id != self->m_request_id && !self->m_timer.isActive()) self->launch();
    });
}
