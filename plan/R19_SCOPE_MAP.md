# R19 retained worker scope map

Copied from `.validation/qt6-r11-r40-20261114/R19/plan/scope-map.md`.
Relative evidence references below resolve against that workspace task's R19
directory. They are not bundled under this branch's historical plan/evidence.

# R19 scope and oracle map

Status: R19 implementation and focused acceptance evidence are complete for
parent review; this note does not close the canonical findings or the overall
R42 qualification gate.

## Source identities

- Integrated base: app `9c99b71c8c777a977fce21f5b0cfff4534a6d3dd`, Core
  `44901d8f64cb617714707bd025ecd83f108353c2`.
- Candidate app commit/tree: `84b985e9b1db05a29c8c6838aa586aa5ae8608a1` /
  `ce94592e7a81883695f4a83231d1f417a71255f3`.
- Bitcoin Core remains clean at `44901d8f64cb617714707bd025ecd83f108353c2`.
- The exact integrated-base parent control is `parent-current/source` at
  app `9c99b71c8c777a977fce21f5b0cfff4534a6d3dd`, Core
  `44901d8f64cb617714707bd025ecd83f108353c2`, with only the three
  test-only control patches retained in
  `evidence/parent-current-red-patches/`. The separate historical pre-rebase
  parent control is `parent-red/source` at `702acef0aa67b3a159ca0f55c9de71592e8bdf57`
  based on `b16d87ba608f346809e7b3a32587d0d9ad601554`, Core 449.
- The first control attempt `owner-original-scopes-parent-control-16` is
  invalid: it mounted the R10 app source and produced an unexpected `selection`
  identity. It is preserved and not counted. The initial parent-red attempt
  `owner-parent-red-focused-24` was blocked by its uninitialized Core
  submodule; after initializing exact Core449 locally, the retry executed all
  three nonempty selector bodies and retained their assertion failures.
- Exact candidate selectors and executed identities are retained in each
  focused manifest and `actual/report.json`; expected counts are not used as
  pass evidence.

## Original findings and current dispositions

| Scope | Original condition and disposition | Existing/new oracle |
| --- | --- | --- |
| D015 | Unknown-fee, fallback, and review/export projection concerns. R19 does not claim a new fee-policy fix. Current-head standalone reproduction was not established in the scope record. Preserve and rerun the Core-authoritative fee review rows. | `SendIntegrationTests.feePreviewLimitationKeepsCoreReviewAuthoritative` (2 rows); inherited R17 fee/provenance selectors. Candidate manifest includes the two review rows. |
| RC24 020/251 | Case-sensitive raw-address membership can mark a canonical, owned recipient output as change. Fix compares decoded destination scripts. | `PsbtWorkTests.uppercaseCanonicalRecipientIsNotClassifiedAsChange` and `SendIntegrationTests.uppercaseBech32RecipientRemainsRecipientOutput`; strict-parent unit and UI/backend stale-send controls are in the parent manifest. |
| D111 | Historical state/flag inventory only; no standalone runtime defect was established. The reducer adds explicit typed phase/ticket/session/review ownership while the wallet facade retains effects. | `TransactionAttemptTests` (7 bodies), plus `WalletQmlModelTests` manual preparation/admission cases and the send integration class. |
| D138 | Existing wallet-unload control was assessed sound; no separate correction is claimed. | Existing `WalletQmlModelTests.preparationAdmissionKeepsOriginalEffect` (4 rows) and `preparationRejectionKeepsOriginalOutcome` (3 rows), plus `closeRetiresCandidateAndRejectsLateCompletion`. |
| D257 | Source-only candidate; normal-workflow impact was not independently established. The attempt reducer and manual interleavings test the specified stale/failure contracts without claiming this finding is independently reproduced. | `heldPrepareCompletionCannotInstallEditedDraft` (`stale-success`, `stale-error`), `editedDraftCannotSendPreviouslyReviewedTransaction`, and `preparationFailureRetainsTheExactCurrentReview`. |
| RC80 307 | **Supported refutation at Core 44901:** the current maximum path does not bypass Core's ordinary chain/package policy. `walletprepare::Execute` preserves Automatic/Manual intent in `CCoinControl` and sends both modes through `wallettransaction::Create`. For a maximum recipient that helper computes the remainder, then calls the public `interfaces::Wallet::createTransaction` with the original control. `interfaces::Wallet::createTransaction` forwards to Core `CreateTransaction`; Core's `CreateTransactionInternal` checks `-walletrejectlongchains` and invokes `chain().checkChainLimits(tx)` before returning a transaction. The default is enabled. Automatic selection also filters candidates by ancestor and cluster limits; Manual selection remains subject to the final transaction check. Thus there is no custom transaction construction path that omits the check. No app/Core source change is indicated. This is a source-path refutation, not a claim that an app maximum test ran against an artificially lowered cluster limit. | Current R19 mode coverage: `WalletPrepareTests.maximumRemainderIgnoresOnlyTheReplacedAmount`; `SendIntegrationTests.maximumAndSweepReviewRespectOwnershipAndSelection` (automatic/manual, external/self-send). Core's independent RPC policy oracle: `bitcoin/test/functional/wallet_create_tx.py::test_create_too_long_mempool_chain` asserts rejection at the too-long unconfirmed-chain boundary; `bitcoin/test/functional/wallet_basic.py` also checks `-walletrejectlongchains` rejection and no wallet-recorded transaction. The R19 `oversizedMaximumPreservesCoreCauseAndRecoversWithFewerCoins` and maximum-handover selectors are separate fee/size and recipient semantics checks, not chain-limit evidence. |
| D310 | Fee/review projection concern. The scope record asks for confirmation against the merged head; R19 does not change the fee algorithm. Keep the authoritative review-fee integration rows and report their actual result. | `SendIntegrationTests.feePreviewLimitationKeepsCoreReviewAuthoritative` (2 rows) and `explicitPublicChangePolicyMatchesThePreview` (data rows). |

