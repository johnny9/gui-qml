# Proposed Qt 6 QML model API

This is the proposed public surface for the model/state contracts (`QT6_MODEL_STATE_CONTRACTS.md`, workspace reference) and threading plan (`QT6_THREADING_AND_ORDERING_PLAN.md`, workspace reference), based on PR 1090 head `02bbdb7711f8b85685e63aa18873e25968aeb848`. Names and signatures below are design contracts, not declarations already implemented in the application. Existing names can remain as compatibility facades during migration.

The 32 internal state contracts do not become 32 independently constructed QML state machines. QML receives a smaller set of feature entry points, typed editors and operation projections. Readers, reducers, backend handles, authorization leases, ingress mailboxes and executors remain private.

The transition completion and validation specification (`QT6_TRANSITION_VALIDATION.md`, workspace reference) and action routing table (`.validation/issue-remediation-pr1090-20261109/model-contracts/action-routes.md`, workspace reference) define the state/event path for every named action below, including supported common operation actions.

## Interface conventions

- All listed properties are read-only to QML unless explicitly identified as presentation-local. Mutable properties have `nameChanged()` notification signals; immutable identity/child pointers may be constant. Derived properties notify when their actual values change. Lists additionally use standard Qt row/data notifications.
- Public actions below are `Q_INVOKABLE` methods or public slots. Both are callable from QML; a method is implemented once, not as duplicate slot and invokable APIs. The GUI implementation only captures transport values and enqueues a command. All domain validation, transitions and Core work execute off-GUI.
- Unless stated otherwise, an action returns a typed `CommandReceipt` containing `commandId`, `acceptedForDelivery` and a transport rejection reason. That is not domain acceptance or backend success. Worker rejection appears in the owning model's scoped error/result; commands never synchronously query workers.
- Factory actions return a typed GUI projection handle immediately. Each handle exposes `FactoryPhase factoryPhase`, `bool pending`, `bool ready`, and `ErrorInfo factoryError`; Requested becomes Ready, Rejected or Cancelled. Domain capabilities are false before Ready. `cancelFactory()` cancels only a Requested factory; a late created owner is retired through its normal lifetime protocol. They allocate a request ID, enqueue the corresponding domain command and begin in a pending state. If transport admission fails, the returned handle exposes that rejection; it does not imply an operation ran. QML never constructs a backend owner.
- Public operation, review, prompt, entity and revision tokens are `QString`. Session association instead uses a typed wallet object; no public session-ID property or session-ID argument is required. Tokens are not indices or values QML should increment or calculate. Editor facades automatically attach their latest submitted input sequence to dependent actions. QML does not supply a guessed draft revision after typing.
- `AmountValue`, `FeeRateValue`, `ErrorInfo`, `CommandReceipt`, `WalletOperationResult`, `TransactionOutcome`, `FileResult` and `VerificationResult` are registered typed value records. No unstructured property bag carries domain state. Canonical amounts stay exact in C++; QML uses their display/entry interface rather than arithmetic on formatted strings.
- Optional object properties are nullable until their value is available. `ready`, `phase`, `available` and capability predicates make this explicit. QML binds to these observations instead of calling readiness getters that reach Core.
- Factories and the operation registry retain operation lifetime independently of the initiating view. A retired facade exposes a terminal/unavailable snapshot and rejects actions; it cannot retarget another wallet. Dispose backend resources on the worker according to the threading contract.

### Session-scoped objects

Every opening creates a new background `WalletSession` object and a distinct GUI `WalletViewModel` with its own feature children. Switching tabs/windows reuses that opening's objects; closing and reopening never reuses or resets the old facade. The registry deduplicates notifications for the same live backend instance.

The shell creates/reuses a view for that facade and changes which view is visible. It must not rebind an existing wallet feature view to `selection.selectedWallet` as selection changes. A reopened wallet gets a new view instance or a freshly constructed feature subtree. Focus/visibility behavior remains local to the appropriate view.

QML receives the wallet object directly. `selection.select(wallet)`, `console.setTarget(wallet)` and `import.chooseTarget(wallet)` establish an association with that exact opening. The receiving facade resolves the object to a private lifetime-safe mailbox token on GUI and enqueues that token. Workers never dereference a GUI QObject, and the token never means “look up whichever session now has this wallet name.” Reject foreign, unavailable or retired objects.

