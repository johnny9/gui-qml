// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/transactionreviewmodel.h>
#include <qml/bitcoinunits.h>

void TransactionReviewModel::setSnapshot(ReviewSnapshot snapshot)
{
    if (!snapshot.transaction) { clear(); return; }
    m_snapshot = std::move(snapshot);
    Q_EMIT changed();
}

void TransactionReviewModel::clear()
{
    if (!m_snapshot) return;
    m_snapshot.reset();
    Q_EMIT changed();
}

QString TransactionReviewModel::format(CAmount amount) const
{
    const auto unit = QmlBitcoinUnits::fromDisplayUnit(m_display_unit);
    return QmlBitcoinUnits::format(unit, amount) + " " + QmlBitcoinUnits::label(unit);
}

QString TransactionReviewModel::transactionId() const
{
    return m_snapshot ? QString::fromStdString(m_snapshot->transaction->GetHash().ToString()) : QString{};
}

QString TransactionReviewModel::feeText() const { return m_snapshot ? format(m_snapshot->fee) : QString{}; }
QString TransactionReviewModel::previousFeeText() const { return m_snapshot && m_snapshot->previous_fee ? format(*m_snapshot->previous_fee) : QString{}; }

QVariantList TransactionReviewModel::outputs() const
{
    QVariantList rows;
    if (!m_snapshot) return rows;
    for (const auto& output : m_snapshot->outputs) {
        rows.push_back(QVariantMap{{"address", output.address}, {"amount", format(output.amount)}, {"change", output.change}});
    }
    return rows;
}

void TransactionReviewModel::setDisplayUnit(int unit)
{
    if (m_display_unit == unit) return;
    m_display_unit = unit;
    Q_EMIT changed();
}
