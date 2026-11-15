// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/paymentimportoperation.h>
PaymentImportOperation::~PaymentImportOperation() { cancel(); }
void PaymentImportOperation::project(paymentimport::Snapshot snapshot)
{
    if (snapshot.owner != m_identity || snapshot.sequence <= m_snapshot.sequence) return;
    m_snapshot = std::move(snapshot);
    Q_EMIT changed();
}
void PaymentImportOperation::use() { if (!m_closed && m_owner) m_owner->use(); }
void PaymentImportOperation::apply() { if (!m_closed && m_owner) m_owner->apply(); }
void PaymentImportOperation::openReview() { if (!m_closed && m_owner) m_owner->openReview(); }
void PaymentImportOperation::replace(quint64 prompt) { if (!m_closed && m_owner) m_owner->replace(prompt); }
void PaymentImportOperation::keepDraft(quint64 prompt) { if (!m_closed && m_owner) m_owner->keepDraft(prompt); }
void PaymentImportOperation::reconcile() { if (m_owner) m_owner->reconcile(); }
void PaymentImportOperation::cancel()
{
    if (std::exchange(m_closed, true)) return;
    if (m_owner) m_owner->close();
}

QString PaymentImportOperation::exportToken() const
{
    return canExport() && m_snapshot.inspection && m_snapshot.inspection->psbt && m_snapshot.inspection->psbt->document
        ? QString::number(m_identity) + QLatin1Char(':') + QString::number(m_snapshot.source) : QString{};
}
bool PaymentImportOperation::requestSaveOriginal(const QString& token, const QString& path)
{
    if (token.isEmpty() || token != exportToken() || (m_export && m_export->pending())) return false;
    const auto document = std::shared_ptr<const PartiallySignedTransaction>{m_snapshot.inspection, m_snapshot.inspection->psbt->document.get()};
    auto* next = new FileExportOperationModel(path, document, m_export_services, this);
    if (m_export) m_export->deleteLater();
    m_export = next;
    Q_EMIT exportChanged();
    return next->start();
}