The background owner enforces admission when processing a command and again at the protected backend boundary. Retaining an old operation/session object keeps its accounting alive, not permission to use a closed wallet. Old outcomes remain owned even if their GUI observer is gone. An opaque internal identity may support tracing or serialized messages; neither pointer addresses nor public session strings are the lifetime contract.

A wallet reference in an operation or result is read-only and can only identify the original facade or become null when it is retired/released. It must never start pointing to a newly opened facade. Mutable nullable references notify on change; they are not declared constant. GUI projection references are resolved on GUI and do not appear in worker result payloads. The background context owns and releases Core handles off-GUI independently of facade destruction.

### Shared operation projection

`WalletOperationModel`, `SecurityOperationModel`, `TransactionAttemptViewModel`, `BackupOperationModel`, `FileExportOperationModel` and other operation projections use this common vocabulary. It can be composition or a small C++ base; it does not force every workflow into one enum.

| Properties | Meaning |
| --- | --- |
| `QString operationId`, `WalletViewModel* wallet` | Immutable operation identity and optional original wallet facade; association is never retargeted. Backend session ownership is internal. |
| Per-type enum `phase`; `bool busy`, `bool terminal`, `bool outcomeKnown` | Phase and derived status. Terminal failure is not success; a timeout is not a known outcome. |
| `ErrorInfo error` | Scoped code, localized message, optional field identity and recovery reason. No secrets. |
| `PromptModel* prompt` | Current action-bound prompt, or null. |
| `bool canCancel`, `bool canRetry`, `bool canReconcile`, `bool canAcknowledge` | Permitted commands, derived from phase and owned work. |

Common actions are `cancel()`, `retry()`, `reconcile()` and `acknowledge(resultId)` where supported by that type's FSM. Unsupported actions reject explicitly. Cancel requests safe cancellation; it never asserts rollback of an admitted mutation.

Besides property notifications, an operation exposes `commandRejected(QString commandId, ErrorInfo error)` and optionally `finished(QString operationId, QString resultId)` for presentation adapters. The error and terminal result remain available as properties. Recreated views must render without replaying either signal. `finished` is emitted for an established terminal outcome, including failure, never to mean “sent successfully.” No public `workStarted`, `readCompleted`, `leaseReleased` or executor callback signals are needed.

### Prompts and text fields

| Type | Properties | Actions | Signals beyond property notifications |
| --- | --- | --- | --- |
| `PromptModel` | `QString promptId`, `QString operationId`, `PromptKind kind`, `QString title`, `QString message`, typed `allowedResponses`, `bool busy`, `ErrorInfo error` | `answer(promptId, Response response)`; `submitSecret(promptId, QString secret)` for password prompts; `dismiss(promptId)` | `commandRejected(commandId, error)` |
| `TextEntryModel` | `QString editorId`, `QString text`, `ValidationState validation`, `ErrorInfo error`, `bool editable`, `bool pending` | `edit(QString text)` | `commandRejected(commandId, error)` |
| `AmountEntryModel` | Text-entry properties plus `AmountParseState parseState`, `AmountValue amount`, `DisplayUnit displayUnit` | `edit(QString text)`; clear through owning editor command | Same as text entry |

Fields and prompts are owned child projections, not additional independently scheduled actors. Transient credential text stays in the credential control and command handoff; there is no readable `password` or `secret` property. Multi-field credential operations accept an explicit credential submission method on their operation projection and clear supplied secrets after use. Prompt IDs prevent stale dialog answers from authorizing a successor action.

## Application and wallet management

