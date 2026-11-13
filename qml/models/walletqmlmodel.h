// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETQMLMODEL_H
#define BITCOIN_QML_MODELS_WALLETQMLMODEL_H

#include <qml/models/backuppolicyoperation.h>
#include <qml/models/psbtfeeservices.h>
#include <qml/models/addresslistmodel.h>
#include <qml/backendexecutor.h>
#include <qml/reviewowner.h>
#include <qml/walletopening.h>
#include <qml/senddraft.h>
#include <qml/walletsecurity.h>
#include <qml/walletreceipts.h>
#include <qml/psbtwork.h>
#include <qml/models/feereadservices.h>
#include <qml/models/feereadstate.h>
#include <qml/models/flowdetailsmodel.h>
#include <qml/models/bumptransactionmodel.h>
#include <qml/models/coinslistmodel.h>
#include <qml/models/paymentrequest.h>
#include <qml/models/imageexportoperation.h>
#include <qml/models/receiverequestoperation.h>
#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/signverifymessagemodel.h>
#include <qml/models/transactionactivitymodel.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <qml/models/walletbalancereader.h>

#include <consensus/amount.h>
#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <policy/feerate.h>
#include <psbt.h>
#include <support/allocators/secure.h>
#include <wallet/coincontrol.h>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QUuid>

namespace interfaces {
class Node;
} // namespace interfaces

namespace walletsend { class Operation; class CommitConsent; }
namespace walletsigning { struct Result; }
namespace walletprepare { struct Result; }

class PsbtQmlModel;

struct SendFeePreview {
    CAmount fee{0};
    int inputs{0};
    CAmount rate_per_kvb{0};
    std::optional<CAmount> maximum_amount;
    bool sweeps_wallet{false};
    bool operator==(const SendFeePreview&) const = default;
};

struct FeeReadInput {
    struct Recipient { std::string address; CAmount amount; };
    std::vector<Recipient> recipients;
    wallet::CCoinControl coin_control;
    std::shared_ptr<const CoinsListModel::Selection> selected;
    int remainder_index{-1};
    bool custom_fee_enabled{false};
    QString custom_fee_rate;
    std::shared_ptr<const senddraft::Snapshot> draft;
    quint64 maximum_token{0};
};
struct FeeReadResult {
    QHash<unsigned int, SendFeePreview> estimates;
    std::optional<SendFeePreview> custom;
};

class WalletQmlModel : public QObject, public ReviewOwner
{
    Q_OBJECT
    Q_INTERFACES(ReviewOwner)

public:
    enum class SendSubmission { SubmissionUnknown, SubmissionMempool, SubmissionConfirmed, SubmissionNotInMempool, SubmissionConflicted, SubmissionAbandoned, SubmissionAccepted };
    Q_ENUM(SendSubmission)
    enum class SendState { SendIdle, SendPending, SendNeedsUnlock, SendFailed, SendCommitted, SendUncertain, SendClosed };
    enum class ApprovalState { ApprovalInitial, ApprovalWaiting, ApprovalSigned, ApprovalPartiallySigned, ApprovalFailed, ApprovalClosed, ApprovalOtherReview, ApprovalUncertain };
    Q_ENUM(ApprovalState)
    Q_ENUM(SendState)
    enum class PreparationState { PreparationIdle, PreparationPending, PreparationNeedsUnlock, PreparationReady, PreparationFailed, PreparationStale, PreparationClosed, PreparationUncertain };
    Q_ENUM(PreparationState)
    enum class BalanceState { Unknown, Ready, Busy, Unavailable, Failed, Rejected, Closed };
    Q_ENUM(BalanceState)
    enum class KeyScheme {
        SingleKey = 0,
        WatchOnly,
        MultiKey,
        ExternalSigner,
        Software,
        UnknownScheme,
    };
    Q_ENUM(KeyScheme)

    enum class SecurityState { SecurityIdle, SecurityPending, SecuritySucceeded, SecurityFailed, SecurityUncertain };
    Q_ENUM(SecurityState)

