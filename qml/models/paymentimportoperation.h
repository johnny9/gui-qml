// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_PAYMENTIMPORTOPERATION_H
#define BITCOIN_QML_MODELS_PAYMENTIMPORTOPERATION_H
#include <qml/paymentimport.h>
#include <QObject>
#include <qml/models/fileexportoperation.h>
class BitcoinUriModel;
class WalletQmlModel;
class SendRecipient;
/** Bounded GUI projection of one independently owned import. Commands retain
 * the original source, wallet opening and recipient; no selection lookup. */
class PaymentImportOperation final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(quint64 requestId READ requestId CONSTANT)
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool canApply READ canApply NOTIFY changed)
    Q_PROPERTY(bool canReview READ canReview NOTIFY changed)
    Q_PROPERTY(bool canExport READ canExport NOTIFY changed)
    Q_PROPERTY(bool canReconcile READ canReconcile NOTIFY changed)
    Q_PROPERTY(quint64 promptToken READ promptToken NOTIFY changed)
    Q_PROPERTY(quint64 resultId READ resultId NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString address READ address NOTIFY changed)
    Q_PROPERTY(QString publicLabel READ publicLabel NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool hasAmount READ hasAmount NOTIFY changed)
    Q_PROPERTY(qint64 amount READ amount NOTIFY changed)
    Q_PROPERTY(QString exportToken READ exportToken NOTIFY changed)
    Q_PROPERTY(FileExportOperationModel* fileExportOperation READ fileExportOperation NOTIFY exportChanged)
public:
    enum class State { Empty, Reading, Analyzing, Inspecting, AwaitingReplacement, Applying, CreatingReview,
        Reconciling, Applied, ReviewOpened, Failed, Closing, Closed };
    Q_ENUM(State)
    ~PaymentImportOperation() override;
    quint64 requestId() const { return m_identity; }
    State state() const { return static_cast<State>(m_snapshot.phase); }
    bool pending() const { return m_snapshot.pending; }
    bool canApply() const { return !m_closed && m_snapshot.can_apply; }
    bool canReview() const { return !m_closed && m_snapshot.can_review; }
    bool canExport() const { return !m_closed && m_snapshot.can_export; }
    bool canReconcile() const { return m_snapshot.can_reconcile; }
    quint64 promptToken() const { return m_snapshot.prompt ? m_snapshot.prompt->id : 0; }
    quint64 resultId() const { return m_snapshot.result ? m_snapshot.result->result : 0; }
    QString error() const { return m_snapshot.error; }
    QString address() const { return m_snapshot.inspection ? m_snapshot.inspection->payment.address : QString{}; }
    QString publicLabel() const { return m_snapshot.inspection ? m_snapshot.inspection->payment.label : QString{}; }
    QString message() const { return m_snapshot.inspection ? m_snapshot.inspection->payment.message : QString{}; }
    bool hasAmount() const { return m_snapshot.inspection && m_snapshot.inspection->payment.amount.has_value(); }
    qint64 amount() const { return hasAmount() ? *m_snapshot.inspection->payment.amount : 0; }
    QString exportToken() const;
    FileExportOperationModel* fileExportOperation() const { return m_export; }
    Q_INVOKABLE bool requestSaveOriginal(const QString& token, const QString& path);
    Q_INVOKABLE void chooseTarget(WalletQmlModel* wallet, SendRecipient* recipient);
    Q_INVOKABLE void use();
    Q_INVOKABLE void apply();
    Q_INVOKABLE void openReview();
    Q_INVOKABLE void replace(quint64 prompt);
    Q_INVOKABLE void keepDraft(quint64 prompt);
    Q_INVOKABLE void reconcile();
    Q_INVOKABLE void cancel();
Q_SIGNALS:
    void changed();
    void exportChanged();
private:
    friend class BitcoinUriModel;
    PaymentImportOperation(quint64 identity, QObject* parent) : QObject(parent), m_identity(identity) {}
    void project(paymentimport::Snapshot snapshot);
    const quint64 m_identity;
    bool m_closed{false};
    backup::Services m_export_services;
    FileExportOperationModel* m_export{nullptr};
    paymentimport::Snapshot m_snapshot;
    std::shared_ptr<paymentimport::Owner> m_owner;
};
#endif // BITCOIN_QML_MODELS_PAYMENTIMPORTOPERATION_H