| Model | Exposed properties | Slots / invokables | Additional signals |
| --- | --- | --- | --- |
| `ApplicationModel` | `NodeModel* node`, `WalletManagerModel* wallets`, `SettingsModel* settings`, `OnboardingModel* onboarding`, `SignerDiscoveryModel* signers`, `PaymentImportManagerModel* imports`, `RuntimePromptQueueModel* prompts`, `bool stopping` | `requestQuit()` | `commandRejected(commandId, error)`; lifecycle observations through properties |
| `WalletManagerModel` | `WalletAvailability availability`, `WalletCatalogModel* catalog`, `OpenWalletListModel* openWallets`, window-scoped `WalletSelectionModel* selection`, `bool acceptingOperations` | `newCreation() -> WalletCreationModel*`; `open(QString walletId) -> WalletOperationModel*`; `restore(QUrl source) -> WalletOperationModel*`; `migrate(QString walletId) -> WalletOperationModel*`; `refreshCatalog()` | Property notifications; each returned operation owns its result signals |
| `WalletCatalogModel` | `ReaderPhase phase`, `bool fresh`, `ErrorInfo error`; roles: `walletId`, `displayName`, `format`, nullable typed `openWallet`, `loadState`, `operationId`, `rowError` | `refresh()` | Qt row/data notifications and property notifications |
| `OpenWalletListModel` | Roles: `displayName`, typed `wallet` facade, `lifecycle`, `available` | No backend actions; use the exact facade's close action | Qt row/data notifications; row identity is internally tied to the opening, not its index |
| `WalletSelectionModel` | `WalletViewModel* selectedWallet`, `QString pendingTargetId`, `bool pending`, `ErrorInfo error` | `select(WalletViewModel* wallet)`; `clear()` | `selectedWalletChanged()` and other property notifications; `commandRejected(commandId, error)` |
| `WalletCreationModel` | `WalletKind kind`, `TextEntryModel* name`, `TextEntryModel* extendedPublicKey`, `QString selectedSignerId`, `bool encryptRequested`, `bool canCreate`, `ErrorInfo error` | `chooseKind(WalletKind)`; `chooseSigner(QString signerId)`; `setEncryptRequested(bool)`; `create(QString passphrase, QString confirmation) -> WalletOperationModel*`; `discard()` | Property notifications; `commandRejected(commandId, error)` |
| `WalletOperationModel` | Common operation properties; `WalletOperationKind kind`, `QString targetWalletId`, `WalletOperationResult result`, nullable `WalletViewModel* resultWallet`, typed `resultWallets` list for auxiliary results, `bool canOpenResult` | Common operation actions; `submitCredentials(promptId, QString passphrase)` where needed | Common operation signals |
| `SignerDiscoveryModel` | `ReaderPhase phase`, `QString effectiveConfigurationId`, typed device list with `signerId`, `name`, `fingerprint`, `available`; `ErrorInfo error`, `bool canRefresh` | `refresh()` | Property/list notifications; `commandRejected(commandId, error)` |

`WalletOperationResult` is a value record containing `resultId`, actual primary/auxiliary wallet identities, warnings, recovery paths and typed partial-effect status. The separate `resultWallet`/`resultWallets` properties expose the original live or retired GUI facades, when retained; they are never resolved to a different opening by wallet name. Completion does not silently change selection. An explicit Done/Open action calls `selection.select(operation.resultWallet)` only when `canOpenResult` is true; its domain guard rechecks availability. If the result wallet closed, reopening is a fresh manager operation. Closing targets the exact wallet facade below, not a mutable global selection.

## One wallet facade

`WalletViewModel` replaces the broad writable surface of `WalletQmlModel`. It exposes:

| Properties | Actions and signals |
| --- | --- |
| `QString walletId`, `WalletLifecycle lifecycle`, `bool available` | Object instance identifies this opening; `walletId` identifies the wallet across openings. Notifications for mutable values; no public setters for identity, lifecycle or availability. |
| `bool canClose`, `PromptModel* closePrompt`, `ErrorInfo closeError` | `requestClose()` posts the intention for this exact session; confirmation answers the captured prompt. Session lifecycle owns teardown after confirmation, with no separate load/publication operation. `commandRejected(commandId, error)` reports a rejected request. |
| `WalletOverviewModel* overview`, `WalletSecurityModel* security`, `WalletSendModel* send`, `WalletReceiveModel* receive`, `WalletActivityModel* activity`, `AddressListModel* addresses`, `WalletStorageModel* storage`, `MessageSigningModel* messages` | Stable typed child properties for that session. Feature actions live on the child that owns them. |

`WalletOverviewModel` exposes `displayName`, `keyScheme`, `balance`, `pendingBalance`, `immatureBalance`, `balanceFreshness` and `ErrorInfo error`, using typed amounts and enums. It has no Core-reading getters or mutation slots. Aliases are edited through storage; security state through security operations. Child facades stay on GUI; their domain owners and backend resources do not.

