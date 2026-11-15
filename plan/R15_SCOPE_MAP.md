# R15 focused scope map

Current candidate: source-05, parent app `11a1a7f0529c6e92866ac60eb3b8c7a1a6df8cab`, unchanged Core `44901d8f64cb617714707bd025ecd83f108353c2`. Exact hashes: SNAPSHOT_05.json. This map does not replace R42 qualification.

| Finding / regression scope | Exact focused oracle and resulting behavior |
| --- | --- |
| 062 | `commandLineSignerPreservesEffectiveConfiguration` three real-Core rows (CLI-only, persisted conflict, explicitly disabled); `SettingsNavigation::test_commandLineSignerRemainsUsableAndCanRefresh`. Effective CLI remains authoritative/read-only and is never replaced by persisted state. |
| 086 | `actualSignerProcessPreservesErrorsAndRefreshesWithoutReentry`; `SettingsNavigation::test_lockedDeviceReasonIsVisibleAndRecoveryDoesNotRequireReentry`. Retains actual process cause and recovers without leaving the view. The hardware entry remains present when support is compiled. |
| 156 | `saveReadbackDoesNotOverwriteNewerEditedText`, `successfulPersistenceSurvivesRuntimeOverrideFailure` two rows; `SettingsNavigation::test_invalidSignerValidationAndFailedSaveDoNotRescan`. No optimistic saved/device success, known persistence survives later runtime failure. |
| 157 | `editAndDiscardNotifyTheDerivedApplyCapability`; `SettingsNavigation::test_signerPendingDoesNotShowStaleConnectedDevice`. Uncommitted text is distinct from the effective connection; the typed save action updates through real notifications. |
| 158 | `SettingsNavigation::test_cachedSignerSectionRefreshesOnReturn`, `removalAndReturnRefreshTheCachedStatus`, `sameSeedReplacementSharesTheKeyOriginHint`. Cached settings request fresh device state; equal key-origin hints do not imply the same physical device. |
| 159 | `signerMissingCapabilityIsRetainedForRecreatedViews` two model rows; `ExternalSignerReviewActions::test_error_state_retries_approval`. Retained missing-signer cause produces specific reconnect/retry and survives recreated views. |
| 160 / RC64 | `AsyncOptionsTests::signerSetterRejectsMissingExecutableBeforePersistence`, `pathValidationUsesRealFilesystemFacts` seven rows, actual process integration Options forwarding. No persistence/process call for a missing executable; validates Core's actual command tokenization. |
| 313 / RC62 | `heldSignerProcessDoesNotBlockTheWalletPicker` uses the original public APIs, actual held child process, catalog refresh and physical picker selection before release. `heldSignerProcessAllowsSoftwareSelectionAndConfigurationChanges` additionally proves independent RPC/configuration progress and drops held A in favor of B. Parent-oracle results are separate; do not extrapolate one schedule to all startup behavior. |

Exact class/function/data identities and counts are retained in EXECUTED_SELECTORS.json and per-selection XML/text/raw stderr under local-04. Scope 155 / RC55 remains R33.

## Switching devices and fixed operation identity

* Held discovery A followed by config/B: one active process and one latest successor. Stale A is never current. Covered by manual churn/configuration tests and actual held-process integration.
* A configuration/check revision changes before queued creation enters Core: `changedSignerVersionRejectsQueuedCreationBeforeCoreEntry` three schedules prove no loader effect.
* `signerDescriptorOperationDoesNotRetargetAcrossChanges:fresh-creation-chooses-current-sole-origin`: before binding descriptors, Core may choose sole B; actual created public descriptor strings must be B. Cached A is never stamped as created-wallet identity.
* `:compatible-replacement-during-descriptors`: a held getdescriptors captures A's command fingerprint; compatible same-key replacement succeeds with exact actual A descriptor strings.
* `:incompatible-origin-during-descriptors`: captured A request rejects incompatible B; no loaded wallet/facade/open action. Core throws after an artifact exists, so the app truthfully retains Uncertain/PossibleEffect, directory recovery and no automatic retry. A test's filesystem observation cannot manufacture a known effect receipt.
* `:discovery-after-descriptor-binding`: later B discovery leaves actual A descriptor strings, exact result wallet/result ID and selected wallet unchanged.

These use real disposable Core wallets and subprocesses, not physical hardware. Fingerprint equality is not cryptographic identity. Actual PSBT review/signing handoff changes and compatible/incompatible signing outcomes remain R24. R15 changes no signing internals and does not claim those future checks pass.

## Limitations and final obligations

Core's public subprocess API has no physical cancellation/deadline/output-budget handle. The app deadline is logical; physical invocation and Node lifetime remain owned until actual retirement. Current creation intentionally has no explicit device-selection promise. Future expected-origin creation/read-only descriptor APIs are upstream notes, not current acceptance exceptions. No Core internals, hidden key/address allocation or passive PSBT preparation are used by the app.

Exhaustive coverage, full suites, sanitizers, and platform/Qt/configuration matrices including Windows remain R42 obligations under the user's revised cadence.