## O06-O09 and state-machine contract

- O06 captured intent and validation before admission:
  `preparationStatusHandoffKeepsCapturedDraft`,
  `preparationAdmissionKeepsOriginalEffect`, and
  `pendingAmountIntentCannotPrepareTheEarlierValidDraft`.
- O07 stale completion cannot replace a successor or expose sending of an old
  review: `heldPrepareCompletionCannotInstallEditedDraft` (2 rows),
  `editedDraftCannotSendPreviouslyReviewedTransaction`, and
  `SendCreate::test_send_button_requires_the_current_draft_review`.
- O08 display-unit changes preserve the immutable review while recipient and
  fee/input intent changes invalidate it:
  `editedDraftCannotSendPreviouslyReviewedTransaction` and
  `displayOnlyChangesDoNotInvalidateCandidate`; existing fee and selection
  rows remain required.
- O09 a failed reprepare retains the exact previous review:
  `preparationFailureRetainsTheExactCurrentReview`.
- The typed reducer tests direct deterministic decisions. The model tests
  provide held Core/Domain/GUI boundaries; the Core/RPC integration selector
  is independent of the reducer. Existing real-thread tests in
  `WalletPrepareTests` and `WalletQmlModelTests` remain required and are not
  replaced by the manual scheduler.

## Validation and remaining qualification

- `evidence/owner-domain-focused-13/actual/report.json`: six nonempty
  selections, 32/32 bodies passed, zero failures or skips. This includes the
  manual owner/model timing rows, real-thread preparation row, Core-backed
  maximum preparation, canonical-output classification, stale-review QML and
  public Core/RPC policy selectors.
- `evidence/owner-domain-focused-qml-lint-01/`: the same 32 bodies passed and
  the app `qml_lint` target exited 0.
- `evidence/owner-original-scopes-focused-23/actual/report.json`: nine
  nonempty original-scope selections, ten bodies passed, zero failures/skips.
  This includes the Core-authoritative fee review, uppercase Bech32 output,
  both maximum-handover data rows, Core maximum failure/recovery, and all four
  automatic/manual external/self-send maximum and sweep cases.
- `evidence/owner-parent-current-red-26/actual/report.json`: all three
  strict-parent controls ran on the exact integrated base and failed at their
  intended assertions: canonical recipient output was classified as change;
  sending a review after draft edit remained pending instead of failing; and
  the stale-review send button remained visible. Candidate counterparts pass
  in `owner-domain-focused-13` and the original-scope run. The earlier
  `owner-parent-red-focused-25` repeats the same three intended REDs on the
  pre-rebase R19 parent.
- The three rows that failed on the earlier fixture run
  (`owner-original-scopes-focused-15`) now pass on the final candidate. The
  maximum-handover setup had asserted whole-recipient validity while its
  second recipient intentionally had no amount yet; it now waits only for
  address normalization. The uppercase test now moves focus from the editor,
  then waits for recipient validation and fee/policy readiness before clicking.
  It retains the original review and output-ownership assertions.
- `evidence/owner-clang-tidy-04/tidy/results.json`: pinned LLVM/Clang-Tidy
  23.1.6 and Bitcoin plugin checked the changed `test_send.cpp` plus all seven
  selected app MOC aggregates; 8/8 passed. The preceding R19 production/test
  candidate check `owner-clang-tidy-03` passed 12/12, including the owner and
  model translation units.
- `evidence/owner-lint-app-03/exit.txt`: app scope of the pinned Core lint
  adapter passed for `HEAD^..HEAD` at `84b985e9b1d`; the layout and Core lint
  gate probes also passed.
- The uppercase UI test was added by R19 and is not itself present in the
  strict parent; the parent control is the corresponding lower-level
  canonical-output classifier test, which does fail on the parent and pass on
  the candidate. The maximum-handover test failures were fixture preconditions,
  not production regressions; after correcting those waits, both original
  handover output/amount assertions pass.
- RC80's original allegation is refuted by the current pinned call graph, not
  by the oversize or maximum-handover tests: `walletprepare.cpp:53,96` applies
  the requested input mode and dispatches preparation; `wallettransaction.cpp:35-64`
  routes a max remainder to the same public wallet builder; `wallet/interfaces.cpp:272-279`
  forwards that call into `CreateTransaction`; and `wallet/spend.cpp:1430-1435`
  runs `checkChainLimits` before success (unless the existing debug override
  `-walletrejectlongchains=0` is explicitly selected). `wallet/spend.cpp`
  also applies ancestor/cluster eligibility during automatic selection. The
  pinned Core functional test `wallet_create_tx.py::test_create_too_long_mempool_chain`
  supplies an independent RPC rejection oracle for the ordinary builder. No
  app test in this R19 evidence lowered cluster limits while exercising both
  maximum modes; that specific end-to-end combination is not claimed.
- Keep draft-307-RC11 in R20: it concerns how a transaction already prepared
  and recorded is reported when broadcast is disabled or rejected. It does not
  change the preparation-time chain-limit proof above.
- Sanitizers, exhaustive coverage, and platform/configuration matrices remain
  deferred to R42. No whole-Core or whole-application sanitizer claim is made.