| Model | Exposed properties | Slots / invokables | Additional signals |
| --- | --- | --- | --- |
| `WalletSecurityModel` | `SecurityState observedState`, `bool canEncrypt`, `bool canChangePassphrase`, `bool canSign`, `bool canGenerateAddresses`, `SecurityOperationModel* activeOperation`, `RecoveryCoverage backupCoverage` | `beginEncryption() -> SecurityOperationModel*`; `beginPassphraseChange() -> SecurityOperationModel*` | Property notifications; no generic `unlocked()` continuation signal |
| `SecurityOperationModel` | Common properties; `SecurityOperationKind kind`, `SecurityOutcome result`, `bool backupRecommended` | `submitCredentials(promptId, QString current, QString replacement, QString confirmation)`; supported common actions | Common operation signals |
| `WalletStorageModel` | `QString displayName`, cached `QUrl location`, `bool canBackup`, `ErrorInfo error` | `editDisplayName() -> TextEditSessionModel*`; `beginBackup() -> BackupOperationModel*`; `requestOpenLocation()` | Token-bound `openLocationRequested(QString requestId, QUrl location)` for the native presentation adapter; matching result reporting API `reportOpenLocation(requestId, bool opened)` |
| `BackupOperationModel` | Common properties; `QUrl selectedPath`, `QUrl finalPath`, `FileResult result` | `chooseDestination(QUrl path)`; overwrite answered through captured prompt; supported common actions | Common operation signals |

No generic wallet-level `unlock()`/`relock()` pair is exposed as a workflow primitive. Protected actions request their own authorization and prompt; cleanup follows the shared backend authorization contract.

## Send, review, PSBT and fee replacement

| Model | Exposed properties | Slots / invokables | Additional signals |
| --- | --- | --- | --- |
| `WalletSendModel` | `SendDraftModel* draft`, `FeeSelectionModel* fees`, `CoinSelectionModel* coins`, `TransactionAttemptViewModel* activeAttempt`, `bool canPrepare`, `ErrorInfo error` | `prepare() -> TransactionAttemptViewModel*`; `inspectPayment(QString text, ImportSource source) -> PaymentImportModel*`; `inspectPsbt(QUrl path) -> PaymentImportModel*`; `clearDraft()` | Property notifications; returned handles own operation signals |
| `SendDraftModel` | `QString draftId`, `QString revision`, `DraftState state`, `RecipientListModel* recipients`, `InputPolicy inputPolicy`, `AmountValue total`, `bool valid`, `ErrorInfo error` | `addRecipient()`; `removeRecipient(QString recipientId)`; `clear()` | Property/list notifications; `commandRejected(commandId, error)` |
| `RecipientListModel` / `RecipientEditorModel` | List roles: `recipientId`, typed `editor`; editor: `TextEntryModel* destination`, `TextEntryModel* privateNote`, `AmountEntryModel* amount`, `AmountMode mode`, typed imported public metadata, `ErrorInfo error` | Editor: `chooseFixedAmount()`; `chooseMaximum()`; `clearImportedMetadata()` | Property/list notifications; `commandRejected(commandId, error)` |
| `FeeSelectionModel` | `FeePolicyKind mode`, `int targetBlocks`, `TextEntryModel* customRate`, `FeeEstimateState estimateState`, `AmountValue estimatedFee`, `FeeRateValue estimatedRate`, `EstimateProvenance provenance`, `ErrorInfo error` | `choosePreset(int targetBlocks)`; `chooseCustom()` | Property notifications; model schedules estimates automatically |
| `CoinSelectionModel` | `bool editorOpen`, `InputPolicy mode`, typed coin list roles: `outpointId`, `amount`, `label`, `selected`, `locked`, `available`, `operationError`; `int selectedCount`, `AmountValue selectedAmount`, `bool canApply`, `bool busy`, `ErrorInfo error` | `beginEdit()`; `setSelected(QString outpointId, bool)`; `chooseAutomatic()`; `chooseManual()`; `apply()`; `cancel()`; `lockSelected()`; `unlockSelected()` | Property/list notifications; `commandRejected(commandId, error)`; lock outcomes retained separately from pending marks |
| `TransactionAttemptViewModel` | Common properties; `AttemptPhase phase`, `AttemptKind kind`, `QString draftId`, `QString draftRevision`, `QString reviewId`, `TransactionReviewModel* review`, `bool canSignLocally`, `bool canApproveOnDevice`, `bool canRequestSubmit`, `bool canExport`, `bool canRecheck`, `bool canReprepare`, per-action disabled reasons, `SignerPhase signerPhase`, `TransactionOutcome outcome` | `signLocally(reviewId)`; `approveOnDevice(reviewId)`; `confirm(reviewId)`; `recheck()`; `reprepare()`; `beginExport(reviewId) -> FileExportOperationModel*`; supported common actions | Common operation signals; signer progress/errors are properties, not an independent QML-maintained state machine |
| `TransactionReviewModel` | `QString reviewId`, `QString candidateId`, `QString documentRevision`, typed recipient/input/output list models, `AmountValue amount`, `AmountValue fee`, `AmountValue total`, `FeeProvenance feeProvenance`, `SignatureState signatures`, `EligibilityState eligibility`, `QString eligibilityReason`, `bool sweepsWallet`, replacement facts, `PsbtDocumentModel* document` | No mutation slots; attempt owns all actions | Property/list notifications; changed unsigned terms create a new review identity |
| `PsbtDocumentModel` | `QString documentId`, `QString revision`, `AnalysisState analysisState`, typed read-only inspection facts/rows, `SignatureState signatures`, `FeeProvenance feeProvenance`, `bool exportable`, `ErrorInfo error` | No raw-byte setter; inspect/sign/export through operation owners | Property notifications |
| `FileExportOperationModel` | Common properties; `ExportKind kind`, captured document/snapshot ID and revision, `QUrl finalPath`, `FileResult result` | `chooseDestination(QUrl)`; overwrite through prompt; supported common actions | Common operation signals |