    enum class PsbtImportResult {
        PsbtUnsupported = 0,
        WalletCanSign,
        WalletCannotSign,
        TransactionAlreadyKnown,
        ReviewPending,
    };
    Q_ENUM(PsbtImportResult)
    enum class PsbtActionState { PsbtIdle, PsbtPending, PsbtImported, PsbtKnown, PsbtSaved, PsbtSubmitted, PsbtFailed, PsbtUncertain, PsbtStale, PsbtClosed };
    Q_ENUM(PsbtActionState)

private:
    Q_PROPERTY(bool recipientLabelsPending READ recipientLabelsPending NOTIFY recipientLabelsChanged)
    Q_PROPERTY(QString recipientLabelsError READ recipientLabelsError NOTIFY recipientLabelsChanged)
    Q_PROPERTY(QString recipientLabelsTransactionId READ recipientLabelsTransactionId NOTIFY recipientLabelsChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY displayNameChanged)
    Q_PROPERTY(QString balance READ balance NOTIFY balanceChanged)
    Q_PROPERTY(qint64 balanceSatoshi READ displayedBalanceSatoshi NOTIFY balanceChanged)
    Q_PROPERTY(bool balanceKnown READ balanceKnown NOTIFY balanceObservationChanged)
    Q_PROPERTY(BalanceState balanceState READ balanceState NOTIFY balanceObservationChanged)
    Q_PROPERTY(bool hasExternalSigner READ hasExternalSigner NOTIFY openingMetadataChanged)
    Q_PROPERTY(QVariantList receiveAddressTypes READ availableReceiveAddressTypes NOTIFY openingMetadataChanged)
    Q_PROPERTY(QString preferredReceiveAddressType READ defaultReceiveAddressType NOTIFY openingMetadataChanged)
    Q_PROPERTY(bool receivePreferencePending MEMBER m_receive_preference_pending NOTIFY receivePreferenceChanged)
    Q_PROPERTY(QString receivePreferenceError MEMBER m_receive_preference_error NOTIFY receivePreferenceChanged)
    Q_PROPERTY(bool capabilitiesKnown READ capabilitiesKnown NOTIFY openingMetadataChanged)
    Q_PROPERTY(QString closeQuestionToken READ closeQuestionToken NOTIFY closeQuestionChanged)
    Q_PROPERTY(TransactionActivityModel* transactionActivityModel READ transactionActivityModel CONSTANT)
    Q_PROPERTY(AddressListModel* addressListModel READ addressListModel CONSTANT)
    Q_PROPERTY(CoinsListModel* coinsListModel READ coinsListModel CONSTANT)
    Q_PROPERTY(SendRecipientsListModel* recipients READ sendRecipientList CONSTANT)
    Q_PROPERTY(SignVerifyMessageModel* signVerifyMessageModel READ signVerifyMessageModel CONSTANT)
    Q_PROPERTY(PaymentRequest* currentPaymentRequest READ currentPaymentRequest CONSTANT)
    Q_PROPERTY(PaymentRequest* receivingAddress READ receivingAddress CONSTANT)
    Q_PROPERTY(PaymentRequest* detailPaymentRequest READ detailPaymentRequest CONSTANT)
    Q_PROPERTY(ReceiveRequestHistoryModel* receiveRequests READ receiveRequests CONSTANT)
    Q_PROPERTY(bool receiveRequestReconciliationPending READ receiveRequestReconciliationPending NOTIFY receiveRequestReconciliationPendingChanged)
    Q_PROPERTY(QString receiveRequestReconciliationError READ receiveRequestReconciliationError NOTIFY receiveRequestReconciliationPendingChanged)
    Q_PROPERTY(bool receiveRequestsLoaded MEMBER m_request_storage_loaded NOTIFY receiveRequestStorageChanged)
    Q_PROPERTY(bool receiveRequestsBusy MEMBER m_request_storage_busy NOTIFY receiveRequestStorageChanged)
    Q_PROPERTY(QString receiveRequestStorageError MEMBER m_request_storage_error NOTIFY receiveRequestStorageChanged)
    Q_PROPERTY(bool receivingAddressReady MEMBER m_receiving_address_ready NOTIFY receivingAddressStateChanged)
    Q_PROPERTY(bool receivingAddressPending MEMBER m_receiving_address_pending NOTIFY receivingAddressStateChanged)
    Q_PROPERTY(WalletQmlModelTransaction* currentTransaction READ currentTransaction NOTIFY currentTransactionChanged)
    Q_PROPERTY(QVariantMap currentTransactionFlow READ currentTransactionFlowPreview NOTIFY currentTransactionFlowChanged)
    Q_PROPERTY(FlowDetailsModel* currentTransactionFlowDetails READ currentTransactionFlowDetails CONSTANT)
    Q_PROPERTY(quint64 currentTransactionFlowRevision READ currentTransactionFlowRevision NOTIFY currentTransactionFlowChanged)
    Q_PROPERTY(bool currentTransactionFlowPending READ currentTransactionFlowPending NOTIFY currentTransactionFlowChanged)
    Q_PROPERTY(QString currentTransactionFlowError READ currentTransactionFlowError NOTIFY currentTransactionFlowChanged)
    Q_PROPERTY(bool sendDraftSweepsWallet READ sendDraftSweepsWallet NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(bool currentTransactionSweepsWallet READ currentTransactionSweepsWallet NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionIsImportedPsbt READ currentTransactionIsImportedPsbt NOTIFY currentTransactionChanged)
    Q_PROPERTY(unsigned int targetBlocks READ feeTargetBlocks WRITE setFeeTargetBlocks NOTIFY feeTargetBlocksChanged)
    Q_PROPERTY(qint64 estimatedFeeSatoshi READ estimatedFeeSatoshi NOTIFY estimatedFeeChanged)
    Q_PROPERTY(QString estimatedFeeRate READ estimatedFeeRate NOTIFY estimatedFeeChanged)
    Q_PROPERTY(int estimatedInputCount READ estimatedInputCount NOTIFY estimatedFeeChanged)
    Q_PROPERTY(qint64 sendTotalSatoshi READ sendTotalSatoshi NOTIFY sendAmountExhaustsBalanceChanged)
    Q_PROPERTY(qint64 availableSendBalanceSatoshi READ availableSendBalanceSatoshi NOTIFY sendFactsChanged)
    Q_PROPERTY(bool availableSendBalanceKnown READ availableSendBalanceKnown NOTIFY sendFactsChanged)
    Q_PROPERTY(SendRecipient* maximumRecipient READ maximumRecipient NOTIFY maximumRecipientChanged)
    Q_PROPERTY(QString estimatedFee READ estimatedFee NOTIFY estimatedFeeChanged)
    Q_PROPERTY(bool sendAmountExhaustsBalance READ sendAmountExhaustsBalance NOTIFY sendAmountExhaustsBalanceChanged)
    Q_PROPERTY(bool customFeeEnabled READ customFeeEnabled WRITE setCustomFeeEnabled NOTIFY customFeeEnabledChanged)
    Q_PROPERTY(QString customFeeRate READ customFeeRate WRITE setCustomFeeRate NOTIFY customFeeRateChanged)
    Q_PROPERTY(bool customFeeRateValid READ customFeeRateValid NOTIFY customFeeRateValidChanged)
    Q_PROPERTY(double minimumCustomFeeRate READ minimumCustomFeeRate NOTIFY sendFactsChanged)
    Q_PROPERTY(bool customFeeTargetPending READ customFeeTargetPending NOTIFY customFeeTargetPendingChanged)
    Q_PROPERTY(bool feeEstimatePending READ feeEstimatePending NOTIFY feeEstimatePendingChanged)
    Q_PROPERTY(int feeEstimateRevision READ feeEstimateRevision NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(BumpTransactionModel* bumpModel READ bumpModel CONSTANT)
    Q_PROPERTY(bool isWalletLoaded READ isWalletLoaded NOTIFY walletIsLoadedChanged)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY displayUnitChanged)
    Q_PROPERTY(bool isEncrypted READ isEncrypted NOTIFY securityStateChanged)
    Q_PROPERTY(bool isLocked READ isLocked NOTIFY securityStateChanged)
    Q_PROPERTY(QString keyScheme READ keyScheme NOTIFY openingMetadataChanged)
    Q_PROPERTY(WalletQmlModel::KeyScheme keySchemeKind READ keySchemeKind NOTIFY openingMetadataChanged)
    Q_PROPERTY(QString privateKeysStatus READ privateKeysStatus NOTIFY openingMetadataChanged)
    Q_PROPERTY(QString externalSignerStatus READ externalSignerStatus NOTIFY openingMetadataChanged)
    Q_PROPERTY(bool canManagePassphrase READ canManagePassphrase NOTIFY openingMetadataChanged)
    Q_PROPERTY(QString transactionError READ transactionError NOTIFY transactionErrorChanged)
    Q_PROPERTY(SendState sendState READ sendState NOTIFY sendOperationChanged)
    Q_PROPERTY(quint64 sendOperationId READ sendOperationId NOTIFY sendOperationChanged)
    Q_PROPERTY(PsbtActionState psbtActionState READ psbtActionState NOTIFY psbtActionChanged)
    Q_PROPERTY(quint64 psbtActionId READ psbtActionId NOTIFY psbtActionChanged)
    Q_PROPERTY(bool psbtActionBusy READ psbtActionBusy NOTIFY psbtActionChanged)
    Q_PROPERTY(PsbtImportResult psbtImportResult READ psbtImportResult NOTIFY psbtActionChanged)
    Q_PROPERTY(QString psbtActionError READ psbtActionError NOTIFY psbtActionChanged)
    Q_PROPERTY(QString psbtSavedPath READ psbtSavedPath NOTIFY psbtActionChanged)
    Q_PROPERTY(QString psbtSubmittedTxid READ psbtSubmittedTxid NOTIFY psbtActionChanged)
    Q_PROPERTY(PreparationState preparationState READ preparationState NOTIFY preparationChanged)
    Q_PROPERTY(quint64 preparationId READ preparationId NOTIFY preparationChanged)
    Q_PROPERTY(bool preparationBusy READ preparationBusy NOTIFY preparationChanged)
    Q_PROPERTY(ApprovalState signerApprovalState READ signerApprovalState NOTIFY signerApprovalChanged)
    Q_PROPERTY(quint64 signerApprovalId READ signerApprovalId NOTIFY signerApprovalChanged)
    Q_PROPERTY(QString signerApprovalError READ signerApprovalError NOTIFY signerApprovalChanged)
    Q_PROPERTY(bool sendBusy READ sendBusy NOTIFY sendOperationChanged)
    Q_PROPERTY(QString sendResultTxid READ sendResultTxid NOTIFY sendOperationChanged)
    Q_PROPERTY(SendSubmission sendSubmission READ sendSubmission NOTIFY sendOperationChanged)
    Q_PROPERTY(bool transactionNeedsUnlock READ transactionNeedsUnlock NOTIFY transactionNeedsUnlockChanged)
    Q_PROPERTY(bool currentTransactionCanSend READ currentTransactionCanSend NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool transactionFeePending READ transactionFeePending NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionCanBroadcast READ currentTransactionCanBroadcast NOTIFY currentTransactionChanged)
    Q_PROPERTY(QString currentTransactionReviewMessage READ currentTransactionReviewMessage NOTIFY currentTransactionChanged)
    Q_PROPERTY(BackupFileOperation* backupOperation READ backupOperation NOTIFY backupOperationChanged)
    Q_PROPERTY(QString settingsError READ settingsError NOTIFY settingsErrorChanged)
    Q_PROPERTY(SecurityState securityState READ securityState NOTIFY securityOperationChanged)
    Q_PROPERTY(bool securityBusy READ securityBusy NOTIFY securityOperationChanged)
    Q_PROPERTY(QString securityError READ securityError NOTIFY securityOperationChanged)
    Q_PROPERTY(PsbtQmlModel* importedPsbt READ importedPsbt CONSTANT)

public:
    WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node = nullptr, QObject* parent = nullptr);
    WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node,
                   FeeReadServices fee_services, QObject* parent = nullptr);
    WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node,
                   FeeReadServices fee_services, psbtfee::Services proof_services, QObject* parent = nullptr);
    WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node,
                   FeeReadServices fee_services, psbtfee::Services proof_services, backup::Services backup_services,
                   QObject* parent = nullptr);
    struct SharedWallet {
        std::shared_ptr<interfaces::Wallet> wallet;
        // Production composition shares the controller's bounded R08 lanes.
        std::shared_ptr<execution::Scheduler> observations{};
        QString captured_name{};
        walletopening::Session::Preferences preferences{};
        walletopening::FeePolicy fee_policy{};
        std::shared_ptr<std::atomic_bool> shutdown_gate{};
        walletopening::NativeEvents native_events{};
    };
    WalletQmlModel(SharedWallet wallet, interfaces::Node* node,
                   FeeReadServices fee_services, psbtfee::Services proof_services, backup::Services backup_services,
                   QObject* parent = nullptr);
    WalletQmlModel(interfaces::Node* node, QObject* parent = nullptr);
    WalletQmlModel(QObject *parent = nullptr);
    ~WalletQmlModel();

    QString name() const;
    QString displayName() const;
    void setDisplayName(const QString& display_name);
    QString balance() const;
    qint64 displayedBalanceSatoshi() const { return m_wallet ? m_balance : 0; }
    // Native balance reads are background-only; all facade readers use cache.
    qint64 balanceSatoshi() const;
    bool balanceKnown() const { return m_balance_known; }
    BalanceState balanceState() const { return m_balance_state; }
    void refreshBalance();
    bool hasExternalSigner() const { return m_metadata_known && m_external_signer; }
    bool capabilitiesKnown() const { return m_metadata_known; }
    QString closeQuestionToken() const;
    Q_INVOKABLE void requestClose();
    Q_INVOKABLE void answerClose(const QString& token, bool accept);
    std::shared_ptr<walletopening::Session> opening() const { return m_opening; }
    Q_INVOKABLE ReceiveRequestOperation* commitPaymentRequest();
    Q_INVOKABLE ReceiveRequestOperation* commitPaymentRequestWithPassphrase(const QString& passphrase);
    Q_INVOKABLE void reloadReceiveRequests();
    Q_INVOKABLE ReceiveRequestOperation* removeReceiveRequest(const QString& request_id);
    Q_INVOKABLE bool loadPaymentRequest(const QString& request_id);
    Q_INVOKABLE bool loadPaymentRequestDetail(const QString& request_id);
    Q_INVOKABLE ReceiveRequestOperation* updatePaymentRequest(const QString& request_id, qint64 amount, const QString& label, const QString& message, const QString& note);
    Q_INVOKABLE void usePaymentRequestAsTemplate(const QString& request_id);

    TransactionActivityModel* transactionActivityModel();
    AddressListModel* addressListModel() const { return m_address_list_model; }
    BumpTransactionModel* bumpModel() const { return m_bump_transaction_model; }
    CoinsListModel* coinsListModel() const { return m_coins_list_model; }
    SendRecipientsListModel* sendRecipientList() const { return m_send_recipients; }
    SignVerifyMessageModel* signVerifyMessageModel() const { return m_sign_verify_message_model; }
    PaymentRequest* currentPaymentRequest() const { return m_current_payment_request; }
    PaymentRequest* receivingAddress() const { return m_receiving_address; }
    Q_INVOKABLE ReceiveRequestOperation* ensureReceivingAddress(bool next = false, const QString& address_type = {});
    Q_INVOKABLE ReceiveRequestOperation* ensureReceivingAddressWithPassphrase(const QString& passphrase, bool next = false, const QString& address_type = {});
    Q_INVOKABLE ReceiveRequestOperation* resumeReceivingAddress(ReceiveRequestOperation* original, const QString& passphrase);
    Q_INVOKABLE ReceiveRequestOperation* retryReceivingAddress(ReceiveRequestOperation* original);
    Q_INVOKABLE ReceiveRequestOperation* commitReceivingPaymentRequest();
    PaymentRequest* detailPaymentRequest() const { return m_detail_payment_request; }
    ReceiveRequestHistoryModel* receiveRequests() const { return m_receive_requests; }
    bool receiveRequestReconciliationPending() const;
    QString receiveRequestReconciliationError() const { return m_receive_reconciliation_error; }
    bool receiveRequestReconciliationApplying() const { return m_receive_reconciliation_applying; }
    WalletQmlModelTransaction* currentTransaction() const { return m_current_transaction; }
    QVariantMap currentTransactionFlow() const;
    QVariantMap currentTransactionFlowPreview() const;
    FlowDetailsModel* currentTransactionFlowDetails() const { return m_flow_details; }
    quint64 currentTransactionFlowRevision() const { return m_flow_revision; }
    bool currentTransactionFlowPending() const { return m_flow_pending; }
    QString currentTransactionFlowError() const { return m_flow_error; }
    bool sendDraftSweepsWallet() const;
    bool currentTransactionSweepsWallet() const { return m_current_transaction && m_current_transaction_source == CurrentTransactionSource::SendDraft && m_current_transaction_sweeps_wallet; }
    bool currentTransactionIsImportedPsbt() const { return m_current_transaction_source == CurrentTransactionSource::ImportedPsbt; }
    QString estimatedFee() const;
    qint64 estimatedFeeSatoshi() const;
    QString estimatedFeeRate() const;
    int estimatedInputCount() const;
    qint64 sendTotalSatoshi() const;
    qint64 availableSendBalanceSatoshi() const;
    bool availableSendBalanceKnown() const { return m_send_facts_known; }
    SendRecipient* maximumRecipient() const { return m_maximum_recipient.data(); }
    Q_INVOKABLE void useMaximum();
    Q_INVOKABLE void setCustomFeeTarget(unsigned int target);
    CFeeRate dustRelayFee() const;
    bool sendAmountExhaustsBalance() const;
    bool customFeeEnabled() const { return m_custom_fee_enabled; }
    QString customFeeRate() const { return m_custom_fee_rate; }
    bool customFeeRateValid() const;
    double minimumCustomFeeRate() const;
    bool customFeeTargetPending() const { return m_custom_target_pending; }
    bool feeEstimatePending() const { return m_fee_estimate_pending; }
    int feeEstimateRevision() const { return m_fee_estimate_revision; }
    Q_INVOKABLE quint64 requestPrepareTransaction();
    Q_INVOKABLE quint64 approveExternalSignerTransaction();
    Q_INVOKABLE quint64 requestPrepareTransactionWithPassphrase(const QString& passphrase);
    quint64 preparationId() const { return m_preparation_id; }
    PreparationState preparationState() const { return m_preparation_state; }
    bool preparationBusy() const { return m_preparation_state == PreparationState::PreparationPending; }
    Q_INVOKABLE quint64 requestSendTransaction();
    Q_INVOKABLE quint64 requestSendTransactionWithPassphrase(const QString& passphrase);
    quint64 sendOperationId() const { return m_send_request_id; }
    std::shared_ptr<walletsend::Operation> sendOperation() const { return m_send_operation; }
    Q_INVOKABLE quint64 requestBroadcastCurrentTransaction();
    Q_INVOKABLE QVariantList availableReceiveAddressTypes() const;
    Q_INVOKABLE QString defaultReceiveAddressType() const;
    Q_INVOKABLE QString estimatedFeeForTarget(unsigned int target_blocks) const;
    Q_INVOKABLE int feeTargetIndex(unsigned int target_blocks) const;
    Q_INVOKABLE void scheduleFeeEstimates();
    Q_INVOKABLE ReceiveRequestOperation* setCurrentPaymentRequestAddress(const QString& address);
    SecurityState securityState() const { return m_security_state; }
    bool securityBusy() const { return m_security_state == SecurityState::SecurityPending; }
    QString securityError() const { return m_security_error; }
    Q_INVOKABLE bool encryptWallet(const QString& passphrase);
    Q_INVOKABLE bool changeWalletPassphrase(const QString& old_passphrase, const QString& new_passphrase);
    // Returns admission, not write success. Observe the owned operation outcome.
    Q_INVOKABLE bool backupWallet(const QString& path);
    Q_INVOKABLE ImageExportOperation* savePaymentRequestImage(PaymentRequest* request, const QImage& image, const QUrl& path);
    BackupFileOperation* backupOperation() const { return m_backup_operation; }
    Q_INVOKABLE void clearSettingsError();
    Q_INVOKABLE bool setDefaultReceiveAddressType(const QString& address_type);
    Q_INVOKABLE QString receiveAddressTypeLabel(const QString& address_type) const;
    Q_INVOKABLE quint64 requestImportPsbtFromFile(const QString& path);
    Q_INVOKABLE quint64 requestSaveCurrentTransactionAsPsbt(const QString& path);
    PsbtActionState psbtActionState() const { return m_psbt_action_state; }
    quint64 psbtActionId() const { return m_psbt_action_id; }
    bool psbtActionBusy() const { return m_psbt_action_state == PsbtActionState::PsbtPending; }
    PsbtImportResult psbtImportResult() const { return m_psbt_import_result; }
    QString psbtActionError() const { return m_psbt_action_error; }
    QString psbtSavedPath() const;
    QString psbtSubmittedTxid() const;
    Q_INVOKABLE void discardCurrentTransaction();
    PsbtQmlModel* importedPsbt() const { return m_imported_psbt_model; }
    interfaces::Wallet* wallet() const { return m_wallet.get(); }
    // Background readers retain the interface until their current read finishes.
    std::shared_ptr<interfaces::Wallet> walletHandle() const { return m_wallet; }
    interfaces::Node* node() const { return m_node; }
    std::shared_ptr<BackendExecutor> backendExecutor() const { return m_backend_executor; }
    // False once the wallet is unloading or closing. Set from the unload
    // notification's own thread, before the GUI hears about it.
    bool shutdownComplete() const { return m_shutdown_finished; }
    bool observationsAllowed() const { return !m_stopping && !m_admission_closed; }
    bool available() const { return m_wallet && !m_admission_closed && !m_unload_requested->load(); }
    ApprovalState signerApprovalState() const;
    quint64 signerApprovalId() const { return m_approval_id; }
    QString signerApprovalError() const { return m_approval_error; }
    void closeAdmission();
    void registerReview(WalletQmlModelTransaction* review) override;
    void setReviewRetirementPostForTesting(std::function<void(std::function<void()>)> post);
    void beginShutdown(bool remove_wallet = false);
    void removeWallet();

    ReceiveRequestOperation* setAddressLabel(const QString& address, const QString& label);
    // Write a label to the address book only, without touching any payment
    // request saved for the address. This is the request-save direction: a
    // request's label is its own, so saving one request must not rewrite the
    // labels of sibling requests on the same address.
    // Propagate an edited address book label to any payment request saved for
    // that address (the reverse of the request-save address book sync).
    void selectCoin(const COutPoint& output);
    void unselectCoin(const COutPoint& output);
    bool isSelectedCoin(const COutPoint& output);
    std::shared_ptr<const CoinsListModel::Selection> selectedCoins() const;
    bool coinSelectionPending() const;
    Q_PROPERTY(bool coinSelectionPending READ coinSelectionPending NOTIFY coinSelectionChanged)
    Q_INVOKABLE void clearSelectedCoins();
    unsigned int feeTargetBlocks() const;
    void setFeeTargetBlocks(unsigned int target_blocks);
    void setCustomFeeEnabled(bool enabled);
    void setCustomFeeRate(const QString& fee_rate);

    bool isWalletLoaded() const { return m_is_wallet_loaded; }
    void setWalletLoaded(bool loaded);
    int displayUnit() const { return m_display_unit; }
    void setDisplayUnit(int unit);
    bool isEncrypted() const { return m_is_encrypted; }
    bool isLocked() const { return m_is_locked; }
    QString keyScheme() const;
    KeyScheme keySchemeKind() const;
    static KeyScheme keySchemeForWallet(interfaces::Wallet& wallet);
    static QString keySchemeDisplayText(KeyScheme scheme);
    QString privateKeysStatus() const;
    QString externalSignerStatus() const;
    bool canManagePassphrase() const;
    bool recipientLabelsPending() const { return m_sent_labels_pending; }
    QString recipientLabelsError() const { return m_sent_labels_error; }
    QString recipientLabelsTransactionId() const { return m_sent_labels_txid; }
    SendState sendState() const { return m_send_state; }
    bool sendBusy() const { return m_send_state == SendState::SendPending; }
    QString sendResultTxid() const { return m_send_result_txid; }
    SendSubmission sendSubmission() const { return m_send_submission; }
    QString transactionError() const { return m_transaction_error; }
    bool transactionNeedsUnlock() const { return m_transaction_needs_unlock; }
    bool currentTransactionCanSend() const { return m_current_transaction && m_current_transaction_can_send; }
    bool transactionFeePending() const { return m_psbt_fee_pending; }
    bool currentTransactionCanBroadcast() const { return m_current_transaction && m_current_transaction_can_broadcast; }
    QString currentTransactionReviewMessage() const { return m_current_transaction_review_message; }
    QString settingsError() const { return m_settings_error; }
    void setNode(interfaces::Node* node);

