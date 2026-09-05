// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/psbtmodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/bitcoinunits.h>
#include <QPointer>

PsbtModel::PsbtModel(WalletSession& session, interfaces::Node& node, QObject* parent)
    : QObject(parent), m_session(session), m_node(node), m_review(this)
{
    connect(&session, &WalletSession::invalidated, this, &PsbtModel::clear);
    connect(&session, &WalletSession::actionBusyChanged, this, &PsbtModel::changed);
    connect(&m_review, &TransactionReviewModel::changed, this, &PsbtModel::changed);
}

bool PsbtModel::available() const { return m_session.available(); }
bool PsbtModel::busy() const { return m_pending || m_session.actionBusy(); }
bool PsbtModel::canSign() const { return available() && !busy() && !known() && m_inspection.can_sign; }

QString PsbtModel::status() const
{
    if (!available()) return tr("Wallet unavailable.");
    if (m_pending) return tr("Processing PSBT…");
    if (!loaded()) return tr("Import a PSBT to inspect its transaction.");
    if (known()) return tr("This transaction is already known to the wallet or mempool.");
    if (complete()) return m_inspection.fee && m_inspection.inputs_verified
        ? tr("Complete transaction. Review all outputs and the fee before submitting.")
        : tr("Complete transaction, but its inputs or fee cannot be verified. Submission is unavailable.");
    if (m_inspection.can_sign) return tr("Incomplete transaction. This wallet has local signing keys.");
    return tr("Incomplete transaction. This wallet does not have the local keys required to sign.");
}

QVariantList PsbtModel::outputs() const
{
    QVariantList rows;
    const auto unit = QmlBitcoinUnits::fromDisplayUnit(m_review.displayUnit());
    for (const auto& output : m_inspection.outputs) {
        rows.push_back(QVariantMap{{"address", output.address}, {"amount", QString(QmlBitcoinUnits::format(unit, output.amount) + " " + QmlBitcoinUnits::label(unit))}, {"owned", output.owned}});
    }
    return rows;
}

void PsbtModel::clear()
{
    ++m_revision;
    m_document.reset();
    m_inspection = {};
    m_pending = false;
    m_error.clear();
    m_review.clear();
    Q_EMIT changed();
}

void PsbtModel::importFile(const QString& path)
{
    if (path.isEmpty()) return; // Canceling a native file dialog is not a failure.
    import([path](QString& error) { return ReadPsbtDocument(path, error); });
}

void PsbtModel::importData(const QByteArray& data)
{
    import([data](QString& error) { return DecodePsbtDocument(data, error); });
}

void PsbtModel::import(std::function<std::optional<PsbtDocument>(QString&)> read)
{
    if (!available() || busy()) return;
    clear();
    m_pending = true;
    const auto revision = m_revision;
    struct Result { std::optional<PsbtDocument> document; PsbtInspection inspection; };
    const auto result = std::make_shared<Result>();
    const QPointer<PsbtModel> self(this);
    const bool accepted = m_session.runRead([read = std::move(read), result, node = &m_node](interfaces::Wallet& wallet) {
        QString error;
        result->document = read(error);
        if (!result->document) return WalletOperationResult::failure(WalletOperationResult::InvalidInput, error);
        result->inspection = InspectPsbt(result->document->current, wallet, *node);
        return WalletOperationResult{};
    }, [self, revision, result](WalletOperationResult operation) {
        if (!self || self->m_revision != revision) return;
        self->m_pending = false;
        if (operation.code != WalletOperationResult::Success) self->m_error = operation.error;
        else self->publish(std::move(*result->document), std::move(result->inspection));
        Q_EMIT self->changed();
    });
    if (!accepted) {
        m_pending = false;
        m_error = tr("Wallet unavailable.");
    }
    Q_EMIT changed();
}

void PsbtModel::publish(PsbtDocument document, PsbtInspection inspection)
{
    m_document = std::move(document);
    m_inspection = std::move(inspection);
    m_error = m_inspection.error;
    m_review.clear();
    if (!m_inspection.transaction || !m_inspection.fee || !m_error.isEmpty()) return;
    ReviewSnapshot snapshot{m_session.id(), m_session.generation(), m_revision, m_inspection.transaction, *m_inspection.fee, std::nullopt, {}};
    // Ownership is not proof of change. Imported outputs are never hidden or
    // classified as change merely because they pay an address of this wallet.
    for (const auto& output : m_inspection.outputs) snapshot.outputs.push_back({output.address, output.amount, false});
    m_review.setSnapshot(std::move(snapshot));
}