`confirm(reviewId)` is the explicit Send/Broadcast intent. For a supported local-sign-and-send flow it captures that intent and can request authorization/signing before final submission. `signLocally()` and `approveOnDevice()` are sign-only and cannot silently submit. Capability predicates distinguish these paths. Full-wallet confirmation is a token-bound prompt owned by the attempt. Fee replacement uses another `TransactionAttemptViewModel` with `kind = Replacement`, created from transaction details; it shares the execution protocol, not mutable state with a Send attempt.

`TransactionOutcome` includes `resultId`, exact transaction identity, wallet-recording status, mempool outcome, relay disposition and explanatory reason. No `transactionSent()` signal substitutes for those facts. A transaction can be recorded while rejected by the mempool. `finished()` does not decide the success wording or clear a successor draft.

## Receive, Activity and addresses

| Model | Exposed properties | Slots / invokables | Additional signals |
| --- | --- | --- | --- |
| `WalletReceiveModel` | `ReceiveReservationModel* reservation`, `PaymentRequestEditorModel* draft`, `ReceiveRequestListModel* requests`, `PaymentRequestModel* selectedRequest`, `ErrorInfo error` | `beginSharing(QString requestId) -> RequestSharingModel*`; `ensureAddress()`; `nextAddress(AddressType type)`; `editRequest(QString requestId) -> PaymentRequestEditorModel*`; `useAsTemplate(QString requestId)`; `selectRequest(QString requestId)`; `clearSelection()`; `removeRequest(QString requestId) -> PaymentRequestEditorModel*` for a dedicated deletion operation | Property/list notifications; `commandRejected(commandId, error)` |
| `ReceiveReservationModel` | `ReservationPhase phase`, `QString reservationId`, `QString address`, `AddressType addressType`, `bool available`, `bool historicallyUsed`, `bool busy`, `PromptModel* prompt`, `ErrorInfo error` | `retry()`; `cancel()` where safe | Property notifications; no generic `addressGenerated` signal needed to copy fields |
| `PaymentRequestEditorModel` | Common persistent-editor properties below; `TextEntryModel* publicLabel`, `TextEntryModel* message`, `TextEntryModel* privateNote`, `AmountEntryModel* amount`, `AddressType addressType`, `QString reservationId`, `bool publicFieldsEditable`, `EditorMutationKind mutationKind` | `chooseAddressType(AddressType)`; `save()`; `discard()`; close/conflict actions below | Common editor signals; saved request remains available as a result property |
| `PaymentRequestModel` / request list roles | `requestId`, `address`, `addressType`, public label/message, private note, typed requested/received amounts, `ReceiptState receiptState`, `bool historicallyUsed`, `bool publicFieldsEditable`, `QUrl paymentUri`, `bool fresh` | No row setters; scoped editor/service owns changes | Property/list notifications |
| `WalletActivityModel` | Typed activity rows, `ReaderPhase phase`, `bool fresh`, `ErrorInfo error`, typed `filter`, `TransactionDetailModel* detail` | `selectTransaction(QString txid)`; `clearSelection()`; `setFilter(ActivityFilter value)`; `refresh()`; `beginExport() -> FileExportOperationModel*` | Property/list notifications; no refresh request from individual row getters |
| `TransactionDetailModel` | `QString txid`, `bool exists`, `ReaderPhase phase`, owned details/flow/amount/status, `bool canBumpFee`, `QString bumpDisabledReason`, `ErrorInfo error` | `beginFeeBump() -> TransactionAttemptViewModel*` | Property notifications; result is always scoped to selected transaction ID |
| `AddressListModel` | Roles: `addressId`, `address`, `label`, `addressType`, typed amounts, `canEdit`, request associations; `ReaderPhase phase`, `ErrorInfo error` | `editLabel(QString addressId) -> TextEditSessionModel*`; `setFilter(AddressFilter value)` | Property/list notifications |
| `MessageSigningModel` | `TextEntryModel* address`, `TextEntryModel* message`, `MessageSigningPhase phase`, `QString signature`, `VerificationResult verification`, `bool canSign`, `bool canVerify`, `PromptModel* prompt`, `ErrorInfo error` | `sign()`; `verify(QString signature)`; `cancel()`; `clear()` | Property notifications; `commandRejected(commandId, error)` |