Q_SIGNALS:
    void balanceObservationChanged();
    // Backend content invalidation is separate from cached presentation data.
    void walletContentsChanged();
    void shutdownFinished();
    void nameChanged();
    void displayNameChanged();
    void balanceChanged();
    void securityOperationChanged();
    void openingMetadataChanged();
    void receivePreferenceChanged();
    void closeQuestionChanged();
    void closeConfirmed();
    void currentTransactionChanged();
    void currentTransactionFlowChanged();
    void feeTargetBlocksChanged();
    void estimatedFeeChanged();
    void sendAmountExhaustsBalanceChanged();
    void customFeeEnabledChanged();
    void customFeeRateChanged();
    void customFeeRateValidChanged();
    void feeEstimatePendingChanged();
    void feeEstimateRevisionChanged();
    void maximumRecipientChanged();
    void walletIsLoadedChanged();
    void receiveRequestReconciliationPendingChanged();
    void receiveRequestStorageChanged();
    void receivingAddressStateChanged();
    void externalSignerApprovalSucceeded();
    void signerApprovalChanged();
    void preparationChanged();
    void coinSelectionChanged();
    void psbtActionChanged();
    void sendFactsChanged();
    void customFeeTargetPendingChanged();
    void externalSignerApprovalPartiallySucceeded();
    void externalSignerApprovalFailed(const QString& message, bool signerNotFound);
    void displayUnitChanged(int unit);
    void securityStateChanged();
    void recipientLabelsChanged();
    void transactionErrorChanged();
    void sendOperationChanged();
    void transactionNeedsUnlockChanged();
    void availableChanged();
    void walletUnloaded();
    void backupOperationChanged();
    void settingsErrorChanged();
    void addressListChanged();
    void addressInventoryChanged();
    void addressLabelsChanged();
    // Forwarded on the GUI thread; models must not read wallet state from the
    // Core callback while it holds the wallet lock.
    void transactionChanged(const QString& txid, int change);

