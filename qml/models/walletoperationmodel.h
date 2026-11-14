// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_WALLETOPERATIONMODEL_H
#define BITCOIN_QML_MODELS_WALLETOPERATIONMODEL_H
#include <qml/walletcreation.h>
#include <QAbstractListModel>
#include <QObject>
#include <QPointer>
#include <QQmlListProperty>
class WalletQmlModel;
Q_MOC_INCLUDE("qml/models/walletqmlmodel.h")
class WalletCreationModel;
class WalletOperationText final : public QAbstractListModel {
    Q_OBJECT
public:
    WalletOperationText(bool warning, QObject* parent);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void bind(std::shared_ptr<walletcreation::Owner> owner);
    void refresh();
private:
    QString text() const;
    const bool m_warning;
    std::shared_ptr<walletcreation::Owner> m_owner;
};
class WalletOperationModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(OperationKind kind READ kind CONSTANT)
    Q_PROPERTY(Kind creationKind READ qmlCreationKind CONSTANT)
    Q_PROPERTY(QString operationId READ operationId CONSTANT)
    Q_PROPERTY(QString submittedName READ submittedName CONSTANT)
    Q_PROPERTY(quint64 editorRevision READ editorRevision CONSTANT)
    Q_PROPERTY(QString targetWalletId READ targetWalletId NOTIFY changed)
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool terminal READ terminal NOTIFY changed)
    Q_PROPERTY(bool outcomeKnown READ outcomeKnown NOTIFY changed)
    Q_PROPERTY(bool acceptedForDelivery READ acceptedForDelivery CONSTANT)
    Q_PROPERTY(WalletOperationError error READ error NOTIFY changed)
    Q_PROPERTY(WalletOperationResult result READ result NOTIFY changed)
    Q_PROPERTY(WalletQmlModel* resultWallet READ resultWallet NOTIFY changed)
    Q_PROPERTY(QQmlListProperty<WalletQmlModel> resultWallets READ resultWallets NOTIFY changed)
    Q_PROPERTY(bool canOpenResult READ canOpenResult NOTIFY changed)
    Q_PROPERTY(bool canCancel READ canCancel NOTIFY changed)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY changed)
    Q_PROPERTY(bool canReconcile READ canReconcile NOTIFY changed)
    Q_PROPERTY(bool canAcknowledge READ canAcknowledge NOTIFY changed)
    Q_PROPERTY(bool encrypted READ encrypted CONSTANT)
    Q_PROPERTY(QString keyScheme READ keyScheme NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel* errorDetails READ errorDetails CONSTANT)
    Q_PROPERTY(QAbstractItemModel* warningDetails READ warningDetails CONSTANT)
public:
    enum OperationKind { Create };
    Q_ENUM(OperationKind)
    OperationKind kind() const { return Create; }
    enum Kind { Software, WatchOnly, ExternalSigner };
    Q_ENUM(Kind)
    Kind qmlCreationKind() const { return static_cast<Kind>(m_request.kind); }
    enum Phase { Requested, Validating, Creating, SettingUp, Publishing, Succeeded, Failed, Partial, Uncertain, Rejected, Cancelled };
    Q_ENUM(Phase)
    struct Services {
        walletcreation::Services native;
        std::shared_ptr<walletpublication::WalletCatalogPublisher> publisher;
        QPointer<QObject> projection_parent;
    };
    static WalletOperationModel* Make(Services services, walletcreation::Request request);
    ~WalletOperationModel() override;
    QString operationId() const { return QString::number(m_request.id); }
    quint64 sequence() const { return m_request.id; }
    walletcreation::Kind creationKind() const { return m_request.kind; }
    QString submittedName() const { return m_request.submitted_name; }
    quint64 editorRevision() const { return m_request.editor_revision; }
    QString targetWalletId() const { return m_frame.target_id; }
    Phase phase() const { return static_cast<Phase>(m_frame.phase); }
    bool busy() const { return walletcreation::Busy(m_frame.phase); }
    bool terminal() const { return walletcreation::Terminal(m_frame.phase); }
    bool outcomeKnown() const { return m_frame.outcome_known; }
    bool acceptedForDelivery() const { return m_accepted; }
    WalletOperationError error() const { return m_frame.error; }
    WalletOperationResult result() const { return m_frame.result; }
    WalletQmlModel* resultWallet() const;
    QQmlListProperty<WalletQmlModel> resultWallets();
    bool canOpenResult() const;
    bool canCancel() const { return busy() && (phase() == Requested || phase() == Validating); }
    bool canRetry() const { return false; }
    bool canReconcile() const;
    bool canAcknowledge() const { return terminal() && !m_acknowledged; }
    bool encrypted() const { return m_request.kind == walletcreation::Kind::Software && m_request.encrypt; }
    QString keyScheme() const { return m_frame.key_scheme; }
    QAbstractItemModel* errorDetails() { return &m_errors; }
    QAbstractItemModel* warningDetails() { return &m_warnings; }
    QString errorText() const;
    QString warningText() const;
    bool publicationActive() const { return m_publishing; }
    void closeAdmission();
    Q_INVOKABLE WalletCreationReceipt cancel();
    Q_INVOKABLE WalletCreationReceipt retry();
    Q_INVOKABLE WalletCreationReceipt reconcile();
    Q_INVOKABLE WalletCreationReceipt acknowledge(const QString& result_id);
Q_SIGNALS:
    void changed();
    void finished(const QString& operation_id, const QString& result_id);
    void commandRejected(const QString& command_id, const WalletOperationError& error);
    void activated(WalletOperationModel* operation);
private:
    friend class WalletQmlControllerTests; // Captures an original outcome owner; never forges its state.
    friend class WalletCreationModel;
    friend class WalletQmlController;
    WalletOperationModel(Services services, walletcreation::Request request);
    void start();
    std::shared_ptr<walletpublication::NativeObservation> nativeObservation() const { return m_ticket ? m_ticket->observation : nullptr; }
    void publish();
    WalletCreationReceipt reject(const char* command, const QString& message);
    Services m_services;
    walletcreation::Request m_request; // Credentials move to the Native owner before exposure.
    std::shared_ptr<walletcreation::Owner> m_owner;
    walletcreation::Frame m_frame;
    std::shared_ptr<walletpublication::Ticket> m_ticket;
    QPointer<WalletQmlModel> m_result_wallet;
    QMetaObject::Connection m_available, m_destroyed;
    WalletOperationText m_errors{false, this}, m_warnings{true, this};
    bool m_closed{false}, m_accepted{false}, m_finished{false}, m_acknowledged{false}, m_publishing{false}, m_publication_in_flight{false}, m_reconciled{false};
};
#endif // BITCOIN_QML_MODELS_WALLETOPERATIONMODEL_H