Request sharing uses the [V8 sharing protocol](QT6_ARCHITECTURE_FOUNDATION.md#v8-request-sharing-and-qr-effect-protocol). It preserves request-use and presentation authorization separately from immutable export bytes.

| Model | Exposed properties | Slots / invokables | Additional signals |
| --- | --- | --- | --- |
| `RequestSharingModel` | `QString requestId`, `QString payloadRevision`, `SharingInterest phase`, `bool active`, `bool canCopyUri`, `bool canCopyQr`, `bool canExportQr`, `ErrorInfo disabledReason` | `setActive(bool)`; `copyUri() -> ShareOperationModel*`; `copyQr() -> ShareOperationModel*`; `beginQrExport() -> FileExportOperationModel*` | Property notifications; `commandRejected(commandId, error)` |
| `ShareOperationModel` | `QString operationId`, `SharePhase phase`, `bool busy`, `bool outcomeKnown`, `bool canCancel`, `ErrorInfo error`, typed `ShareResult result` | `cancel()` | Property notifications; `commandRejected(commandId, error)`; results remain properties |

Sharing interest is scoped to a view, not global wallet selection. An inactive/closed sharing context cannot admit copy/export; another active view remains independent. QR export retains a revocable source authorization through M24 write admission. Clipboard/scene/native callbacks are private adapters, not public QML continuations or Core-reading getters.

Receipt reconciliation, transaction readers and address reservations do not independently rewrite public request fields. Request again/template is a domain command that creates fresh intent; the view does not copy saved fields into a live object. Expensive history filtering/export preparation and signature verification remain off-GUI.

### Persistent editor protocol

Request, label/display-name and settings editors share properties `editorId`, `entityId`, `baseRevision`, `editRevision`, `phase`, `dirty`, `valid`, `busy`, `canSave`, `canDiscard`, `ErrorInfo error`, typed `conflicts`, optional `PromptModel* closePrompt`, and a typed `saveResult`.

Actions are `save()`, `discard()`, `requestClose()`, `resolveConflict(fieldId, ConflictChoice choice)` and query-only `reconcile()` while the write outcome is unknown. The close prompt accepts Save/Discard/KeepEditing for its captured editor/token. Generic conflicts do not imply automatic overwrite authority. Signals are property notifications, `commandRejected(commandId, error)` and optional `closed(editorId)` for a navigation adapter. The terminal `phase` remains sufficient to reconstruct the UI. Saving is asynchronous; a write outcome does not clear successor edits.

`TextEditSessionModel` adds a typed text-entry child for a label or display alias. `SettingsEditorModel` exposes a typed settings-entry list with `settingId`, value-kind, editor child, effective/committed values, provenance, restart applicability, editability and field error. Save submits the complete edited batch. No generic QML assignment to persistent Core settings is exposed.

## Node, settings, onboarding and tools

| Model | Exposed properties | Slots / invokables | Additional signals |
| --- | --- | --- | --- |
| `NodeModel` | `NodeLifecycle phase`, `bool ready`, `bool stopping`, typed sync/mempool/status snapshots, `NetworkModel* network`, typed `peers`, typed `console`, typed `debugLog`, `ErrorInfo error` | `start()` when supported by startup lifecycle; `requestShutdown()` | Property notifications; no public Core callback signals |
| `NetworkModel` | `NetworkObservedState observedState`, `NetworkCommandPhase phase`, `bool canChange`, `ErrorInfo error` | `requestActive(bool)`; `refresh()` where needed | Property notifications; `commandRejected(commandId, error)` |
| `RuntimePromptQueueModel` | `PromptModel* activePrompt`, `int queuedCount`, `bool stopping` | Answers/dismissals through the active prompt | Property notifications; Core continuation is private |
| `SettingsModel` | Typed committed/effective settings entries and provenance, `bool ready`, `ErrorInfo error`, `DisplayUnit displayUnit`, UI preferences | `beginEdit(SettingsSection section) -> SettingsEditorModel*`; `refresh()` | Property/list notifications; persistence completion belongs to editor |
| `OnboardingModel` | `OnboardingPhase phase`, typed chosen profile/path editor, current preview/storage results, `bool canFinish`, `ErrorInfo error` | `chooseProfile(ProfileKind)`; `chooseDataDirectory(QUrl)`; `finish()`; `retry()`; `cancel()` where safe | Property notifications; optional `handoffCompleted(resultId)` with retained handoff-result property |
| `RpcConsoleModel` | `RpcPhase phase`, `bool busy`, typed input/history/output/completion models, nullable `WalletViewModel* targetWallet`, `RpcTargetState targetState` distinguishing node-only/available-wallet/unavailable-wallet, `bool canExecute`, `ErrorInfo error` | `setTarget(WalletViewModel* wallet)`; `editInput(QString text)`; `execute()` after latest input; `clearOutput()`; `acceptHistory(QString entryId)`; `acceptCompletion(QString completionId)` | Property/list notifications; `commandRejected(commandId, error)`; actual RPC outcome retains its original target even if the console target later changes |
| `DebugLogModel` | Typed bounded rows, `LogAvailability availability`, `ReaderPhase phase`, `QUrl path`, `bool following`, `ErrorInfo error` | `setFollowing(bool)`; `refresh()`; `loadOlder()`; `setFilter(LogFilter value)` | Property/list notifications |
| `PeerListModel` / `PeerDetailModel` | Stable connection IDs, peer roles, reader state/error; detail has owned snapshot, connected state, `canDisconnect`, `canBan` | `select(connectionId)`; detail `disconnectPeer()` and `ban(BanDuration)`; `setFilter(PeerFilter value)`; `refresh()` | Property/list notifications; `commandRejected(commandId, error)` |
| `PaymentImportManagerModel` | Typed pending desktop-intent list and availability | `inspectText(QString text, ImportSource) -> PaymentImportModel*`; `inspectFile(QUrl) -> PaymentImportModel*`; `dismissIntent(deliveryId)` | Property/list notifications; native delivery events are adapted into owned intents |
| `PaymentImportModel` | Common operation properties; import kind/source, typed parsed preview, optional `PsbtDocumentModel* document`, nullable `WalletViewModel* targetWallet`, target draft ID/revision, `bool canApply`, `bool canReview`, `bool canExport`, replacement `PromptModel* prompt`, typed result | `chooseTarget(WalletViewModel* wallet)`; `apply()` for payment intent; `openReview() -> TransactionAttemptViewModel*` for PSBT; `beginExport() -> FileExportOperationModel*` where valid; `cancel()` | Common operation signals; applying does not sign or submit |

The PSBT document and its read-only inspection facts can exist without a wallet for inspection/export. `openReview()` creates the wallet-bound attempt only after an available target has been chosen; `canReview` describes that capability. A signing action captures an eligible wallet session. Neither imported fields nor selected-wallet changes silently retarget an existing attempt.

For console node-only commands, an explicit `setTarget(null)` selects no wallet. A previously selected wallet disappearing must instead become an unavailable captured target and reject wallet-scoped execution; it must not silently fall back to node-only mode. Before applying an import, an explicit target change invalidates any old overwrite prompt and target revision. An already-created attempt remains bound to its original session object.

## Internal contracts mapped to public projections

| Internal contract | Public surface |
| --- | --- |
| M01 WalletSession | WalletViewModel lifecycle/availability; backend context private |
| M02 WalletOperation | WalletOperationModel and creation editor |
| M03 ActionAuthorization | Action-bound PromptModel and operation capabilities; lease private |
| M04 TransactionAttempt | TransactionAttemptViewModel and TransactionReviewModel |
| M05 SignerApproval | Attempt signer phase/capabilities/error; device job private |
| M06 SendDraft | SendDraftModel and recipient editors |
| M07 CoinSelectionSession | CoinSelectionModel |
| M08 FeeEstimateReader | FeeSelectionModel estimates/freshness; reader private |
| M09 PsbtAnalysis | PsbtDocumentModel and review facts; analyzer private |
| M10 ReplacementEligibility | TransactionDetailModel capability and replacement attempt facts |
| M11 ReceiveAddressReservation | ReceiveReservationModel |
| M12 PaymentRequestEditor | PaymentRequestEditorModel |
| M13 ReceiptReconciler | Request/Activity amount and receipt-state properties; reconciler private |
| M14 TransactionStoreReader | WalletActivityModel rows/freshness; store reader private |
| M15 TransactionDetailReader | TransactionDetailModel |
| M16 AddressLabelEditor | TextEditSessionModel from AddressListModel |
| M17 SettingsEditSession | SettingsEditorModel |
| M18 OnboardingFlow | OnboardingModel |
| M19 NodeLifecycle | NodeModel/ApplicationModel lifecycle properties |
| M20 RuntimePrompt | RuntimePromptQueueModel and PromptModel |
| M21 NetworkCommand | NetworkModel |
| M22 RpcExecution | RpcConsoleModel execution/outcome |
| M23 SignerDiscovery | SignerDiscoveryModel |
| M24 FileWriteOperation (backup/export) | BackupOperationModel and FileExportOperationModel |
| M25 MessageSigning | MessageSigningModel |
| M26 PaymentImport | PaymentImportModel |
| M27 DebugLogReader | DebugLogModel |
| M28 PeerDetailSelection | PeerDetailModel |
| M29 DesktopIntentDelivery | PaymentImportManagerModel pending intent projections; queue private |
| M30 AmountEntryValue | AmountEntryModel plus typed AmountValue |
| M31 WalletCatalogReader | WalletCatalogModel |
| M32 WalletSecurityOperation | SecurityOperationModel |

## Signal and API acceptance

Every mutable exposed property needs an appropriate notification, including derived capabilities and disabled reasons. Update the full coherent cache before emitting notifications. Test property changes without backend commands being triggered by bindings. List roles use stable IDs and proper incremental notifications; worker code never calls Qt model mutation APIs.

Operation results, errors and active prompts are properties so a view can attach late. Optional signals exist for one-shot presentation effects, not as the only record of domain state. Factories produce scoped handles; generic controller success/error signals shared by unrelated forms are removed as consumers migrate.

Reject stale review/prompt/entity tokens, invalid state/action pairs, unsupported capabilities and actions after closing. Test Edit → Prepare with delayed acknowledgment, duplicate Confirm before busy publication, accepted work followed by observer destruction, and later subscriptions to completed operations using the manual execution contract (`QT6_THREADING_AND_ORDERING_PLAN.md#manual-executors-and-deterministic-tests`, workspace reference). Each command signature maps to a declared event or documented value/selection operation in the action routing table; new signatures must update that table and its checks. This mapping does not implement the transitions.

Before implementation, specify each value record/enum in C++ and verify the generated QML type information with the supported Qt/tooling matrix. This catalog intentionally does not expose executors, Core interfaces, arbitrary callback registration, raw mutable transaction bytes or an operation-state setter to QML. No application code or executable API tests were changed by this specification.