private:
    enum class CurrentTransactionSource {
        None,
        SendDraft,
        ImportedPsbt,
    };

    void initializeBackend();
    void initializeBalanceReader();
    void checkShutdownFinished();
    void publishBalance();
    void initializeFeeEstimator();
    void setMaximumRecipient(SendRecipient* recipient);
    void updateMaximumAmount();
    void requestFeeEstimatesNow();
    void invalidateFeeRead(bool close = false);
    void applyFeeEstimates(const QHash<unsigned int, SendFeePreview>& estimates,
                           const std::optional<SendFeePreview>& custom_estimate,
                           quint64 request_id);
    std::optional<CAmount> selectedFeeEstimate() const;
    std::optional<SendFeePreview> selectedFeePreview() const;
    void clearFeeEstimates();
    void refreshSecurityState();
    quint64 requestPrepareTransactionInternal(std::optional<QString> passphrase = {});
    void publishPreparation();
    void invalidatePreparation();
    void setCustomFeeRateInternal(const QString& fee_rate, bool infer_target);
    ReceiveRequestOperation* savePaymentRequest(PaymentRequest* request, bool consume_address = false);
    ReceiveRequestOperation* submitRequest(walletrequests::Command command, PaymentRequest* draft = nullptr, bool consume_address = false);
    void publishRequestHistory();
    bool applyRequest(PaymentRequest* target, walletrequests::PreparedRequest data, bool as_template = false);
    void refreshReceiveRequestPayments();
    void pollUnconfirmedReceiveRequestPayments();
    CAmount receivedPaymentRequestAmount(const QString& address) const;
    void updateReceivedPaymentRequestAmounts();
    quint64 requestSendTransactionInternal(std::optional<QString> passphrase = std::nullopt);
    void publishSendOperation();
    std::shared_ptr<walletsend::Operation> m_send_operation;
    std::shared_ptr<walletsend::CommitConsent> m_send_consent;
    std::shared_ptr<const int> m_send_ticket;
    QPointer<WalletQmlModelTransaction> m_send_review;
    SendState m_send_state{SendState::SendIdle};
    QString m_send_result_txid;
    SendSubmission m_send_submission{SendSubmission::SubmissionUnknown};
    struct SentLabelCapture;
    SentLabelCapture captureSentRecipientLabels(const std::shared_ptr<const walletopening::Session::Action>& action);
    void publishSentRecipientLabels();
    std::shared_ptr<walletrequests::Operation> m_sent_labels_operation;
    std::shared_ptr<const int> m_sent_labels_ticket;
    bool m_sent_labels_pending{false};
    QString m_sent_labels_error, m_sent_labels_txid;
    const std::shared_ptr<std::atomic_bool> m_sending_commit{std::make_shared<std::atomic_bool>(false)};
    quint64 m_send_request_id{0};
    bool unlockForAction(std::optional<SecureString>& passphrase, bool& relock);
    void clearTransactionStatus();
    void setTransactionStatus(const QString& error, bool needs_unlock = false);
    void setSettingsError(const QString& error);
    QString persistedReceiveAddressTypeKey() const;
    enum class PsbtActionKind { Import, Save, Broadcast };
    struct PsbtActionValue { std::shared_ptr<const void> lifetime; psbtwork::ImportResult imported; psbtwork::Result effect; QString error; };
    struct PsbtReceipt { std::mutex mutex; QString path, txid; bool saved{false}, submitted{false}; };
    using PsbtAction = execution::OwnedCommand<PsbtActionValue>;
    quint64 startPsbtAction(PsbtActionKind kind, const QString& path = {});
    void publishPsbtAction();
    std::shared_ptr<PsbtAction> m_psbt_action;
    std::shared_ptr<PsbtReceipt> m_psbt_receipt;
    std::optional<execution::ResourceLease> m_psbt_action_lease, m_psbt_document_lease, m_preparation_lease;
    PsbtActionState m_psbt_action_state{PsbtActionState::PsbtIdle};
    PsbtActionKind m_psbt_action_kind{PsbtActionKind::Import};
    PsbtImportResult m_psbt_import_result{PsbtImportResult::PsbtUnsupported};
    quint64 m_psbt_action_id{0};
    quint64 m_psbt_view_epoch{0}, m_psbt_action_view_epoch{0};
    std::weak_ptr<const PartiallySignedTransaction> m_psbt_action_document;
    bool m_psbt_action_had_document{false};
    QPointer<WalletQmlModelTransaction> m_psbt_action_review;
    bool m_psbt_action_had_review{false};
    QString m_psbt_action_error;


    bool startSecurity(walletsecurity::Kind kind, const QString& current, const QString& replacement);
    void publishSecurityOperation();
    std::shared_ptr<const walletsecurity::Identity> m_security_ticket;
    std::shared_ptr<walletsecurity::Operation> m_security_operation;
    SecurityState m_security_state{SecurityState::SecurityIdle};
    QString m_security_error;
    void initializeOpening();
    void publishOpening();
    struct ReviewRetirement;
    std::shared_ptr<ReviewRetirement> m_review_retirement;
    bool m_reviews_drained{true};
    std::shared_ptr<walletopening::Session> m_opening;
    walletopening::AdmissionFence m_admission_closed;
    walletopening::Session::Preferences m_opening_preferences;
    walletopening::NativeEvents m_opening_native_events;
    walletopening::FeePolicy m_fee_policy;
    std::optional<walletopening::Question> m_close_question;
    const QString m_opening_token{QUuid::createUuid().toString(QUuid::WithoutBraces)};
    bool m_close_confirmed{false}, m_available_published{false};
    QString m_name;
    bool m_can_get_addresses{false}, m_taproot{false}, m_receive_preference_pending{false};
    QString m_receive_preference, m_receive_preference_error;
    bool m_metadata_known{false}, m_private_keys_disabled{false}, m_external_signer{false}, m_opening_drained{true};
    std::shared_ptr<BackendExecutor> m_backend_executor{std::make_shared<BackendExecutor>(execution::Limits::Bounded())};
    std::shared_ptr<QObject> m_notification_bridge;
    std::shared_ptr<execution::Scheduler> m_balance_scheduler;
    std::shared_ptr<walletbalance::Reader> m_balance_reader;
    QTimer m_balance_timer;
    qint64 m_balance{0};
    bool m_balance_known{false};
    BalanceState m_balance_state{BalanceState::Unavailable};
    bool m_stopping{false};
    bool m_backend_drained{false}, m_balance_drained{true}, m_shutdown_finished{false};
    std::shared_ptr<interfaces::Wallet> m_wallet;
    interfaces::Node* m_node{nullptr};
    TransactionActivityModel* m_transaction_activity_model{nullptr};
    AddressListModel* m_address_list_model{nullptr};
    BumpTransactionModel* m_bump_transaction_model{nullptr};
    CoinsListModel* m_coins_list_model{nullptr};
    SendRecipientsListModel* m_send_recipients{nullptr};
    SignVerifyMessageModel* m_sign_verify_message_model{nullptr};
    PaymentRequest* m_current_payment_request{nullptr};
    PaymentRequest* m_receiving_address{nullptr};
    PaymentRequest* m_detail_payment_request{nullptr};
    ReceiveRequestHistoryModel* m_receive_requests{nullptr};
    std::shared_ptr<walletreceipts::Reader> m_receipts;
    std::shared_ptr<walletrequests::Store> m_request_store;
    bool m_request_storage_loaded{false}, m_request_storage_busy{false};
    QString m_request_storage_error;
    bool m_receiving_address_ready{false}, m_receiving_address_pending{false};
    std::shared_ptr<const walletrequests::AddressIntent> m_receiving_intent;
    std::shared_ptr<const walletreceipts::Snapshot> m_receipt_snapshot;
    QTimer m_receive_payment_poll_timer;
    bool m_receive_reconciliation_pending{false}, m_receive_reconciliation_applying{false};
    bool m_receipt_projection_again{false}, m_receipt_needs_poll{false};
    QString m_receive_reconciliation_error;
    void publishReceipts();
    WalletQmlModelTransaction* m_current_transaction{nullptr};
    wallet::CCoinControl m_coin_control;
    QPointer<SendRecipient> m_maximum_recipient;
    QMetaObject::Connection m_maximum_amount_connection;
    QMetaObject::Connection m_maximum_recipient_destroyed_connection;
    bool m_updating_maximum{false};
    struct AutomaticMaximum {
        QPointer<SendRecipient> recipient;
        quint64 token{0}, revision{0}, fee_request{0};
        CAmount amount{0};
        int count{0};
        quint8 capabilities{0};
    };
    std::optional<AutomaticMaximum> m_automatic_maximum;
    void publishAutomaticMaximum(CAmount amount);
    void scheduleFeesForRecipientValidation();
    quint8 automaticMaximumCapabilities() const;
    void invalidateAutomaticMaximum();
    void initializePsbtFeeReader();
    bool requestPsbtFeeProof();
    void applyPsbtFeeProof(const std::shared_ptr<const psbtfee::RequestIdentity>& token, psbtfee::Resolution result, bool publish = true);
    bool matchesReviewedPsbtFee(const PartiallySignedTransaction& psbt) const;
    backup::Services m_backup_services;
    QPointer<BackupFileOperation> m_backup_operation;
    psbtfee::Services m_psbt_fee_services;
    bool m_custom_psbt_lookup{false};
    std::shared_ptr<const psbtfee::RequestIdentity> m_psbt_fee_request;
    std::shared_ptr<const psbtfee::Analysis> m_psbt_fee_proof;
    bool m_psbt_fee_pending{false};
    bool m_psbt_key_can_send{false};
    bool m_psbt_can_commit_complete{false};
    std::shared_ptr<const PartiallySignedTransaction> m_current_psbt;
    quint64 m_approval_id{0};
    ApprovalState m_approval_state{ApprovalState::ApprovalInitial};
    QString m_approval_error;
    std::shared_ptr<execution::OwnedCommand<walletsigning::Result>> m_approval;
    std::shared_ptr<execution::OwnedCommand<walletprepare::Result>> m_preparation;
    PreparationState m_preparation_state{PreparationState::PreparationIdle};
    quint64 m_preparation_revision{0}, m_preparation_requested_revision{0};
    quint64 m_preparation_id{0};
    QPointer<WalletQmlModelTransaction> m_preparation_previous_review;
    bool m_preparation_had_previous_review{false};
    QPointer<WalletQmlModelTransaction> m_approval_review;
    CTransactionRef m_approval_transaction;
    void publishApproval();
    struct SendFacts {
        CAmount available{0}, selected{0}, dust{0}, minimum_relay{0};
        QHash<unsigned int, CAmount> target_rates;
        CAmount selected_target_rate{0};
        std::optional<CAmount> gross_remainder;
        quint64 revision{0};
    };
    struct SendFactsInput {
        std::mutex mutex;
        wallet::CCoinControl control;
        std::shared_ptr<const CoinsListModel::Selection> selected;
        std::shared_ptr<const senddraft::Snapshot> draft;
        quint64 maximum_token{0};
        quint64 revision{0};
    };
    std::shared_ptr<SendFactsInput> m_send_facts_input{std::make_shared<SendFactsInput>()};
    std::shared_ptr<execution::SnapshotQuery<SendFacts>> m_send_facts_query;
    CAmount m_available_send_balance{0}, m_selected_send_balance{0};
    std::optional<CAmount> m_dust_relay_fee;
    std::optional<CAmount> m_minimum_relay_fee;
    QHash<unsigned int, CAmount> m_custom_target_rates;
    bool m_custom_target_pending{false};
    unsigned int m_pending_custom_target{0};
    bool m_send_facts_known{false};
    void requestSendFacts();
    void publishSendFacts();
    struct ReviewFlowInput {
        std::mutex mutex;
        CTransactionRef transaction;
        std::shared_ptr<const PartiallySignedTransaction> document;
        std::shared_ptr<const psbtfee::Analysis> proof;
        QSet<QString> recipients;
        QMap<QString, QString> notes;
        int unit{0};
        quint64 revision{0};
    };
    struct ReviewFlowResult {
        std::shared_ptr<const void> lifetime;
        QVariantMap flow;
        QString error;
        quint64 revision{0};
    };
    std::shared_ptr<ReviewFlowInput> m_flow_input{std::make_shared<ReviewFlowInput>()};
    std::shared_ptr<execution::SnapshotQuery<ReviewFlowResult>> m_flow_query;
    QPointer<WalletQmlModelTransaction> m_flow_review;
    CTransactionRef m_flow_transaction;
    std::shared_ptr<const PartiallySignedTransaction> m_flow_document;
    std::shared_ptr<const psbtfee::Analysis> m_flow_proof;
    int m_flow_unit{-1};
    bool m_flow_pending{false};
    quint64 m_flow_revision{0};
    std::shared_ptr<const void> m_flow_lifetime;
    QVariantMap m_current_flow;
    FlowDetailsModel* m_flow_details{nullptr};
    QString m_flow_error;
    void requestCurrentFlow();
    void publishCurrentFlow();
    void adoptCurrentFlow(QVariantMap flow);
    void publishFlowDetails();
    CurrentTransactionSource m_current_transaction_source{CurrentTransactionSource::None};
    bool m_current_transaction_can_send{false};
    bool m_current_transaction_sweeps_wallet{false};
    bool m_current_transaction_can_broadcast{false};
    QString m_current_transaction_review_message;
    FeeReadServices m_fee_read_services;
    std::shared_ptr<FeeReadState> m_fee_read_state{std::make_shared<FeeReadState>()};
    QHash<unsigned int, SendFeePreview> m_fee_estimates;
    std::optional<SendFeePreview> m_custom_fee_estimate;
    QString m_custom_fee_rate;
    quint64 m_fee_estimate_request_id{0};
    int m_fee_estimate_revision{0};
    bool m_custom_fee_enabled{false};
    bool m_fee_estimate_pending{false};
    bool m_is_wallet_loaded{false};
    bool m_is_encrypted{false};
    bool m_is_locked{false};
    QString m_transaction_error;
    quint64 m_transaction_status_revision{0};
    bool m_transaction_needs_unlock{false};
    QString m_settings_error;
    QString m_display_name;
    std::shared_ptr<std::atomic<bool>> m_unload_requested{std::make_shared<std::atomic<bool>>(false)};
    int m_display_unit{0};
    PsbtQmlModel* m_imported_psbt_model{nullptr};
};

#endif // BITCOIN_QML_MODELS_WALLETQMLMODEL_H
