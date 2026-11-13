# Worker refactoring work orders

Read the [execution and acceptance plan](../QT6_TOTAL_REFACTOR_PLAN.md) before starting. These are planned tasks; all test selectors must be discovered/implemented and recorded. Default execution follows numerical order. Dependencies are minimum prerequisites, not authorization to edit another worker’s files.

## R00 — Establish reproducible baseline and test inventory

Prerequisites: -. Contracts: -. Work packages: -.

Pin PR1090 and Core; initialize exact submodule; inventory all test selectors and failures; verify CI adapters and coverage toolchain.

**Specific acceptance:** Reproduce existing tests in wallet-enabled and wallet-disabled modes; retain logs and nonempty selection counts; FSM review: Use the earlier FSM inventory as a preservation checklist; distinguish its older source/test baseline from PR1090 and inventory actual selectors..

**Commit recipe:** Inventory and reproduce the baseline; change no application behavior. Record test/coverage/CI results and blockers. Do not invent a defect-fix commit for this qualification task.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** Infrastructure/qualification task; its specific acceptance still requires executable tests..

**Cross-consumer follow-up:** None assigned..

## R01 — Injectable execution seam with first real reader consumer

Prerequisites: R00. Contracts: M08. Work packages: -.

Extend existing executor minimally for domain completion; immutable input/result and owned task ledger; inject manual scheduler into fee reader while preserving public API.

**Specific acceptance:** Production/manual executor contract parity; dispatch rejection; non-inline completion; old result; real-thread affinity; FSM review: Keep domain decisions deterministic with typed state/events and explicit effect intents; inject clocks/IDs/outcomes. Introduce no generic FSM framework without a concrete need.; Foundation: Implement typed deterministic decision/effect seams; test new event rejection/exhaustive handling with production logic. Keep dependencies injected and no service locator..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** Infrastructure/qualification task; its specific acceptance still requires executable tests..

**Cross-consumer follow-up:** None assigned..

## R02 — Reject substituted signer transactions

Prerequisites: R00. Contracts: M05. Work packages: -.

Fix exact unsigned commitment comparison in current signing path before architecture migration.

**Specific acceptance:** Draft88 altered inputs/outputs/sequences/locktime; valid partial signatures; signer subprocess.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-088-RC01.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-088-RC01 | A synthetic signer changing an input, destination, amount, sequence or locktime is rejected before installation/submission; exact matching and partial signature returns remain usable; check the approved replacement conflict scope. Synthetic compromised/faulty signer; ordinary Send and Speed Up remain separate acceptance cases. | test/integration/signers/test_watchonly.cpp |

## R03 — Authenticate PSBT fee inputs

Prerequisites: R02. Contracts: M09. Work packages: -.

Validate input metadata and exact fee provenance in current analysis/review path.

**Specific acceptance:** Draft87 forged prevouts; checked amounts; unknown fee disables protected actions; real PSBT integration.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-087-RC02.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-087-RC02 | Forged legacy prevout metadata cannot make review approve an understated fee; authenticate referenced transactions/amounts and compare decoded fee to the approved fee.  | test/integration/psbt/test_psbt.cpp |

## R04 — Correct backup recovery disclosure

Prerequisites: R00. Contracts: M32. Work packages: -.

Preserve truthful encryption/backup recovery coverage and fresh-backup action.

**Specific acceptance:** Draft35 user-visible disclosure plus controlled backup/restore; no fabricated lost-funds premise.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-035-RC03.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-035-RC03 | Explain changed backup coverage; verify restored older-backup coverage with controlled wallets and preserve the live wallet; never infer live funds loss from missing descriptors alone.  | test/integration/wallet_management/test_wallet.cpp |

## R05 — Validate final backup destination

Prerequisites: R04. Contracts: M24. Work packages: -.

Normalize suffix/path before existence and overwrite permission; backend write enforces policy.

**Specific acceptance:** Draft294 refusal preserves files byte-for-byte; existing/new destination; native chooser boundary.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-294-RC04.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-294-RC04 | Finalize extension/path before overwrite confirmation; refusal preserves sentinel bytes at both selected and rewritten paths; write only to the approved final destination. Tested fallback chooser; distinct from modern-wallet suffix stripping on restore (317). | test/integration/wallet_management/test_wallet.cpp |

## R06 — Remove RPC secret retention

Prerequisites: R00. Contracts: M22. Work packages: -.

Redact command arguments before history/output/diagnostic storage with maintained policy.

**Specific acceptance:** Draft92 command/argument matrix; execution still receives intended command; console and process checks.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-092-RC05.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-092-RC05 | Secret-bearing command arguments are absent from output rows, recall and diagnostics; ordinary arguments and safe redacted recall still work. Wallet-sensitive fixtures require wallet support beyond node-only Chunk 3; no storing secrets to repair 205. | test/integration/node_tools/test_console.cpp |

## R07 — Exact shared amount entry

Prerequisites: R00. Contracts: M30. Work packages: -.

Introduce canonical exact parser and typed amount entry with Receive and Send consumers.

**Specific acceptance:** Draft295 locales/units/paste/overflow/incomplete input; no JS floating point; preserve explicit modes.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-295-RC15.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-295-RC15 | Localized input 0,02 BTC represents exactly 2000000 satoshis or is visibly rejected; typing, paste and unit changes never reinterpret it as 2 BTC. Land shared amount-input behavior with its first Receive consumer; retain Send reproduction in Chunk 6. | test/integration/wallet_activity/test_receive.cpp |

## R08 — Background ownership and lifecycle foundation

Prerequisites: R01. Contracts: M01,M19,M20. Work packages: -.

Add keyed bounded effect lanes, reserved cleanup/control, owned outcomes, asynchronous drain and handler ingress with one wallet lifecycle consumer.

**Specific acceptance:** O01-O05,O20,O23-O25; callbacks during registration; observer loss; shutdown while blocked; real IPC lifecycle; FSM review: Record real event producers, thread/ownership/coalescing/teardown and pending-state progress assumptions; test rejection, producer loss and cleanup under saturation.; Foundation: Complete event/effect/pending-state producer records; bound queues and preserve outcome/cleanup capacity; replay synthetic schedules with payload-safe traces..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** Infrastructure/qualification task; its specific acceptance still requires executable tests..

**Cross-consumer follow-up:** None assigned..

## R09 — Shared backend guarantees

Prerequisites: R08,R03. Contracts: M03,M04,M05,M12. Work packages: -.

Audit and minimally implement exact candidate final validation/outcomes, unload admission, conditional writes and authorization coordination; split device wait from wallet locks.

**Specific acceptance:** Independent RPC races; backend lock-order and authorization tests; minimal recorded Core revision when change needed; FSM review: Specify narrow backend consistency groups and coherent capture/version-check protocols; GUI snapshot assembly alone does not provide backend coherence.; Foundation: Test unavailable backend capability and narrow coherent snapshots; define in-process versus restart idempotency/reconciliation limits explicitly..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** Infrastructure/qualification task; its specific acceptance still requires executable tests..

**Cross-consumer follow-up:** None assigned..

## R10 — Extract wallet opening/session lifetime

Prerequisites: R09. Contracts: M01. Work packages: WP4.1.

Introduce new session/facade per opening; close consent then fence/drain; migrate original callback ownership off GUI.

**Specific acceptance:** Open/close/reopen; old facade/prompt/results; Activity and Send lifetime consumers; O04,O27; Foundation: Test two views of one opening with shared domain state and separate view interest; old opening tokens cannot resolve to reopened objects..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-033-RC59, draft-034-RC59, draft-141-RC40, draft-143-RC40, draft-144-RC40, draft-166-RC59, draft-235-D235, draft-236-D236.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-033-RC59 | Bound/coalesce work and commit generation-matching deltas without starving progress. Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. 34 is PR-specific and debug-vs-optimized timings are not comparable; 33 delay is measured, 166 source-only. | test/integration/wallet_activity/test_activity.cpp |
| draft-034-RC59 | Bound/coalesce work and commit generation-matching deltas without starving progress. Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. 34 is PR-specific and debug-vs-optimized timings are not comparable; 33 delay is measured, 166 source-only. | test/integration/wallet_management/test_wallet.cpp |
| draft-141-RC40 | Pin action/session identity, close/invalidate prompts on session change and clear transient credentials. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback. 141 is only the residual password target: the review itself is pinned. Prove an actual wrong action before inferring funds loss. | test/integration/wallet_sending/test_send.cpp |
| draft-143-RC40 | Pin action/session identity, close/invalidate prompts on session change and clear transient credentials. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback. 141 is only the residual password target: the review itself is pinned. Prove an actual wrong action before inferring funds loss. | test/integration/wallet_management/test_wallet.cpp |
| draft-144-RC40 | Pin action/session identity, close/invalidate prompts on session change and clear transient credentials. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback. 141 is only the residual password target: the review itself is pinned. Prove an actual wrong action before inferring funds loss. | test/integration/wallet_sending/test_send.cpp |
| draft-166-RC59 | Bound/coalesce work and commit generation-matching deltas without starving progress. Specify cancellable/failed/retry/backoff/admission outcomes for the operation and implement bounded progress/cancellation effects. A machine cannot forcibly cancel an uncancellable dependency call. 34 is PR-specific and debug-vs-optimized timings are not comparable; 33 delay is measured, 166 source-only. | test/integration/wallet_activity/test_activity.cpp |
| draft-235-D235 | No active standalone fix priority for this stated scope Historical shutdown-order premise needs rechecking against merged shutdown coordinator; not an established current failure. Historical shutdown-order premise needs rechecking against merged shutdown coordinator; not an established current failure. | test/integration/wallet_management/test_wallet.cpp |
| draft-236-D236 | No active standalone fix priority for this stated scope Historical immediate-teardown premise superseded by merged walletsDrained coordinator; not an established current failure. Historical immediate-teardown premise superseded by merged walletsDrained coordinator; not an established current failure. | test/integration/wallet_management/test_wallet.cpp |

## R11 — Split catalog, registry and selection from controller

Prerequisites: R10. Contracts: M31. Work packages: WP4.2.

Registry deduplicates backend lifetime; catalog owns discovery; window selection independent; controller delegates one writer.

**Specific acceptance:** Load callback/result order; load A select B; duplicate discovery; exact facade selection; O18.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-049-RC38, draft-064-D064, draft-065-D065, draft-075-RC38, draft-129-D129, draft-130-D130, draft-131-D131, draft-133-D133, draft-134-D134, draft-135-D135, draft-137-RC39, draft-281-D281, draft-284-RC39, draft-316-D316.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-049-RC38 | Deduplicate by loader/session identity, subscribe before publication and reconcile completion against availability. An authoritative wallet operation/session owns admission, completion, catalog publication and selection; a form edit or stale completion cannot end or retarget it. 49 false-failure/open-behind-dialog and 75 detached phantom need separate tests. | test/integration/wallet_management/test_wallet.cpp |
| draft-064-D064 | No active standalone fix priority for this stated scope Fixed: last-wallet routing passed merged-head retest. Fixed: last-wallet routing passed merged-head retest. | test/integration/wallet_management/test_wallet.cpp |
| draft-065-D065 | Retest affected current scope, then fix or follow existing issue Use canonical wallet identity, isolated encoded settings namespaces and distinguishable names. This is identity/serialization validation within current flows, not a temporal state-machine defect. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_management/test_wallet.cpp |
| draft-075-RC38 | Deduplicate by loader/session identity, subscribe before publication and reconcile completion against availability. An authoritative wallet operation/session owns admission, completion, catalog publication and selection; a form edit or stale completion cannot end or retarget it. 49 false-failure/open-behind-dialog and 75 detached phantom need separate tests. | test/integration/wallet_management/test_wallet.cpp |
| draft-129-D129 | Verify reachable user-visible impact before promoting An authoritative wallet operation/session owns admission, completion, catalog publication and selection; a form edit or stale completion cannot end or retarget it. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_management/test_wallet.cpp |
| draft-130-D130 | Verify reachable user-visible impact before promoting Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_management/test_wallet.cpp |
| draft-131-D131 | Verify reachable user-visible impact before promoting An authoritative wallet operation/session owns admission, completion, catalog publication and selection; a form edit or stale completion cannot end or retarget it. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_management/test_wallet.cpp |
| draft-133-D133 | Verify reachable user-visible impact before promoting Specify cancellable/failed/retry/backoff/admission outcomes for the operation and implement bounded progress/cancellation effects. A machine cannot forcibly cancel an uncancellable dependency call. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_management/test_wallet.cpp |
| draft-134-D134 | Verify reachable user-visible impact before promoting Clarify the intended UX/command/selection/capability contract first. The recorded observation does not by itself establish that another workflow machine is needed. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_management/test_wallet.cpp |
| draft-135-D135 | Verify reachable user-visible impact before promoting Specify cancellable/failed/retry/backoff/admission outcomes for the operation and implement bounded progress/cancellation effects. A machine cannot forcibly cancel an uncancellable dependency call. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_management/test_wallet.cpp |
| draft-137-RC39 | Publish operation result and guard transition coherently before accepting another action. An authoritative wallet operation/session owns admission, completion, catalog publication and selection; a form edit or stale completion cannot end or retarget it.  | test/integration/wallet_management/test_wallet.cpp |
| draft-281-D281 | Verify reachable user-visible impact before promoting An authoritative wallet operation/session owns admission, completion, catalog publication and selection; a form edit or stale completion cannot end or retarget it. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_management/test_wallet.cpp |
| draft-284-RC39 | Publish operation result and guard transition coherently before accepting another action. An authoritative wallet operation/session owns admission, completion, catalog publication and selection; a form edit or stale completion cannot end or retarget it.  | test/integration/wallet_management/test_wallet.cpp |
| draft-316-D316 | Fix or follow existing issue using recorded reproduction; confirm current head Validate the actual wallet/model/signing capability or supported descriptor shape at the existing entry/projection boundary, rather than inferring it from lock state or one broad flag. Merged-head reproduction recorded in document; observe latest narrowed scope | test/integration/wallet_management/test_wallet.cpp |

## R12 — Extract creation and publication operation

Prerequisites: R11. Contracts: M02. Work packages: WP4.3,WP8.3.

Creation form produces operation; pending through setup/publication; preserve created artifacts on partial failure.

**Specific acceptance:** Names/paths/keys; watch-only setup failure; failed publication; original result identity; no duplicate create.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-006-RC37, draft-047-D047, draft-050-RC36, draft-055-RC71, draft-132-RC37, draft-315-RC36.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-006-RC37 | Lifecycle executor owns operation identity/busy state; form edits cannot end a running operation. An authoritative wallet operation/session owns admission, completion, catalog publication and selection; a form edit or stale completion cannot end or retarget it. Duplicate operation or data loss was not demonstrated. | test/integration/wallet_management/test_wallet.cpp |
| draft-047-D047 | Track existing issue/PR; validate remaining scope and merged status; do not duplicate Repair the dependency/backend behavior or cleanup/construction contract; preserve the distinct GUI warning scope in draft 55. Adding GUI phases does not fix the backend mechanism. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/signers/test_watchonly.cpp |
| draft-050-RC36 | Use canonical backend identity and validate distinct display names across create, discovery and import. Use canonical wallet identity, isolated encoded settings namespaces and distinguishable names. This is identity/serialization validation within current flows, not a temporal state-machine defect. Do not call these exact duplicates: 50 is a single wallet listed twice, 315 is distinct keys behind identical labels. | test/integration/wallet_management/test_wallet.cpp |
| draft-055-RC71 | Present warnings in the initiating create/open/restore result without claiming operation failure. Repair the dependency/backend behavior or cleanup/construction contract; preserve the distinct GUI warning scope in draft 55. Adding GUI phases does not fix the backend mechanism. Published warning scope remains separate from the Core settings/cleanup defect. | test/integration/wallet_management/test_wallet.cpp |
| draft-132-RC37 | Lifecycle executor owns operation identity/busy state; form edits cannot end a running operation. An authoritative wallet operation/session owns admission, completion, catalog publication and selection; a form edit or stale completion cannot end or retarget it. Duplicate operation or data loss was not demonstrated. | test/integration/wallet_management/test_wallet.cpp |
| draft-315-RC36 | Use canonical backend identity and validate distinct display names across create, discovery and import. Validate or canonicalize the actual value/identity/path used by the operation, including final-path overwrite confirmation, rather than relying on an earlier display or chooser check. Do not call these exact duplicates: 50 is a single wallet listed twice, 315 is distinct keys behind identical labels. | test/integration/wallet_management/test_wallet.cpp |

## R13 — Extract restore and migration operation adapters

Prerequisites: R12. Contracts: M02. Work packages: WP4.4.

Capture original source/target and prompt; reconcile partial outcomes; preserve compatibility data.

**Specific acceptance:** Invalid backup; plain/encrypted migration; source preservation; old binaries and Qt Widgets compatibility; Foundation: Preserve persisted compatibility; format changes need separate versioned migration and interruption fixtures; never persist transient authorization/tokens..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-024-D024, draft-055-RC70, draft-082-D082, draft-228-D228, draft-317-D317.

**Cross-consumer follow-up:** draft-055-RC71 (fix R12), draft-315-RC36 (fix R12).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-024-D024 | Retest affected current scope, then fix or follow existing issue Preserve and present typed failure reasons/capabilities; correcting error translation does not require adding a state machine. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_management/test_wallet.cpp |
| draft-055-RC70 | Treat late persistence error as warning and do not roll back a published successful wallet by deleting its live file. Repair the dependency/backend behavior or cleanup/construction contract; preserve the distinct GUI warning scope in draft 55. Adding GUI phases does not fix the backend mechanism. Original backup remains intact; do not conflate this with hidden warning presentation. | test/integration/wallet_management/test_wallet.cpp |
| draft-082-D082 | Retest affected current scope, then fix or follow existing issue Preserve and present typed failure reasons/capabilities; correcting error translation does not require adding a state machine. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_management/test_wallet.cpp |
| draft-228-D228 | Verify reachable user-visible impact before promoting Validate or canonicalize the actual value/identity/path used by the operation, including final-path overwrite confirmation, rather than relying on an earlier display or chooser check. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_management/test_wallet.cpp |
| draft-317-D317 | Fix or follow existing issue using recorded reproduction; confirm current head Validate or canonicalize the actual value/identity/path used by the operation, including final-path overwrite confirmation, rather than relying on an earlier display or chooser check. Merged-head reproduction recorded in document; observe latest narrowed scope | test/integration/wallet_management/test_wallet.cpp |

## R14 — Scoped authorization, security and storage

Prerequisites: R09,R10,R04,R05. Contracts: M03,M24,M32. Work packages: WP4.5.

Separate observed security from operation/lease; migrate password users; shared file-write protocol with Backup consumer.

**Specific acceptance:** RPC unlock/relock interleavings; credential rejection; late outcome; backup path/coverage; O13,O33.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-016-RC34, draft-018-D018, draft-019-D019, draft-021-RC34, draft-136-RC35, draft-146-D146, draft-152-D152, draft-225-D225, draft-229-RC34, draft-231-D231, draft-232-D232, draft-261-RC31, draft-282-RC35, draft-309-RC31.

**Cross-consumer follow-up:** draft-035-RC03 (fix R04), draft-294-RC04 (fix R05).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-016-RC34 | Introduce the shared guarded chooser with the backup consumer, then add PSBT overlap/selection tests when Chunk7 arrives. Give dialogs/modal actions explicit ownership and open/close/reentrancy/admission rules; reject stale acceptance and define cancellation outcomes. Keyboard blocking and disabled Open remain separate macOS acceptance cases; only PSBT sequences were reproduced. Shared-helper ownership does not establish a backup defect or all-platform reproduction. | test/integration/psbt/test_psbt.cpp |
| draft-018-D018 | Track existing issue/PR; validate remaining scope and merged status; do not duplicate Validate or canonicalize the actual value/identity/path used by the operation, including final-path overwrite confirmation, rather than relying on an earlier display or chooser check. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_management/test_wallet.cpp |
| draft-019-D019 | No active standalone fix priority for this stated scope Obsolete PR-dependent proposal: underlying backup-confirmation PR was closed; no current merged-app issue established. Obsolete PR-dependent proposal: underlying backup-confirmation PR was closed; no current merged-app issue established. | test/integration/wallet_management/test_wallet.cpp |
| draft-021-RC34 | Introduce the shared guarded chooser with the backup consumer, then add PSBT overlap/selection tests when Chunk7 arrives. Give dialogs/modal actions explicit ownership and open/close/reentrancy/admission rules; reject stale acceptance and define cancellation outcomes. Keyboard blocking and disabled Open remain separate macOS acceptance cases; only PSBT sequences were reproduced. Shared-helper ownership does not establish a backup defect or all-platform reproduction. | test/integration/psbt/test_psbt.cpp |
| draft-136-RC35 | Bind file operation to original available session and reject/drain acceptance during teardown. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback. Source race fails closed in the documented common path; no wrong-wallet backup established. | test/integration/wallet_management/test_wallet.cpp |
| draft-146-D146 | No active standalone fix priority for this stated scope Refuted on current merged head: wrong current password locks wallet and QML correctly updates. Refuted on current merged head: wrong current password locks wallet and QML correctly updates. | test/integration/wallet_management/test_wallet.cpp |
| draft-152-D152 | Verify reachable user-visible impact before promoting Refresh authoritative security state on every operation outcome, but reconcile this source-only sibling against draft 146's refuted stale-lock claim before alleging a current bug. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_management/test_wallet.cpp |
| draft-225-D225 | Verify reachable user-visible impact before promoting Validate or canonicalize the actual value/identity/path used by the operation, including final-path overwrite confirmation, rather than relying on an earlier display or chooser check. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_management/test_wallet.cpp |
| draft-229-RC34 | Introduce the shared guarded chooser with the backup consumer, then add PSBT overlap/selection tests when Chunk7 arrives. Give dialogs/modal actions explicit ownership and open/close/reentrancy/admission rules; reject stale acceptance and define cancellation outcomes. Keyboard blocking and disabled Open remain separate macOS acceptance cases; only PSBT sequences were reproduced. Shared-helper ownership does not establish a backup defect or all-platform reproduction. | test/integration/psbt/test_psbt.cpp |
| draft-231-D231 | Verify reachable user-visible impact before promoting Validate or canonicalize the actual value/identity/path used by the operation, including final-path overwrite confirmation, rather than relying on an earlier display or chooser check. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_management/test_wallet.cpp |
| draft-232-D232 | Verify reachable user-visible impact before promoting Give dialogs/modal actions explicit ownership and open/close/reentrancy/admission rules; reject stale acceptance and define cancellation outcomes. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_management/test_wallet.cpp |
| draft-261-RC31 | Encode canonical wallet identity as one key component and isolate/remove exact entries only. Use canonical wallet identity, isolated encoded settings namespaces and distinguishable names. This is identity/serialization validation within current flows, not a temporal state-machine defect. 309 proves reservation deletion. 261 also includes an independent unchecked-write-status concern; alias corruption is not established by the reservation test. | test/integration/wallet_management/test_wallet.cpp |
| draft-282-RC35 | Bind file operation to original available session and reject/drain acceptance during teardown. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback. Source race fails closed in the documented common path; no wrong-wallet backup established. | test/integration/wallet_management/test_wallet.cpp |
| draft-309-RC31 | Encode canonical wallet identity as one key component and isolate/remove exact entries only. Use canonical wallet identity, isolated encoded settings namespaces and distinguishable names. This is identity/serialization validation within current flows, not a temporal state-machine defect. 309 proves reservation deletion. 261 also includes an independent unchecked-write-status concern; alias corruption is not established by the reservation test. | test/integration/wallet_activity/test_receive.cpp |

## R15 — Extract bounded signer discovery/configuration

Prerequisites: R12,R08. Contracts: M23. Work packages: WP8.2.

Separate edited/effective config and bounded process discovery; stable device identity; no enumeration in getters.

**Specific acceptance:** Configuration change during discovery; timeout/rejection; stale device result; external signer process.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-062-RC64, draft-086-RC64, draft-156-RC64, draft-157-RC64, draft-158-RC64, draft-159-RC64, draft-160-RC64, draft-313-RC62.

**Cross-consumer follow-up:** draft-155-RC55 (fix R33).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-062-RC64 | Honor effective configuration and preserve typed discovery/device errors; refresh state independently of selected wallet. Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. Group for coordinated review, not an assertion that path precedence, validation and cached device status are one literal bug. | test/integration/signers/test_watchonly.cpp |
| draft-086-RC64 | Honor effective configuration and preserve typed discovery/device errors; refresh state independently of selected wallet. Preserve and present typed failure reasons/capabilities; correcting error translation does not require adding a state machine. Group for coordinated review, not an assertion that path precedence, validation and cached device status are one literal bug. | test/integration/signers/test_watchonly.cpp |
| draft-156-RC64 | Honor effective configuration and preserve typed discovery/device errors; refresh state independently of selected wallet. Separate edited path, persisted/effective configuration, pending check and generation-tagged discovery result; invalidate stale status when the configuration or device context changes. Group for coordinated review, not an assertion that path precedence, validation and cached device status are one literal bug. | test/integration/signers/test_watchonly.cpp |
| draft-157-RC64 | Honor effective configuration and preserve typed discovery/device errors; refresh state independently of selected wallet. Separate edited path, persisted/effective configuration, pending check and generation-tagged discovery result; invalidate stale status when the configuration or device context changes. Group for coordinated review, not an assertion that path precedence, validation and cached device status are one literal bug. | test/integration/signers/test_watchonly.cpp |
| draft-158-RC64 | Honor effective configuration and preserve typed discovery/device errors; refresh state independently of selected wallet. Separate edited path, persisted/effective configuration, pending check and generation-tagged discovery result; invalidate stale status when the configuration or device context changes. Group for coordinated review, not an assertion that path precedence, validation and cached device status are one literal bug. | test/integration/signers/test_watchonly.cpp |
| draft-159-RC64 | Honor effective configuration and preserve typed discovery/device errors; refresh state independently of selected wallet. Preserve and present typed failure reasons/capabilities; correcting error translation does not require adding a state machine. Group for coordinated review, not an assertion that path precedence, validation and cached device status are one literal bug. | test/integration/signers/test_watchonly.cpp |
| draft-160-RC64 | Honor effective configuration and preserve typed discovery/device errors; refresh state independently of selected wallet. Validate the effective signer path at the model boundary, not only in the QML form. Group for coordinated review, not an assertion that path precedence, validation and cached device status are one literal bug. | test/integration/signers/test_watchonly.cpp |
| draft-313-RC62 | Run bounded discovery asynchronously and keep unrelated software-wallet selection responsive. Move blocking work to a bounded asynchronous execution path. A busy/discovery state does not make a synchronous GUI-thread call responsive. Around 2.5-second delay measured; permanent hangs and physical hardware were not tested. | test/integration/signers/test_watchonly.cpp |

## R16 — Extract revisioned Send draft

Prerequisites: R07,R10. Contracts: M06,M30. Work packages: WP6.1.

Move recipients/Fixed-Maximum/provenance to domain draft; typed QML fields; no implicit Save protocol.

**Specific acceptance:** Same-value semantic mode change; empty/invalid recipient; bounded counts; imported payment consumer; O06,O10.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-001-RC16, draft-031-RC16, draft-045-D045, draft-094-D094, draft-099-RC68, draft-099-RC76, draft-270-D270.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-001-RC16 | Represent fixed/max intent explicitly and update it even when the numeric amount is unchanged. Represent explicit Fixed/Maximum and Automatic/Manual intent; value equality or reselection must not replace an intentional mode transition. 1 is payment-request import in Chunk 9; 31 is ordinary Send. Preserve their separate application entry points and the hold on 1. | test/integration/payment_import/test_payment_import.cpp |
| draft-031-RC16 | Represent fixed/max intent explicitly and update it even when the numeric amount is unchanged. Represent explicit Fixed/Maximum and Automatic/Manual intent; value equality or reselection must not replace an intentional mode transition. 1 is payment-request import in Chunk 9; 31 is ordinary Send. Preserve their separate application entry points and the hold on 1. | test/integration/wallet_sending/test_send.cpp |
| draft-045-D045 | Retest affected current scope, then fix or follow existing issue Represent explicit Fixed/Maximum and Automatic/Manual intent; value equality or reselection must not replace an intentional mode transition. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_sending/test_send.cpp |
| draft-094-D094 | Retest affected current scope, then fix or follow existing issue Use exact canonical values and consistent parsing/unit/checked-arithmetic rules in the existing flow; rendered text must not silently change amount meaning. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_sending/test_send.cpp |
| draft-099-RC68 | Use per-component memory budgets/virtualization and avoid repeated copied payloads. Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. 99 is not one bug. Model/delegate memory was measured; no end-to-end OOM was attempted. 208 repeats console scope 99-F1. | test/integration/wallet_sending/test_send.cpp |
| draft-099-RC76 | Cap/render/page recipient rows and return a visible bounded result. Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. Component memory measurement; native end-to-end OOM deliberately not attempted. | test/integration/wallet_sending/test_send.cpp |
| draft-270-D270 | Verify reachable user-visible impact before promoting Use exact canonical values and consistent parsing/unit/checked-arithmetic rules in the existing flow; rendered text must not silently change amount meaning. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_sending/test_send.cpp |

## R17 — Extract fee selection and versioned estimate reader

Prerequisites: R16,R01. Contracts: M08. Work packages: WP6.2.

Explicit preset/custom intent; per-dependency revisions and provenance; off-GUI estimate.

**Specific acceptance:** Stale success/error; coalesced changes; fee units/fallback; approved review never repriced; Foundation: Exercise another versioned-reader consumer without another executor framework; maintain per-resource dependencies and typed errors..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-048-D048, draft-058-D058, draft-076-D076, draft-080-D080, draft-142-D142, draft-265-RC61, draft-296-RC22.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-048-D048 | Retest affected current scope, then fix or follow existing issue Use the same change-output sizing policy in preview and transaction construction; a state machine does not correct a mismatched fee-estimation calculation. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_sending/test_send.cpp |
| draft-058-D058 | Retest affected current scope, then fix or follow existing issue Check current spendability, bump eligibility, fee minima or preparation feasibility at the existing action boundary; a separate workflow machine is not required for this scope. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_sending/test_send.cpp |
| draft-076-D076 | Retest affected current scope, then fix or follow existing issue Check current spendability, bump eligibility, fee minima or preparation feasibility at the existing action boundary; a separate workflow machine is not required for this scope. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_sending/test_send.cpp |
| draft-080-D080 | Retest affected current scope, then fix or follow existing issue Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_sending/test_send.cpp |
| draft-142-D142 | No active standalone fix priority for this stated scope Sound timer/shared-ownership control; no defect established. Sound timer/shared-ownership control; no defect established. | test/integration/wallet_sending/test_send.cpp |
| draft-265-RC61 | Bound input and use checked exact fixed-point parsing. Use exact canonical values and consistent parsing/unit/checked-arithmetic rules in the existing flow; rendered text must not silently change amount meaning. Separate from recipient aggregate overflow 93/266 and decimal-comma loss 295. | test/integration/wallet_sending/test_send.cpp |
| draft-296-RC22 | Make mode reselection idempotent and separate target-derived defaults from explicit rates. Represent explicit Fixed/Maximum and Automatic/Manual intent; value equality or reselection must not replace an intentional mode transition.  | test/integration/wallet_sending/test_send.cpp |

## R18 — Extract temporary coin selection and lock operation

Prerequisites: R16,R08. Contracts: M07. Work packages: WP6.3.

Separate catalog/marks/committed policy and actual lock results; unknown outcome reconciliation.

**Specific acceptance:** Manual empty/missing outpoint; apply/cancel; partial locks; close during work; O14,O35.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-003-RC17, draft-009-D009, draft-011-RC19, draft-017-D017, draft-030-RC17, draft-032-D032, draft-038-D038, draft-217-RC17, draft-218-RC17, draft-219-RC18, draft-220-RC18, draft-221-RC19, draft-222-RC19, draft-223-RC18, draft-224-RC17, draft-267-D267.

**Cross-consumer follow-up:** draft-266-RC30 (fix R21).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-003-RC17 | Invalidate or explicitly reconcile Manual/Max policy instead of silently broadening selected input scope. An empty or temporarily unavailable manual selection must enter an explicit reconciliation state rather than silently broadening the input policy. 217 is a source-only wallet-switch variant; existing warnings/review show the resulting amount. | test/integration/wallet_sending/test_send.cpp |
| draft-009-D009 | Retest affected current scope, then fix or follow existing issue Apply the wallet's avoid-reuse input policy in the existing coin-control/preparation path; lifecycle states do not implement a missing input-selection policy. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_sending/test_send.cpp |
| draft-011-RC19 | Reconcile marks against live coins and preserve unaffected selection on failure. Validate selected outpoints and lock actions against authoritative current coins; cached rows/marks are not an action precondition. 222 is a source rollback-window case; 11 did not act on a wrong live output. | test/integration/wallet_sending/test_send.cpp |
| draft-017-D017 | Retest affected current scope, then fix or follow existing issue Check current spendability, bump eligibility, fee minima or preparation feasibility at the existing action boundary; a separate workflow machine is not required for this scope. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_sending/test_send.cpp |
| draft-030-RC17 | Invalidate or explicitly reconcile Manual/Max policy instead of silently broadening selected input scope. An empty or temporarily unavailable manual selection must enter an explicit reconciliation state rather than silently broadening the input policy. 217 is a source-only wallet-switch variant; existing warnings/review show the resulting amount. | test/integration/wallet_sending/test_send.cpp |
| draft-032-D032 | Retest affected current scope, then fix or follow existing issue Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_sending/test_send.cpp |
| draft-038-D038 | Retest affected current scope, then fix or follow existing issue Repair the dependency/backend behavior or cleanup/construction contract; preserve the distinct GUI warning scope in draft 55. Adding GUI phases does not fix the backend mechanism. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_sending/test_send.cpp |
| draft-217-RC17 | Invalidate or explicitly reconcile Manual/Max policy instead of silently broadening selected input scope. Represent explicit Fixed/Maximum and Automatic/Manual intent; value equality or reselection must not replace an intentional mode transition. 217 is a source-only wallet-switch variant; existing warnings/review show the resulting amount. | test/integration/wallet_sending/test_send.cpp |
| draft-218-RC17 | Invalidate or explicitly reconcile Manual/Max policy instead of silently broadening selected input scope. Represent explicit Fixed/Maximum and Automatic/Manual intent; value equality or reselection must not replace an intentional mode transition. 217 is a source-only wallet-switch variant; existing warnings/review show the resulting amount. | test/integration/wallet_sending/test_send.cpp |
| draft-219-RC18 | Refresh/subscribe to authoritative lock state and reconcile selection before actions. Validate selected outpoints and lock actions against authoritative current coins; cached rows/marks are not an action precondition.  | test/integration/wallet_sending/test_send.cpp |
| draft-220-RC18 | Refresh/subscribe to authoritative lock state and reconcile selection before actions. Validate selected outpoints and lock actions against authoritative current coins; cached rows/marks are not an action precondition.  | test/integration/wallet_sending/test_send.cpp |
| draft-221-RC19 | Reconcile marks against live coins and preserve unaffected selection on failure. Preserve per-session selection on partial failure and validate each lock/unlock result against current coins; both edit-session disposition and per-item validation matter. 222 is a source rollback-window case; 11 did not act on a wrong live output. | test/integration/wallet_sending/test_send.cpp |
| draft-222-RC19 | Reconcile marks against live coins and preserve unaffected selection on failure. Validate selected outpoints and lock actions against authoritative current coins; cached rows/marks are not an action precondition. 222 is a source rollback-window case; 11 did not act on a wrong live output. | test/integration/wallet_sending/test_send.cpp |
| draft-223-RC18 | Refresh/subscribe to authoritative lock state and reconcile selection before actions. Repair exit-status/platform-preference/notification/readback wiring. A modeled transition cannot supply an observation or status that is never propagated.  | test/integration/wallet_sending/test_send.cpp |
| draft-224-RC17 | Invalidate or explicitly reconcile Manual/Max policy instead of silently broadening selected input scope. Represent explicit Fixed/Maximum and Automatic/Manual intent; value equality or reselection must not replace an intentional mode transition. 217 is a source-only wallet-switch variant; existing warnings/review show the resulting amount. | test/integration/wallet_sending/test_send.cpp |
| draft-267-D267 | Verify reachable user-visible impact before promoting Use exact canonical values and consistent parsing/unit/checked-arithmetic rules in the existing flow; rendered text must not silently change amount meaning. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_sending/test_send.cpp |

## R19 — Extract candidate preparation and immutable review

Prerequisites: R09,R16,R17,R18. Contracts: M04. Work packages: WP6.4.

Introduce attempt owner through Prepare/Review/Revalidate; temporary facade delegates; review owns exact terms.

**Specific acceptance:** Edit-before-prepare publication; stale candidate/prompt; relevant vs display dependency; O06-O09; FSM review: Run production decision logic in manual-executor tests; keep Core/IO/clock acquisition outside reduction and derive capabilities from authoritative state..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-015-D015, draft-020-RC24, draft-111-D111, draft-138-D138, draft-251-RC24, draft-257-D257, draft-307-RC80, draft-310-D310.

**Cross-consumer follow-up:** draft-141-RC40 (fix R10), draft-144-RC40 (fix R10), draft-307-RC11 (fix R20).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-015-D015 | Retest affected current scope, then fix or follow existing issue Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_sending/test_send.cpp |
| draft-020-RC24 | Compare destination scripts/canonical recipient identity and preserve explicit recipient provenance. Correct finalized transaction/destination identity and owned-recipient/change classification independently of action phase or signability. Correct destination scripts and values were retained in the documented uppercase case. | test/integration/wallet_sending/test_send.cpp |
| draft-111-D111 | Verify reachable user-visible impact before promoting Enforce a coherent operation object and allowed transitions, including atomic failure/reset, review invalidation, one commit in flight and terminal action capabilities. Existing enum/flag presence alone does not enforce these contracts. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_sending/test_send.cpp |
| draft-138-D138 | No active standalone fix priority for this stated scope Sound wallet-unload control; no defect established. Sound wallet-unload control; no defect established. | test/integration/wallet_sending/test_send.cpp |
| draft-251-RC24 | Compare destination scripts/canonical recipient identity and preserve explicit recipient provenance. Correct finalized transaction/destination identity and owned-recipient/change classification independently of action phase or signability. Correct destination scripts and values were retained in the documented uppercase case. | test/integration/wallet_sending/test_send.cpp |
| draft-257-D257 | Verify reachable user-visible impact before promoting Enforce a coherent operation object and allowed transitions, including atomic failure/reset, review invalidation, one commit in flight and terminal action capabilities. Existing enum/flag presence alone does not enforce these contracts. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_sending/test_send.cpp |
| draft-307-RC80 | Maximum preparation applies the relevant cluster policy; separately, a recorded-but-rejected transaction cannot be reported as accepted or broadcast. Keep the separate 6.5 acceptance/result fix from RC11. | test/integration/wallet_sending/test_send.cpp |
| draft-310-D310 | Fix or follow existing issue using recorded reproduction; confirm current head Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Merged-head reproduction recorded in document; observe latest narrowed scope | test/integration/wallet_sending/test_send.cpp |

## R20 — Extract transaction confirmation and submission

Prerequisites: R19,R14. Contracts: M04. Work packages: WP6.5.

Add exclusive confirm continuations; immutable exact submit; known/unknown truthful outcome ledger; retain successor draft.

**Specific acceptance:** O26,O28; duplicate confirm; sweep refusal; record vs mempool vs relay; lost result; one mutation.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-081-RC11, draft-256-D256, draft-307-RC11.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-081-RC11 | Model wallet-recording, local acceptance, propagation and rejection as separate results. Expose wallet-recorded, locally accepted/submitted, disabled-broadcast and rejected outcomes separately. The effect/backend API must return the real outcome; enum names do not repair a misleading boolean. 307 also has a separate Max-specific missing cluster preflight; preserve that scope separately. | test/integration/wallet_sending/test_send.cpp |
| draft-256-D256 | Verify reachable user-visible impact before promoting Enforce a coherent operation object and allowed transitions, including atomic failure/reset, review invalidation, one commit in flight and terminal action capabilities. Existing enum/flag presence alone does not enforce these contracts. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_sending/test_send.cpp |
| draft-307-RC11 | Maximum preparation applies the relevant cluster policy; separately, a recorded-but-rejected transaction cannot be reported as accepted or broadcast. 307 also has a separate Max-specific missing cluster preflight; preserve that scope separately. | test/integration/wallet_sending/test_send.cpp |

## R21 — Extract PSBT document analysis

Prerequisites: R03,R19. Contracts: M09. Work packages: WP7.1.

Immutable parsed document and separate analysis/eligibility facts; standalone inspection.

**Specific acceptance:** Structure/metadata/cardinality/fee validation; known-PSBT baseline overlap39; held95 premise; bounded parse.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-029-D029, draft-039-RC07, draft-093-RC30, draft-095-D095, draft-102-RC23, draft-104-RC27, draft-106-RC23, draft-250-RC25, draft-252-RC27, draft-266-RC30, draft-280-RC25.

**Cross-consumer follow-up:** draft-016-RC34 (fix R14), draft-021-RC34 (fix R14), draft-087-RC02 (fix R03), draft-107-RC20 (fix R26), draft-229-RC34 (fix R14).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-029-D029 | Retest affected current scope, then fix or follow existing issue Represent unknown fee distinctly from a numeric zero in presentation; the recorded warning and disabled-broadcast guard already exist. This is a value/projection correction, not another workflow phase. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/psbt/test_psbt.cpp |
| draft-039-RC07 | Identify the finalized transaction when checking known status. Correct finalized transaction/destination identity and owned-recipient/change classification independently of action phase or signability. Same GitHub report as 96/97 does not establish the same root cause. | test/integration/psbt/test_psbt.cpp |
| draft-093-RC30 | Validate amount ranges and use checked accumulation before downstream derived calculations. Validate money ranges and use checked aggregate arithmetic before review/coverage calculations; retain the documented unknown-fee action guard. Unknown-fee guard disabled Send/Broadcast in reproduced malformed cases; keep 265 rate-parser overflow separate. | test/integration/psbt/test_psbt.cpp |
| draft-095-D095 | Hold claim; reconcile evidence or demonstrate actual lifetime/impact before promotion Held validation candidate: reconcile actual signature commitments and reviewed outputs before treating the historical claim as established. Do not count it as a confirmed fix opportunity. HOLD pending sighash-byte/output-commitment reconciliation; do not repeat historical unconditional P1. | test/integration/psbt/test_psbt.cpp |
| draft-102-RC23 | Classify owned recipient/change outputs independently of signability; preserve self-transfer review. Correct finalized transaction/destination identity and owned-recipient/change classification independently of action phase or signability. Distinct from address-casing recipient recognition in 20/251. | test/integration/psbt/test_psbt.cpp |
| draft-104-RC27 | Commit a coherent operation object only after validation or clear the entire tuple atomically. Enforce a coherent operation object and allowed transitions, including atomic failure/reset, review invalidation, one commit in flight and terminal action capabilities. Existing enum/flag presence alone does not enforce these contracts. Document describes inert residue; no incorrect send established. | test/integration/psbt/test_psbt.cpp |
| draft-106-RC23 | Classify owned recipient/change outputs independently of signability; preserve self-transfer review. Correct finalized transaction/destination identity and owned-recipient/change classification independently of action phase or signability. Distinct from address-casing recipient recognition in 20/251. | test/integration/psbt/test_psbt.cpp |
| draft-250-RC25 | Use consistent immutable review/export metadata and make any refresh explicit. Build review and export from one versioned immutable snapshot; refresh explicitly rather than mixing retained transaction bytes with live metadata. Review and export surfaces differ; not evidence of an altered unsigned transaction. | test/integration/psbt/test_psbt.cpp |
| draft-252-RC27 | Commit a coherent operation object only after validation or clear the entire tuple atomically. Enforce a coherent operation object and allowed transitions, including atomic failure/reset, review invalidation, one commit in flight and terminal action capabilities. Existing enum/flag presence alone does not enforce these contracts. Document describes inert residue; no incorrect send established. | test/integration/psbt/test_psbt.cpp |
| draft-266-RC30 | Validate amount ranges and use checked accumulation before downstream derived calculations. Use exact canonical values and consistent parsing/unit/checked-arithmetic rules in the existing flow; rendered text must not silently change amount meaning. Unknown-fee guard disabled Send/Broadcast in reproduced malformed cases; keep 265 rate-parser overflow separate. | test/integration/wallet_sending/test_send.cpp |
| draft-280-RC25 | Use consistent immutable review/export metadata and make any refresh explicit. Build review and export from one versioned immutable snapshot; refresh explicitly rather than mixing retained transaction bytes with live metadata. Review and export surfaces differ; not evidence of an altered unsigned transaction. | test/integration/psbt/test_psbt.cpp |

## R22 — Local signing and immutable export

Prerequisites: R21,R14,R20. Contracts: M04,M24. Work packages: WP7.2.

SignOnly continuation; preserve partial signatures; export captured document/rows through shared file-write operation.

**Specific acceptance:** Partial/complete/foreign input capability; exact exported bytes; destination race; no accidental send.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-026-D026, draft-090-RC10, draft-308-D308.

**Cross-consumer follow-up:** draft-280-RC25 (fix R21).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-026-D026 | Retest affected current scope, then fix or follow existing issue Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/psbt/test_psbt.cpp |
| draft-090-RC10 | Validate transaction structure before signing or wallet commit. Reject structurally invalid duplicate-input transactions before signing or wallet commit. Shares publication with 93 but not its unchecked-total root cause. | test/integration/psbt/test_psbt.cpp |
| draft-308-D308 | Fix or follow existing issue using recorded reproduction; confirm current head Validate the actual wallet/model/signing capability or supported descriptor shape at the existing entry/projection boundary, rather than inferring it from lock state or one broad flag. Merged-head reproduction recorded in document; observe latest narrowed scope | test/integration/psbt/test_psbt.cpp |

## R23 — Final PSBT eligibility and result semantics

Prerequisites: R21,R20,R09. Contracts: M09,M04. Work packages: WP7.3.

Separate signature completeness from admissibility; final extraction uses analyzed revision.

**Specific acceptance:** Independent RPC spend; rejected/recorded/known transaction; refute or reproduce253; no false success.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-096-RC06, draft-097-RC06, draft-105-RC06, draft-108-RC28, draft-109-RC29, draft-110-RC26, draft-112-RC26, draft-253-RC28, draft-254-RC29, draft-304-D304.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-096-RC06 | Use finalized transaction and node state for readiness, disclose conflicts and report actual rejection. Add current node admissibility/conflict/finality/relay checks and accurate capability explanations within PSBT review. Complete and known-fee do not imply admissible; recheck at submission. Spent inputs, mempool replacement, nonfinal and below-relay cases are distinct acceptance cases, not identical reproductions. | test/integration/psbt/test_psbt.cpp |
| draft-097-RC06 | Use finalized transaction and node state for readiness, disclose conflicts and report actual rejection. Add current node admissibility/conflict/finality/relay checks and accurate capability explanations within PSBT review. Complete and known-fee do not imply admissible; recheck at submission. Spent inputs, mempool replacement, nonfinal and below-relay cases are distinct acceptance cases, not identical reproductions. | test/integration/psbt/test_psbt.cpp |
| draft-105-RC06 | Use finalized transaction and node state for readiness, disclose conflicts and report actual rejection. Add current node admissibility/conflict/finality/relay checks and accurate capability explanations within PSBT review. Complete and known-fee do not imply admissible; recheck at submission. Spent inputs, mempool replacement, nonfinal and below-relay cases are distinct acceptance cases, not identical reproductions. | test/integration/psbt/test_psbt.cpp |
| draft-108-RC28 | Verify the actual source assignment and clear terminal action state coherently. Enforce a coherent operation object and allowed transitions, including atomic failure/reset, review invalidation, one commit in flight and terminal action capabilities. Existing enum/flag presence alone does not enforce these contracts. 253 says source is unchanged while its own citation says source=None. Do not retain the stale-source assertion without reconciliation; normal UI already gates repeat action. | test/integration/psbt/test_psbt.cpp |
| draft-109-RC29 | Invalidate workflow state when the original session becomes unavailable. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback. Controller selection currently masks the path; source hardening is not a demonstrated wrong-wallet send. | test/integration/psbt/test_psbt.cpp |
| draft-110-RC26 | Store/tag the analyzed result and derive readiness/extraction from the same revision. Pin the same analyzed PSBT revision through capability publication and extraction, and validate successful final extraction; snapshot coherence and the result check are separate obligations. 112 requires actual divergence; verify reachability rather than inferring it from a flag alone. | test/integration/psbt/test_psbt.cpp |
| draft-112-RC26 | Store/tag the analyzed result and derive readiness/extraction from the same revision. Pin the same analyzed PSBT revision through capability publication and extraction, and validate successful final extraction; snapshot coherence and the result check are separate obligations. 112 requires actual divergence; verify reachability rather than inferring it from a flag alone. | test/integration/psbt/test_psbt.cpp |
| draft-253-RC28 | Verify the actual source assignment and clear terminal action state coherently. Enforce a coherent operation object and allowed transitions, including atomic failure/reset, review invalidation, one commit in flight and terminal action capabilities. Existing enum/flag presence alone does not enforce these contracts. 253 says source is unchanged while its own citation says source=None. Do not retain the stale-source assertion without reconciliation; normal UI already gates repeat action. | test/integration/psbt/test_psbt.cpp |
| draft-254-RC29 | Invalidate workflow state when the original session becomes unavailable. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback. Controller selection currently masks the path; source hardening is not a demonstrated wrong-wallet send. | test/integration/psbt/test_psbt.cpp |
| draft-304-D304 | Track existing issue/PR; validate remaining scope and merged status; do not duplicate Supply the correct rate and absolute limits at the existing broadcast API boundary; track the exact identified PR fix rather than redesigning the workflow. Merged-head reproduction recorded in document; observe latest narrowed scope | test/integration/psbt/test_psbt.cpp |

## R24 — Extract external signer approval

Prerequisites: R15,R19,R22,R09. Contracts: M05. Work packages: WP8.4.

Immutable bounded invocation, unlocked device wait, verify before merge, cancel/drain.

**Specific acceptance:** Altered terms; partial results; device config changes; timeout/close; preserve draft255 baseline behavior; O12,O28.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-089-RC63, draft-122-RC63, draft-154-D154, draft-161-RC41, draft-249-D249, draft-255-D255, draft-286-RC41.

**Cross-consumer follow-up:** draft-088-RC01 (fix R02).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-089-RC63 | Catch execution errors and complete the original action with a recoverable failure and restored controls. Catch and propagate the failing dependency/persistence operation. Draft 89's demonstrated abort is fixed at the exception boundary; draft 122 separately describes a hypothetical missing failure transition after a partial fix. 89 demonstrates abort; 122 describes a stuck state after a hypothetical partial fix, not another observed crash. | test/integration/signers/test_watchonly.cpp |
| draft-122-RC63 | Catch execution errors and complete the original action with a recoverable failure and restored controls. Source-only hypothetical after an exception-handling partial fix: ensure the action reaches a terminal/retry state. Do not treat it as another reproduced crash in addition to draft 89. 89 demonstrates abort; 122 describes a stuck state after a hypothetical partial fix, not another observed crash. | test/integration/signers/test_watchonly.cpp |
| draft-154-D154 | Verify reachable user-visible impact before promoting Preserve partial signing as an explicit recoverable review outcome independent of whether a Send button is visible; export already exists and remaining impact needs reproduction. Source/inventory candidate; separate runtime reproduction required | test/integration/signers/test_watchonly.cpp |
| draft-161-RC41 | Capture original session/action generation at arming and ignore stale completion. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback.  | test/integration/signers/test_watchonly.cpp |
| draft-249-D249 | Verify reachable user-visible impact before promoting Enforce a coherent operation object and allowed transitions, including atomic failure/reset, review invalidation, one commit in flight and terminal action capabilities. Existing enum/flag presence alone does not enforce these contracts. Source/inventory candidate; separate runtime reproduction required | test/integration/signers/test_watchonly.cpp |
| draft-255-D255 | Verify reachable user-visible impact before promoting Validate the actual wallet/model/signing capability or supported descriptor shape at the existing entry/projection boundary, rather than inferring it from lock state or one broad flag. Source/inventory candidate; separate runtime reproduction required | test/integration/signers/test_watchonly.cpp |
| draft-286-RC41 | Capture original session/action generation at arming and ignore stale completion. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback.  | test/integration/signers/test_watchonly.cpp |

## R25 — Unify replacement attempt with explicit conflict scope

Prerequisites: R20,R23. Contracts: M10,M04. Work packages: WP7.4.

Replacement adapter shares attempt lifecycle; retain original and approved conflict scope.

**Specific acceptance:** Original confirms/spent/abandoned; stale fee change; no automatic rebuilt submit; encrypted RBF process; O09,O26; Foundation: Demonstrate replacement as an attempt adapter without weakening exact-candidate validation or multiplying unrelated phase dimensions..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-041-D041, draft-046-RC08, draft-056-RC08, draft-103-RC42, draft-113-D113, draft-114-D114, draft-115-D115, draft-116-D116, draft-117-D117, draft-118-RC42, draft-119-D119, draft-120-RC08, draft-121-RC08, draft-140-RC42, draft-278-RC08, draft-288-D288, draft-303-RC08, draft-305-RC09, draft-306-RC08.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-041-D041 | Retest affected current scope, then fix or follow existing issue Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/psbt/test_psbt.cpp |
| draft-046-RC08 | Invalidate/reprepare on relevant state changes and recheck original plus candidate conflict scope at commit. Check current spendability, bump eligibility, fee minima or preparation feasibility at the existing action boundary; a separate workflow machine is not required for this scope. 303 requires concurrent spend of an added input; 306 requires expiry/abandonment. 120 includes additional source-only failed-state paths. | test/integration/psbt/test_psbt.cpp |
| draft-056-RC08 | Invalidate/reprepare on relevant state changes and recheck original plus candidate conflict scope at commit. Invalidate or leave obsolete review/password states and independently revalidate the original plus replacement candidate inputs/conflicts/abandonment at commit. A state transition does not make cached backend facts authoritative. 303 requires concurrent spend of an added input; 306 requires expiry/abandonment. 120 includes additional source-only failed-state paths. | test/integration/psbt/test_psbt.cpp |
| draft-103-RC42 | Bind bump model/txid to one original session and reset that same instance. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback. 140 describes retargeted detail actions; external unload path must actually survive the modal on merged code. | test/integration/psbt/test_psbt.cpp |
| draft-113-D113 | Verify reachable user-visible impact before promoting Enforce a coherent operation object and allowed transitions, including atomic failure/reset, review invalidation, one commit in flight and terminal action capabilities. Existing enum/flag presence alone does not enforce these contracts. Source/inventory candidate; separate runtime reproduction required | test/integration/psbt/test_psbt.cpp |
| draft-114-D114 | Verify reachable user-visible impact before promoting Enforce a coherent operation object and allowed transitions, including atomic failure/reset, review invalidation, one commit in flight and terminal action capabilities. Existing enum/flag presence alone does not enforce these contracts. Source/inventory candidate; separate runtime reproduction required | test/integration/psbt/test_psbt.cpp |
| draft-115-D115 | Verify reachable user-visible impact before promoting Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback. Source/inventory candidate; separate runtime reproduction required | test/integration/psbt/test_psbt.cpp |
| draft-116-D116 | Verify reachable user-visible impact before promoting Enforce a coherent operation object and allowed transitions, including atomic failure/reset, review invalidation, one commit in flight and terminal action capabilities. Existing enum/flag presence alone does not enforce these contracts. Source/inventory candidate; separate runtime reproduction required | test/integration/psbt/test_psbt.cpp |
| draft-117-D117 | Verify reachable user-visible impact before promoting Enforce a coherent operation object and allowed transitions, including atomic failure/reset, review invalidation, one commit in flight and terminal action capabilities. Existing enum/flag presence alone does not enforce these contracts. Source/inventory candidate; separate runtime reproduction required | test/integration/psbt/test_psbt.cpp |
| draft-118-RC42 | Bind bump model/txid to one original session and reset that same instance. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback. 140 describes retargeted detail actions; external unload path must actually survive the modal on merged code. | test/integration/psbt/test_psbt.cpp |
| draft-119-D119 | Verify reachable user-visible impact before promoting Enforce a coherent operation object and allowed transitions, including atomic failure/reset, review invalidation, one commit in flight and terminal action capabilities. Existing enum/flag presence alone does not enforce these contracts. Source/inventory candidate; separate runtime reproduction required | test/integration/psbt/test_psbt.cpp |
| draft-120-RC08 | Invalidate/reprepare on relevant state changes and recheck original plus candidate conflict scope at commit. Enforce a coherent operation object and allowed transitions, including atomic failure/reset, review invalidation, one commit in flight and terminal action capabilities. Existing enum/flag presence alone does not enforce these contracts. 303 requires concurrent spend of an added input; 306 requires expiry/abandonment. 120 includes additional source-only failed-state paths. | test/integration/psbt/test_psbt.cpp |
| draft-121-RC08 | Invalidate/reprepare on relevant state changes and recheck original plus candidate conflict scope at commit. Check current spendability, bump eligibility, fee minima or preparation feasibility at the existing action boundary; a separate workflow machine is not required for this scope. 303 requires concurrent spend of an added input; 306 requires expiry/abandonment. 120 includes additional source-only failed-state paths. | test/integration/psbt/test_psbt.cpp |
| draft-140-RC42 | Bind bump model/txid to one original session and reset that same instance. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback. 140 describes retargeted detail actions; external unload path must actually survive the modal on merged code. | test/integration/psbt/test_psbt.cpp |
| draft-278-RC08 | Invalidate/reprepare on relevant state changes and recheck original plus candidate conflict scope at commit. Invalidate or leave obsolete review/password states and independently revalidate the original plus replacement candidate inputs/conflicts/abandonment at commit. A state transition does not make cached backend facts authoritative. 303 requires concurrent spend of an added input; 306 requires expiry/abandonment. 120 includes additional source-only failed-state paths. | test/integration/psbt/test_psbt.cpp |
| draft-288-D288 | Verify reachable user-visible impact before promoting Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback. Source/inventory candidate; separate runtime reproduction required | test/integration/psbt/test_psbt.cpp |
| draft-303-RC08 | After preparation, independently spend a candidate input by RPC; stale confirmation cannot replace that unrelated payment without renewed approval of the actual conflict scope. 303 requires concurrent spend of an added input; 306 requires expiry/abandonment. 120 includes additional source-only failed-state paths. | test/integration/psbt/test_psbt.cpp |
| draft-305-RC09 | Address transaction timing through Core bump construction; disclose changed terms if intentional. Repair the dependency/backend behavior or cleanup/construction contract; preserve the distinct GUI warning scope in draft 55. Adding GUI phases does not fix the backend mechanism. Transaction-level locks were tested; script-enforced timelocks were not. | test/integration/psbt/test_psbt.cpp |
| draft-306-RC08 | After preparation, abandon/change the original payment; stale confirmation cannot revive it under the old approval; revalidation gives an actionable result. 303 requires concurrent spend of an added input; 306 requires expiry/abandonment. 120 includes additional source-only failed-state paths. | test/integration/psbt/test_psbt.cpp |

## R26 — Extract payment/PSBT import owner

Prerequisites: R16,R21,R22. Contracts: M26. Work packages: WP9.1.

Source independent of target; atomic revisioned Apply; target-bound review factory; child acknowledgment ledger.

**Specific acceptance:** Same-value Maximum; overwrite stale prompt; target switch/close; lost apply/factory acknowledgment; O11,O29,O30; Foundation: Demonstrate import as an attempt adapter without implicit approval; independent child outcome ownership remains explicit..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-002-RC20, draft-004-RC20, draft-005-RC20, draft-107-RC20, draft-246-RC20, draft-312-D312.

**Cross-consumer follow-up:** draft-001-RC16 (fix R16).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-002-RC20 | Revisioned import/apply/discard tokens with explicit dirty-draft replacement and session ownership. Bind deferred actions, draft replacement, dismissal, deletion and undo history to the operation/request/draft revision that created them. 107 is generic PSBT replacement available in Chunk 7; URI-specific reproductions must wait for Chunk 9. Not all paths are exact duplicates. | test/integration/payment_import/test_payment_import.cpp |
| draft-004-RC20 | Revisioned import/apply/discard tokens with explicit dirty-draft replacement and session ownership. Bind deferred actions, draft replacement, dismissal, deletion and undo history to the operation/request/draft revision that created them. 107 is generic PSBT replacement available in Chunk 7; URI-specific reproductions must wait for Chunk 9. Not all paths are exact duplicates. | test/integration/payment_import/test_payment_import.cpp |
| draft-005-RC20 | Revisioned import/apply/discard tokens with explicit dirty-draft replacement and session ownership. Bind deferred actions, draft replacement, dismissal, deletion and undo history to the operation/request/draft revision that created them. 107 is generic PSBT replacement available in Chunk 7; URI-specific reproductions must wait for Chunk 9. Not all paths are exact duplicates. | test/integration/payment_import/test_payment_import.cpp |
| draft-107-RC20 | Revisioned import/apply/discard tokens with explicit dirty-draft replacement and session ownership. Bind deferred actions, draft replacement, dismissal, deletion and undo history to the operation/request/draft revision that created them. 107 is generic PSBT replacement available in Chunk 7; URI-specific reproductions must wait for Chunk 9. Not all paths are exact duplicates. | test/integration/psbt/test_psbt.cpp |
| draft-246-RC20 | Revisioned import/apply/discard tokens with explicit dirty-draft replacement and session ownership. Bind deferred actions, draft replacement, dismissal, deletion and undo history to the operation/request/draft revision that created them. 107 is generic PSBT replacement available in Chunk 7; URI-specific reproductions must wait for Chunk 9. Not all paths are exact duplicates. | test/integration/payment_import/test_payment_import.cpp |
| draft-312-D312 | Fix or follow existing issue using recorded reproduction; confirm current head Keep imported public metadata and user-authored private labels distinct and validate explicit overwrite intent; this is a data-provenance rule rather than an additional lifecycle phase. Merged-head reproduction recorded in document; observe latest narrowed scope | test/integration/payment_import/test_payment_import.cpp |

## R27 — Migrate clipboard, chooser and drop consumers

Prerequisites: R26. Contracts: M26,M24. Work packages: WP9.2.

Thin adapters collect source and original operation; declarative preview and confirmation.

**Specific acceptance:** Actual clipboard/file URL/drop paths; extension/path identity; stale chooser; no JS field synchronization.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-247-D247, draft-248-D248, draft-297-D297.

**Cross-consumer follow-up:** draft-004-RC20 (fix R26), draft-246-RC20 (fix R26).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-247-D247 | Verify reachable user-visible impact before promoting Clarify the intended UX/command/selection/capability contract first. The recorded observation does not by itself establish that another workflow machine is needed. Source/inventory candidate; separate runtime reproduction required | test/integration/payment_import/test_payment_import.cpp |
| draft-248-D248 | Verify reachable user-visible impact before promoting Clarify the intended UX/command/selection/capability contract first. The recorded observation does not by itself establish that another workflow machine is needed. Source/inventory candidate; separate runtime reproduction required | test/integration/payment_import/test_payment_import.cpp |
| draft-297-D297 | Fix or follow existing issue using recorded reproduction; confirm current head Fix request equality/provenance to include the message; existing clipboard transitions can stay unchanged. Merged-head reproduction recorded in document; observe latest narrowed scope | test/integration/payment_import/test_payment_import.cpp |

## R28 — Extract versioned readers and Activity store/detail

Prerequisites: R10,R08. Contracts: M14,M15. Work packages: WP5.1.

Shared reader protocol with coherent snapshots/deltas; stable detail selection; no Core in Qt getters.

**Specific acceptance:** Old errors/deltas; deletion/tombstone; notification during apply; current status; O07,O22; FSM review: Characterize Transaction versus Activity status predicate precedence, reorg/maturity/conflict/abandonment; test mutation between constituent snapshot reads..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-012-RC12, draft-162-RC58, draft-163-RC58, draft-164-RC58, draft-165-D165, draft-168-D168, draft-169-RC58, draft-170-D170, draft-171-D171, draft-172-RC58, draft-173-D173, draft-174-RC12, draft-260-D260, draft-277-RC12.

**Cross-consumer follow-up:** draft-033-RC59 (fix R10), draft-166-RC59 (fix R10).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-012-RC12 | Invalidate from relevant transaction roles and status generations, not only row counts. Repair role/status-based invalidation or derive aggregates from authoritative records. Existing transitions can be correct while a cached total fails to update. 12 also covers transaction status projection; retain all replacement/reorg/expiry cases. | test/integration/wallet_activity/test_activity.cpp |
| draft-162-RC58 | Generation-scoped result/error state, visible failure with existing rows and an independent retry action. Use generation-scoped read/error states, coalesced ordered updates, explicit stopped admission and retry; reconciliation lifecycle must publish a coherent current snapshot. 172 is a consequence of failed reload pruning; establish permanent stale state with a real retry sequence. | test/integration/wallet_activity/test_activity.cpp |
| draft-163-RC58 | Generation-scoped result/error state, visible failure with existing rows and an independent retry action. Use generation-scoped read/error states, coalesced ordered updates, explicit stopped admission and retry; reconciliation lifecycle must publish a coherent current snapshot. 172 is a consequence of failed reload pruning; establish permanent stale state with a real retry sequence. | test/integration/wallet_activity/test_activity.cpp |
| draft-164-RC58 | Generation-scoped result/error state, visible failure with existing rows and an independent retry action. Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. 172 is a consequence of failed reload pruning; establish permanent stale state with a real retry sequence. | test/integration/wallet_activity/test_activity.cpp |
| draft-165-D165 | Verify reachable user-visible impact before promoting Use generation-scoped read/error states, coalesced ordered updates, explicit stopped admission and retry; reconciliation lifecycle must publish a coherent current snapshot. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_activity.cpp |
| draft-168-D168 | Verify reachable user-visible impact before promoting Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_activity.cpp |
| draft-169-RC58 | Generation-scoped result/error state, visible failure with existing rows and an independent retry action. Use generation-scoped read/error states, coalesced ordered updates, explicit stopped admission and retry; reconciliation lifecycle must publish a coherent current snapshot. 172 is a consequence of failed reload pruning; establish permanent stale state with a real retry sequence. | test/integration/wallet_activity/test_activity.cpp |
| draft-170-D170 | Verify reachable user-visible impact before promoting Specify cancellable/failed/retry/backoff/admission outcomes for the operation and implement bounded progress/cancellation effects. A machine cannot forcibly cancel an uncancellable dependency call. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_activity.cpp |
| draft-171-D171 | Verify reachable user-visible impact before promoting Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_activity.cpp |
| draft-172-RC58 | Generation-scoped result/error state, visible failure with existing rows and an independent retry action. Use generation-scoped read/error states, coalesced ordered updates, explicit stopped admission and retry; reconciliation lifecycle must publish a coherent current snapshot. 172 is a consequence of failed reload pruning; establish permanent stale state with a real retry sequence. | test/integration/wallet_activity/test_activity.cpp |
| draft-173-D173 | Verify reachable user-visible impact before promoting Use generation-scoped read/error states, coalesced ordered updates, explicit stopped admission and retry; reconciliation lifecycle must publish a coherent current snapshot. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_activity.cpp |
| draft-174-RC12 | Invalidate from relevant transaction roles and status generations, not only row counts. Repair role/status-based invalidation or derive aggregates from authoritative records. Existing transitions can be correct while a cached total fails to update. 12 also covers transaction status projection; retain all replacement/reorg/expiry cases. | test/integration/wallet_activity/test_activity.cpp |
| draft-260-D260 | Verify reachable user-visible impact before promoting Validate or canonicalize the actual value/identity/path used by the operation, including final-path overwrite confirmation, rather than relying on an earlier display or chooser check. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_activity.cpp |
| draft-277-RC12 | Invalidate from relevant transaction roles and status generations, not only row counts. Repair role/status-based invalidation or derive aggregates from authoritative records. Existing transitions can be correct while a cached total fails to update. 12 also covers transaction status projection; retain all replacement/reorg/expiry cases. | test/integration/wallet_activity/test_activity.cpp |

## R29 — Shared persistent editor with labels consumer

Prerequisites: R14,R28. Contracts: M16,M25. Work packages: WP5.5.

Exact base/edit revisions; dirty-close/conflicts/reconcile; address identity; separate sign/verify CPU and key work.

**Specific acceptance:** Label conflicts; private/public metadata; sign/verify stale input; cancel drains; O17,O31,O34.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-008-RC48, draft-022-D022, draft-040-RC47, draft-043-RC45, draft-057-D057, draft-077-RC47, draft-078-RC48, draft-099-RC74, draft-187-RC49, draft-188-D188, draft-189-RC49, draft-190-RC49, draft-191-RC49, draft-192-RC48, draft-193-RC49, draft-194-RC45, draft-195-D195, draft-196-RC45, draft-197-D197, draft-198-D198, draft-199-D199, draft-283-D283, draft-289-D289.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-008-RC48 | Apply incremental model updates and retain editor identity/focus/scroll across unrelated changes. Primary repair is incremental Qt model updates and stable editor/list identity; an FSM does not itself preserve delegates, focus or scroll across a reset. 8 and 78 have merged-build observations; 192 focus-in case still needs full-app reproduction. | test/integration/wallet_activity/test_receive.cpp |
| draft-022-D022 | Retest affected current scope, then fix or follow existing issue Use exact canonical values and consistent parsing/unit/checked-arithmetic rules in the existing flow; rendered text must not silently change amount meaning. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_activity/test_receive.cpp |
| draft-040-RC47 | Track editor dirty intent and backend revision; do not write untouched stale prefill. Separate committed data, per-editor/request/wallet draft intent and dirty/error state; define commit/discard/refresh ownership and preserve untouched or failed edits appropriately. 40 row cache and 77 request prefill each need coverage. 188 is an unsupported extra trigger, not an independent member. | test/integration/wallet_activity/test_receive.cpp |
| draft-043-RC45 | Keep errors scoped to an attempt and clear them at the documented edit/cancel boundaries. Make credentials, errors and queued retry intent belong to one action attempt; explicitly handle edit, cancel, failure, reopen and security-change transitions. 43 remaining scope is Sign only; cross-wallet Send error leakage was fixed. | test/integration/wallet_activity/test_receive.cpp |
| draft-057-D057 | Retest affected current scope, then fix or follow existing issue Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_activity/test_receive.cpp |
| draft-077-RC47 | Track editor dirty intent and backend revision; do not write untouched stale prefill. Separate committed data, per-editor/request/wallet draft intent and dirty/error state; define commit/discard/refresh ownership and preserve untouched or failed edits appropriately. 40 row cache and 77 request prefill each need coverage. 188 is an unsupported extra trigger, not an independent member. | test/integration/wallet_activity/test_receive.cpp |
| draft-078-RC48 | Apply incremental model updates and retain editor identity/focus/scroll across unrelated changes. Primary repair is incremental Qt model updates and stable editor/list identity; an FSM does not itself preserve delegates, focus or scroll across a reset. 8 and 78 have merged-build observations; 192 focus-in case still needs full-app reproduction. | test/integration/wallet_activity/test_receive.cpp |
| draft-099-RC74 | Incremental address updates and bounded rebuild memory. Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. Related to 8/78 reset lifetime, but allocation pressure is a separate acceptance dimension. | test/integration/wallet_activity/test_receive.cpp |
| draft-187-RC49 | Explicit editor draft state, one commit/discard policy and revision-aware failed-save recovery. Separate committed data, per-editor/request/wallet draft intent and dirty/error state; define commit/discard/refresh ownership and preserve untouched or failed edits appropriately. This is separate from stale unchanged prefill writeback in 40/77; reproduce each normal navigation/save-failure path. | test/integration/wallet_activity/test_receive.cpp |
| draft-188-D188 | No active standalone fix priority for this stated scope Unsupported independent causal claim; backend-label handoff is a consequence of 40/77, not a third reproduced trigger. Unsupported independent causal claim; backend-label handoff is a consequence of 40/77, not a third reproduced trigger. | test/integration/wallet_activity/test_receive.cpp |
| draft-189-RC49 | Explicit editor draft state, one commit/discard policy and revision-aware failed-save recovery. Separate committed data, per-editor/request/wallet draft intent and dirty/error state; define commit/discard/refresh ownership and preserve untouched or failed edits appropriately. This is separate from stale unchanged prefill writeback in 40/77; reproduce each normal navigation/save-failure path. | test/integration/wallet_activity/test_receive.cpp |
| draft-190-RC49 | Explicit editor draft state, one commit/discard policy and revision-aware failed-save recovery. Separate committed data, per-editor/request/wallet draft intent and dirty/error state; define commit/discard/refresh ownership and preserve untouched or failed edits appropriately. This is separate from stale unchanged prefill writeback in 40/77; reproduce each normal navigation/save-failure path. | test/integration/wallet_activity/test_receive.cpp |
| draft-191-RC49 | Explicit editor draft state, one commit/discard policy and revision-aware failed-save recovery. Separate committed data, per-editor/request/wallet draft intent and dirty/error state; define commit/discard/refresh ownership and preserve untouched or failed edits appropriately. This is separate from stale unchanged prefill writeback in 40/77; reproduce each normal navigation/save-failure path. | test/integration/wallet_activity/test_receive.cpp |
| draft-192-RC48 | Apply incremental model updates and retain editor identity/focus/scroll across unrelated changes. Primary repair is incremental Qt model updates and stable editor/list identity; an FSM does not itself preserve delegates, focus or scroll across a reset. 8 and 78 have merged-build observations; 192 focus-in case still needs full-app reproduction. | test/integration/wallet_activity/test_receive.cpp |
| draft-193-RC49 | Explicit editor draft state, one commit/discard policy and revision-aware failed-save recovery. Separate committed data, per-editor/request/wallet draft intent and dirty/error state; define commit/discard/refresh ownership and preserve untouched or failed edits appropriately. This is separate from stale unchanged prefill writeback in 40/77; reproduce each normal navigation/save-failure path. | test/integration/wallet_activity/test_receive.cpp |
| draft-194-RC45 | Keep errors scoped to an attempt and clear them at the documented edit/cancel boundaries. Make credentials, errors and queued retry intent belong to one action attempt; explicitly handle edit, cancel, failure, reopen and security-change transitions. 43 remaining scope is Sign only; cross-wallet Send error leakage was fixed. | test/integration/wallet_activity/test_receive.cpp |
| draft-195-D195 | Verify reachable user-visible impact before promoting Make credentials, errors and queued retry intent belong to one action attempt; explicitly handle edit, cancel, failure, reopen and security-change transitions. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-196-RC45 | Keep errors scoped to an attempt and clear them at the documented edit/cancel boundaries. Make credentials, errors and queued retry intent belong to one action attempt; explicitly handle edit, cancel, failure, reopen and security-change transitions. 43 remaining scope is Sign only; cross-wallet Send error leakage was fixed. | test/integration/wallet_activity/test_receive.cpp |
| draft-197-D197 | Verify reachable user-visible impact before promoting Validate the actual wallet/model/signing capability or supported descriptor shape at the existing entry/projection boundary, rather than inferring it from lock state or one broad flag. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-198-D198 | Verify reachable user-visible impact before promoting Validate the actual wallet/model/signing capability or supported descriptor shape at the existing entry/projection boundary, rather than inferring it from lock state or one broad flag. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-199-D199 | Verify reachable user-visible impact before promoting Move blocking work to a bounded asynchronous execution path. A busy/discovery state does not make a synchronous GUI-thread call responsive. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-283-D283 | Verify reachable user-visible impact before promoting Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-289-D289 | Verify reachable user-visible impact before promoting Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |

## R30 — Extract address reservation and request editor

Prerequisites: R29,R07,R09. Contracts: M11,M12. Work packages: WP5.2.

One generation/bind writer; persistent request editing; explicit delete consent; fresh template intent.

**Specific acceptance:** Used/consumed reservation; rejected generation; immutable public fields; deletion stale base; O16,O32; FSM review: Specify typed request-sharing capability and token-bound copy/QR/export commands; preserve ID/payload/use/visibility checks. Update API/action routing and tests with the consumer; move file writing off GUI.; Foundation: Implement the now-specified V8 sharing API/protocol and M24 source-revocation guards; test multiple views, use during capture/chooser, stale adapter tokens and write-after-invalidation accounting..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-007-RC33, draft-014-D014, draft-023-RC44, draft-025-D025, draft-036-D036, draft-042-D042, draft-099-RC75, draft-124-RC32, draft-127-D127, draft-128-D128, draft-139-RC46, draft-145-D145, draft-147-D147, draft-148-D148, draft-149-D149, draft-150-D150, draft-151-RC44, draft-153-RC46, draft-182-D182, draft-183-RC32, draft-184-RC33, draft-185-RC43, draft-186-D186, draft-226-D226, draft-227-D227, draft-233-RC44, draft-268-RC21, draft-269-RC21, draft-287-RC43, draft-290-RC43, draft-291-D291, draft-292-RC44, draft-311-D311, draft-314-D314.

**Cross-consumer follow-up:** draft-077-RC47 (fix R29), draft-126-RC13 (fix R31), draft-295-RC15 (fix R07), draft-309-RC31 (fix R14).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-007-RC33 | Expose supported types and invalidate or clearly retain the previous draft on generation failure. Constrain receive-address type choices and generation to actual descriptor capabilities in the existing flow; the related failed-transition residue is separately draft 184.  | test/integration/wallet_activity/test_receive.cpp |
| draft-014-D014 | Retest affected current scope, then fix or follow existing issue Bind deferred actions, draft replacement, dismissal, deletion and undo history to the operation/request/draft revision that created them. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_activity/test_receive.cpp |
| draft-023-RC44 | Clear action-specific prompt state on cancel/reopen and report the current failing phase. Make credentials, errors and queued retry intent belong to one action attempt; explicitly handle edit, cancel, failure, reopen and security-change transitions. Do not count 233/292 as confirmed new issues without distinguishing a remaining trigger. | test/integration/wallet_activity/test_receive.cpp |
| draft-025-D025 | Retest affected current scope, then fix or follow existing issue Validate QR encodability and image creation before offering or reporting export success. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_activity/test_receive.cpp |
| draft-036-D036 | Retest affected current scope, then fix or follow existing issue Catch and propagate the failing dependency/persistence operation. Draft 89's demonstrated abort is fixed at the exception boundary; draft 122 separately describes a hypothetical missing failure transition after a partial fix. Merged-head reproduction/reassessment recorded; no fresh runtime test in this analysis | test/integration/wallet_activity/test_receive.cpp |
| draft-042-D042 | Retest affected current scope, then fix or follow existing issue Bind deferred actions, draft replacement, dismissal, deletion and undo history to the operation/request/draft revision that created them. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_activity/test_receive.cpp |
| draft-099-RC75 | Bound accepted metadata and build URI strings on demand. Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. Does not prove QR parser memory vulnerability; oversize QR is separately draft25. | test/integration/wallet_activity/test_receive.cpp |
| draft-124-RC32 | Clear reservations durably and validate unused/saved-address eligibility before binding. Define durable reservation consumption/lifetime and independently reject used or already-bound destinations at binding; validate reachability of the alleged repeat-bind case. Prove that a surviving GUI path can bind the address twice; current guards may prevent the alleged consequence. | test/integration/wallet_activity/test_receive.cpp |
| draft-127-D127 | Verify reachable user-visible impact before promoting Remove or correctly consume dead/unused state surfaces and notifications; their presence is not an established workflow defect. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-128-D128 | Verify reachable user-visible impact before promoting Derive current receipt/sharing/copy predicates from the appropriate current evidence and payload. Keep historical address-use protection separate from current credited amount; do not automatically reopen sharing or treat a partial receipt as paid-in-full. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-139-RC46 | Interleave GUI temporary authorization with independent RPC unlock/lock and scheduled relock; cleanup cannot undo a later authorized unlock. Source-only concurrency candidates; does not establish permanent unauthorized unlock. | test/integration/wallet_activity/test_receive.cpp |
| draft-145-D145 | No active standalone fix priority for this stated scope Already-unlocked wallet does not reauthenticate a redundant passphrase; no standalone issue. Already-unlocked wallet does not reauthenticate a redundant passphrase; no standalone issue. | test/integration/wallet_activity/test_receive.cpp |
| draft-147-D147 | Verify reachable user-visible impact before promoting Validate the actual wallet/model/signing capability or supported descriptor shape at the existing entry/projection boundary, rather than inferring it from lock state or one broad flag. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-148-D148 | Verify reachable user-visible impact before promoting Make credentials, errors and queued retry intent belong to one action attempt; explicitly handle edit, cancel, failure, reopen and security-change transitions. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-149-D149 | Verify reachable user-visible impact before promoting Make credentials, errors and queued retry intent belong to one action attempt; explicitly handle edit, cancel, failure, reopen and security-change transitions. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-150-D150 | Verify reachable user-visible impact before promoting Remove or correctly consume dead/unused state surfaces and notifications; their presence is not an established workflow defect. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-151-RC44 | Clear action-specific prompt state on cancel/reopen and report the current failing phase. Make credentials, errors and queued retry intent belong to one action attempt; explicitly handle edit, cancel, failure, reopen and security-change transitions. Do not count 233/292 as confirmed new issues without distinguishing a remaining trigger. | test/integration/wallet_activity/test_receive.cpp |
| draft-153-RC46 | Interleave GUI temporary authorization with independent RPC unlock/lock and scheduled relock; cleanup cannot undo a later authorized unlock. Source-only concurrency candidates; does not establish permanent unauthorized unlock. | test/integration/wallet_activity/test_receive.cpp |
| draft-182-D182 | Verify reachable user-visible impact before promoting Keep the requested destination/type and address-generation outcome explicit; a background use notification or failed type change may not silently rebind an existing draft. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-183-RC32 | Clear reservations durably and validate unused/saved-address eligibility before binding. Define durable reservation consumption/lifetime and independently reject used or already-bound destinations at binding; validate reachability of the alleged repeat-bind case. Prove that a surviving GUI path can bind the address twice; current guards may prevent the alleged consequence. | test/integration/wallet_activity/test_receive.cpp |
| draft-184-RC33 | Expose supported types and invalidate or clearly retain the previous draft on generation failure. Keep the requested destination/type and address-generation outcome explicit; a background use notification or failed type change may not silently rebind an existing draft.  | test/integration/wallet_activity/test_receive.cpp |
| draft-185-RC43 | Pin session plus request ID; honor failed opens and suppress stale close callbacks. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback.  | test/integration/wallet_activity/test_receive.cpp |
| draft-186-D186 | Verify reachable user-visible impact before promoting Clarify the intended UX/command/selection/capability contract first. The recorded observation does not by itself establish that another workflow machine is needed. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-226-D226 | Verify reachable user-visible impact before promoting Give an accepted QR capture/export an explicit completion/cancellation lifetime and captured request/payload identity when the owner closes. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-227-D227 | Verify reachable user-visible impact before promoting Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-233-RC44 | Clear action-specific prompt state on cancel/reopen and report the current failing phase. Make credentials, errors and queued retry intent belong to one action attempt; explicitly handle edit, cancel, failure, reopen and security-change transitions. Do not count 233/292 as confirmed new issues without distinguishing a remaining trigger. | test/integration/wallet_activity/test_receive.cpp |
| draft-268-RC21 | Keep satoshi value and editor-unit snapshot authoritative; unit changes only reformat the value. Use exact canonical values and consistent parsing/unit/checked-arithmetic rules in the existing flow; rendered text must not silently change amount meaning. Source candidates need current saved-request and decoded-QR checks. | test/integration/wallet_activity/test_receive.cpp |
| draft-269-RC21 | Keep satoshi value and editor-unit snapshot authoritative; unit changes only reformat the value. Use exact canonical values and consistent parsing/unit/checked-arithmetic rules in the existing flow; rendered text must not silently change amount meaning. Source candidates need current saved-request and decoded-QR checks. | test/integration/wallet_activity/test_receive.cpp |
| draft-287-RC43 | Pin session plus request ID; honor failed opens and suppress stale close callbacks. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback.  | test/integration/wallet_activity/test_activity.cpp |
| draft-290-RC43 | Pin session plus request ID; honor failed opens and suppress stale close callbacks. Pin original wallet session plus target/action identity and invalidate or drain the same instance when it closes or its view disappears; never resolve a later global selection in a callback.  | test/integration/wallet_activity/test_receive.cpp |
| draft-291-D291 | Verify reachable user-visible impact before promoting Separate committed data, per-editor/request/wallet draft intent and dirty/error state; define commit/discard/refresh ownership and preserve untouched or failed edits appropriately. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_receive.cpp |
| draft-292-RC44 | Clear action-specific prompt state on cancel/reopen and report the current failing phase. Make credentials, errors and queued retry intent belong to one action attempt; explicitly handle edit, cancel, failure, reopen and security-change transitions. Do not count 233/292 as confirmed new issues without distinguishing a remaining trigger. | test/integration/wallet_activity/test_receive.cpp |
| draft-311-D311 | Fix or follow existing issue using recorded reproduction; confirm current head Separate committed data, per-editor/request/wallet draft intent and dirty/error state; define commit/discard/refresh ownership and preserve untouched or failed edits appropriately. Merged-head reproduction recorded in document; observe latest narrowed scope | test/integration/wallet_activity/test_receive.cpp |
| draft-314-D314 | Fix or follow existing issue using recorded reproduction; confirm current head Bind deferred actions, draft replacement, dismissal, deletion and undo history to the operation/request/draft revision that created them. Merged-head reproduction recorded in document; observe latest narrowed scope | test/integration/wallet_activity/test_receive.cpp |

## R31 — Extract receipt reconciliation

Prerequisites: R30,R28. Contracts: M13. Work packages: WP5.3.

Current credit separate from historical use; merge reorg/replacement/deletion updates.

**Specific acceptance:** Actual payments/reorgs/replacements; delayed observations; receipt persistence across restart; FSM review: Verify use during pending sharing invalidates admission while historical use remains set after credited totals decrease; preserve exact request identity..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-099-RC73, draft-100-RC69, draft-100-RC79, draft-101-RC14, draft-123-RC14, draft-125-RC13, draft-126-RC13, draft-167-D167, draft-275-RC13, draft-276-RC13, draft-279-RC60.

**Cross-consumer follow-up:** draft-287-RC43 (fix R30).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-099-RC73 | Use address/request snapshots and references instead of per-action copies. Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. Memory growth shape inferred from source and component measurements; not a confirmed remote crash. | test/integration/wallet_activity/test_activity.cpp |
| draft-100-RC69 | Incremental aggregates, batched filtering and cached payer reconciliation. Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. Static trigger/complexity analysis plus prior timing anchors; no end-to-end hostile-dusting runtime confirmation. | test/integration/wallet_activity/test_activity.cpp |
| draft-100-RC79 | Cache reconciliation deltas and move bounded work off GUI thread. Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. Distinct from draft32 per-coin label lookup and draft34 PR-only snapshot rebuild. | test/integration/wallet_activity/test_activity.cpp |
| draft-101-RC14 | Keep held; preserve actual received amount and the intentional observed-address protection. Held design claim: intentional partial-payment status shows actual received amount; not established paid-in-full behavior. Zero-after-cancellation is separately tracked. No confirmed paid-in-full defect; do not merge with the zero-after-cancellation bug. | test/integration/wallet_activity/test_activity.cpp |
| draft-123-RC14 | Keep held; preserve actual received amount and the intentional observed-address protection. Positive-underpayment claim held with 101; zero-after-cancellation overlaps 125 and existing issue. No confirmed paid-in-full defect; do not merge with the zero-after-cancellation bug. | test/integration/wallet_activity/test_activity.cpp |
| draft-125-RC13 | Derive presentation from current qualifying receipts; restore awaiting/sharing state consistently after cancellation/reorg. Derive current receipt/sharing/copy predicates from the appropriate current evidence and payload. Keep historical address-use protection separate from current credited amount; do not automatically reopen sharing or treat a partial receipt as paid-in-full. Positive partial receipt is intentional and is excluded in 101/123; 126 is a UI consequence, not another settlement bug. | test/integration/wallet_activity/test_activity.cpp |
| draft-126-RC13 | Derive presentation from current qualifying receipts; restore awaiting/sharing state consistently after cancellation/reorg. Derive current receipt/sharing/copy predicates from the appropriate current evidence and payload. Keep historical address-use protection separate from current credited amount; do not automatically reopen sharing or treat a partial receipt as paid-in-full. Positive partial receipt is intentional and is excluded in 101/123; 126 is a UI consequence, not another settlement bug. | test/integration/wallet_activity/test_receive.cpp |
| draft-167-D167 | Verify reachable user-visible impact before promoting Use generation-scoped read/error states, coalesced ordered updates, explicit stopped admission and retry; reconciliation lifecycle must publish a coherent current snapshot. Source/inventory candidate; separate runtime reproduction required | test/integration/wallet_activity/test_activity.cpp |
| draft-275-RC13 | Derive presentation from current qualifying receipts; restore awaiting/sharing state consistently after cancellation/reorg. Use generation-scoped read/error states, coalesced ordered updates, explicit stopped admission and retry; reconciliation lifecycle must publish a coherent current snapshot. Positive partial receipt is intentional and is excluded in 101/123; 126 is a UI consequence, not another settlement bug. | test/integration/wallet_activity/test_activity.cpp |
| draft-276-RC13 | Derive presentation from current qualifying receipts; restore awaiting/sharing state consistently after cancellation/reorg. Derive current receipt/sharing/copy predicates from the appropriate current evidence and payload. Keep historical address-use protection separate from current credited amount; do not automatically reopen sharing or treat a partial receipt as paid-in-full. Positive partial receipt is intentional and is excluded in 101/123; 126 is a UI consequence, not another settlement bug. | test/integration/wallet_activity/test_activity.cpp |
| draft-279-RC60 | Retain authoritative generation/event state and reconcile against current Core data. Use generation-scoped read/error states, coalesced ordered updates, explicit stopped admission and retry; reconciliation lifecycle must publish a coherent current snapshot. Shares reconciliation feature with 125, but has a distinct lost-update cause. | test/integration/wallet_activity/test_activity.cpp |

## R32 — Bounded Activity filtering and snapshot export

Prerequisites: R28,R22. Contracts: M14,M24. Work packages: WP5.4.

Off-GUI transforms; incremental stable rows; exact CSV snapshot; measured work/memory budgets.

**Specific acceptance:** Filter normalization; large histories; stale diff base; CSV columns/units/escaping; no wall-clock-only benchmark.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-010-D010, draft-013-D013, draft-027-D027, draft-028-D028, draft-052-D052, draft-100-RC77, draft-100-RC78, draft-299-D299, draft-300-D300, draft-301-D301, draft-318-D318.

**Cross-consumer follow-up:** draft-174-RC12 (fix R28), draft-277-RC12 (fix R28).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-010-D010 | Retest affected current scope, then fix or follow existing issue Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_activity/test_activity.cpp |
| draft-013-D013 | Retest affected current scope, then fix or follow existing issue Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_activity/test_activity.cpp |
| draft-027-D027 | Track existing issue/PR; validate remaining scope and merged status; do not duplicate Check the final write/sync result and durability before reporting success, clearing dirty intent, deleting migration input or reusing a backup path. This is a persistence protocol obligation, not necessarily a new FSM. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_activity/test_activity.cpp |
| draft-028-D028 | Retest affected current scope, then fix or follow existing issue Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_activity/test_activity.cpp |
| draft-052-D052 | Retest affected current scope, then fix or follow existing issue Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/wallet_activity/test_activity.cpp |
| draft-100-RC77 | Maintain generation-consistent aggregate deltas. Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. Static source analysis; do not equate with a reproduced dust attack. | test/integration/wallet_activity/test_activity.cpp |
| draft-100-RC78 | Cache comparator state and batch/reuse filter results. Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. Separate algorithm from aggregate caching; retain a separate bounded performance test. | test/integration/wallet_activity/test_activity.cpp |
| draft-299-D299 | Fix or follow existing issue using recorded reproduction; confirm current head Separate committed data, per-editor/request/wallet draft intent and dirty/error state; define commit/discard/refresh ownership and preserve untouched or failed edits appropriately. Merged-head reproduction recorded in document; observe latest narrowed scope | test/integration/wallet_activity/test_activity.cpp |
| draft-300-D300 | Fix or follow existing issue using recorded reproduction; confirm current head Clarify the intended UX/command/selection/capability contract first. The recorded observation does not by itself establish that another workflow machine is needed. Merged-head reproduction recorded in document; observe latest narrowed scope | test/integration/wallet_activity/test_activity.cpp |
| draft-301-D301 | Fix or follow existing issue using recorded reproduction; confirm current head Correct accepted search/date normalization and domain-boundary rules in the existing filter; preserve valid localized/displayed inputs. Merged-head reproduction recorded in document; observe latest narrowed scope | test/integration/wallet_activity/test_activity.cpp |
| draft-318-D318 | Fix or follow existing issue using recorded reproduction; confirm current head Correct accepted search/date normalization and domain-boundary rules in the existing filter; preserve valid localized/displayed inputs. Merged-head reproduction recorded in document; observe latest narrowed scope | test/integration/wallet_activity/test_activity.cpp |

## R33 — Migrate effective settings and editor batches

Prerequisites: R29,R08. Contracts: M17. Work packages: WP2.2,WP3.3.

Shared versioned editor; complete effective config validation; signer/mempool constraints and asynchronous persistence.

**Specific acceptance:** Invalid batch/no partial save; RPC/config conflicts; restart semantics; O17,O19,O31.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-037-D037, draft-051-RC53, draft-059-D059, draft-060-RC52, draft-061-RC52, draft-069-RC52, draft-073-RC51, draft-083-D083, draft-155-RC55, draft-175-RC54, draft-176-RC51, draft-177-RC54, draft-178-RC55, draft-179-RC55, draft-180-RC55, draft-181-D181, draft-209-RC56, draft-210-RC56, draft-211-D211, draft-212-RC56, draft-213-D213, draft-214-D214, draft-215-D215, draft-216-D216, draft-262-RC54, draft-271-RC53, draft-272-D272, draft-273-D273, draft-274-D274, draft-298-RC52.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-037-D037 | Retest affected current scope, then fix or follow existing issue Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/node_setup/test_settings.cpp |
| draft-051-RC53 | One explicit save/discard/cancel route policy across sidebar, Done, Back and onboarding close. One explicit Save/Discard/Cancel policy must govern every navigation/focus exit for a dirty proxy draft. 271 traces a legacy route; reconcile its auto-commit premise with the current narrowed 51 reproduction before claiming a duplicate. | test/integration/node_setup/test_settings.cpp |
| draft-059-D059 | Retest affected current scope, then fix or follow existing issue Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/node_setup/test_settings.cpp |
| draft-060-RC52 | Validate effective configuration before persisting and provide a recoverable settings path if startup fails. Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. Not one literal validator defect: retain each configuration testcase. 298 is nondefault-debug-policy/P3. | test/integration/node_setup/test_settings.cpp |
| draft-061-RC52 | Validate effective configuration before persisting and provide a recoverable settings path if startup fails. Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. Not one literal validator defect: retain each configuration testcase. 298 is nondefault-debug-policy/P3. | test/integration/node_setup/test_settings.cpp |
| draft-069-RC52 | Validate effective configuration before persisting and provide a recoverable settings path if startup fails. Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. Not one literal validator defect: retain each configuration testcase. 298 is nondefault-debug-policy/P3. | test/integration/node_setup/test_settings.cpp |
| draft-073-RC51 | Retain explicit intent and compare effective next-start configuration; keep restart notice truthful. Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid.  | test/integration/node_setup/test_settings.cpp |
| draft-083-D083 | Retest affected current scope, then fix or follow existing issue Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/node_setup/test_settings.cpp |
| draft-155-RC55 | Derive meaningful effective-change state and consume/clear it in feature notices. Correct effective-change/restart derivation and connect the notice consumer; restart wording is not an operation lifecycle. 155 is signer-specific and belongs to Chunk 8 configuration consumer; no need to introduce signer UI in Chunk 2. | test/integration/signers/test_watchonly.cpp |
| draft-175-RC54 | Propagate write status and preserve dirty state on failure; verify model/file/restart agree. Check the final write/sync result and durability before reporting success, clearing dirty intent, deleting migration input or reusing a backup path. This is a persistence protocol obligation, not necessarily a new FSM. Current Connection exceptions partly recover; this family is not proof that the old fixed crash/write scope remains. | test/integration/node_setup/test_settings.cpp |
| draft-176-RC51 | Retain explicit intent and compare effective next-start configuration; keep restart notice truthful. Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid.  | test/integration/node_setup/test_settings.cpp |
| draft-177-RC54 | Propagate write status and preserve dirty state on failure; verify model/file/restart agree. Separate explicit commit intent, dirty persistence state and numeric equality; an accepted no-op must not silently leave an intended write uncommitted. Current Connection exceptions partly recover; this family is not proof that the old fixed crash/write scope remains. | test/integration/node_setup/test_settings.cpp |
| draft-178-RC55 | Derive meaningful effective-change state and consume/clear it in feature notices. Correct effective-change/restart derivation and connect the notice consumer; restart wording is not an operation lifecycle. 155 is signer-specific and belongs to Chunk 8 configuration consumer; no need to introduce signer UI in Chunk 2. | test/integration/node_setup/test_settings.cpp |
| draft-179-RC55 | Derive meaningful effective-change state and consume/clear it in feature notices. Correct effective-change/restart derivation and connect the notice consumer; restart wording is not an operation lifecycle. 155 is signer-specific and belongs to Chunk 8 configuration consumer; no need to introduce signer UI in Chunk 2. | test/integration/node_setup/test_settings.cpp |
| draft-180-RC55 | Derive meaningful effective-change state and consume/clear it in feature notices. Clarify the intended UX/command/selection/capability contract first. The recorded observation does not by itself establish that another workflow machine is needed. 155 is signer-specific and belongs to Chunk 8 configuration consumer; no need to introduce signer UI in Chunk 2. | test/integration/node_setup/test_settings.cpp |
| draft-181-D181 | Verify reachable user-visible impact before promoting Validate a proposed proxy value before mutating the stored session, or apply it transactionally; an early validation failure must leave the prior value intact. Source/inventory candidate; separate runtime reproduction required | test/integration/node_setup/test_settings.cpp |
| draft-209-RC56 | Keep staged answers, atomically publish successful setup and explicitly handle cancel/drain/rollback. Distinguish first-run configuration, recoverable startup failure, preparation and durable completion; preserve answers and define retry/cancel/recovery instead of treating partial setup as completed. Inspect current rollback work and reproduce remaining behavior; these source observations are not a new retest. | test/integration/node_setup/test_settings.cpp |
| draft-210-RC56 | Keep staged answers, atomically publish successful setup and explicitly handle cancel/drain/rollback. Distinguish first-run configuration, recoverable startup failure, preparation and durable completion; preserve answers and define retry/cancel/recovery instead of treating partial setup as completed. Inspect current rollback work and reproduce remaining behavior; these source observations are not a new retest. | test/integration/node_setup/test_settings.cpp |
| draft-211-D211 | Verify reachable user-visible impact before promoting Distinguish first-run configuration, recoverable startup failure, preparation and durable completion; preserve answers and define retry/cancel/recovery instead of treating partial setup as completed. Source/inventory candidate; separate runtime reproduction required | test/integration/node_setup/test_settings.cpp |
| draft-212-RC56 | Keep staged answers, atomically publish successful setup and explicitly handle cancel/drain/rollback. Distinguish first-run configuration, recoverable startup failure, preparation and durable completion; preserve answers and define retry/cancel/recovery instead of treating partial setup as completed. Inspect current rollback work and reproduce remaining behavior; these source observations are not a new retest. | test/integration/node_setup/test_settings.cpp |
| draft-213-D213 | Verify reachable user-visible impact before promoting Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. Source/inventory candidate; separate runtime reproduction required | test/integration/node_setup/test_settings.cpp |
| draft-214-D214 | Verify reachable user-visible impact before promoting Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. Source/inventory candidate; separate runtime reproduction required | test/integration/node_setup/test_settings.cpp |
| draft-215-D215 | Verify reachable user-visible impact before promoting Separate committed data, per-editor/request/wallet draft intent and dirty/error state; define commit/discard/refresh ownership and preserve untouched or failed edits appropriately. Source/inventory candidate; separate runtime reproduction required | test/integration/node_setup/test_settings.cpp |
| draft-216-D216 | Verify reachable user-visible impact before promoting Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. Source/inventory candidate; separate runtime reproduction required | test/integration/node_setup/test_settings.cpp |
| draft-262-RC54 | Propagate write status and preserve dirty state on failure; verify model/file/restart agree. Check the final write/sync result and durability before reporting success, clearing dirty intent, deleting migration input or reusing a backup path. This is a persistence protocol obligation, not necessarily a new FSM. Current Connection exceptions partly recover; this family is not proof that the old fixed crash/write scope remains. | test/integration/node_setup/test_settings.cpp |
| draft-271-RC53 | One explicit save/discard/cancel route policy across sidebar, Done, Back and onboarding close. One explicit Save/Discard/Cancel policy must govern every navigation/focus exit for a dirty proxy draft. 271 traces a legacy route; reconcile its auto-commit premise with the current narrowed 51 reproduction before claiming a duplicate. | test/integration/node_setup/test_settings.cpp |
| draft-272-D272 | Verify reachable user-visible impact before promoting Validate the complete proxy edit set and publish/persist it as one coherent transaction with explicit partial-failure recovery. Source/inventory candidate; separate runtime reproduction required | test/integration/node_setup/test_settings.cpp |
| draft-273-D273 | Verify reachable user-visible impact before promoting Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. Source/inventory candidate; separate runtime reproduction required | test/integration/node_setup/test_settings.cpp |
| draft-274-D274 | Verify reachable user-visible impact before promoting Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. Source/inventory candidate; separate runtime reproduction required | test/integration/node_setup/test_settings.cpp |
| draft-298-RC52 | Validate effective configuration before persisting and provide a recoverable settings path if startup fails. Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. Not one literal validator defect: retain each configuration testcase. 298 is nondefault-debug-policy/P3. | test/integration/node_setup/test_settings.cpp |

## R34 — Display settings and geometry consumers

Prerequisites: R33,R07. Contracts: M17,M30. Work packages: WP2.4.

Typed display values and restored geometry; presentation changes preserve canonical amounts.

**Specific acceptance:** Units/locales/geometry boundaries; both shells and persisted restart.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-044-D044, draft-067-D067, draft-258-D258, draft-259-D259, draft-285-D285.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-044-D044 | Retest affected current scope, then fix or follow existing issue Validate or canonicalize the actual value/identity/path used by the operation, including final-path overwrite confirmation, rather than relying on an earlier display or chooser check. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/node_setup/test_settings.cpp |
| draft-067-D067 | Retest affected current scope, then fix or follow existing issue Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/node_setup/test_settings.cpp |
| draft-258-D258 | Verify reachable user-visible impact before promoting Validate or canonicalize the actual value/identity/path used by the operation, including final-path overwrite confirmation, rather than relying on an earlier display or chooser check. Source/inventory candidate; separate runtime reproduction required | test/integration/node_setup/test_settings.cpp |
| draft-259-D259 | Verify reachable user-visible impact before promoting Validate or canonicalize the actual value/identity/path used by the operation, including final-path overwrite confirmation, rather than relying on an earlier display or chooser check. Source/inventory candidate; separate runtime reproduction required | test/integration/node_setup/test_settings.cpp |
| draft-285-D285 | Verify reachable user-visible impact before promoting Repair exit-status/platform-preference/notification/readback wiring. A modeled transition cannot supply an observation or status that is never propagated. Source/inventory candidate; separate runtime reproduction required | test/integration/node_setup/test_settings.cpp |

## R35 — Onboarding and durable profile handoff

Prerequisites: R33,R09. Contracts: M18. Work packages: WP2.5.

Revisioned path/storage checks; durable handoff; partial outcome recovery; no repeated migration.

**Specific acceptance:** Old preview; persistence rejection/uncertainty; source data; restart/preinit; O19,O36; Foundation: Exercise versioned persistence and interrupted recovery; document recoverable versus uncertain effects without blind startup replay..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-066-D066, draft-068-RC57, draft-070-D070, draft-072-D072, draft-074-RC57, draft-084-D084, draft-085-RC57, draft-091-RC50, draft-263-D263, draft-264-RC50.

**Cross-consumer follow-up:** draft-262-RC54 (fix R33).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-066-D066 | No active standalone fix priority for this stated scope Deferred pending stronger ordinary-user impact; retain as coverage rather than an actionable defect. Deferred pending stronger ordinary-user impact; retain as coverage rather than an actionable defect. | test/integration/node_setup/test_settings.cpp |
| draft-068-RC57 | Catch and surface ordinary startup errors while retaining active-profile/onboarding recovery. Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. 68 conflicting chain flags, 74 InitConfig return and 85 path assertion are not exact duplicates. | test/integration/node_setup/test_settings.cpp |
| draft-070-D070 | Retest affected current scope, then fix or follow existing issue Check the final write/sync result and durability before reporting success, clearing dirty intent, deleting migration input or reusing a backup path. This is a persistence protocol obligation, not necessarily a new FSM. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/node_setup/test_settings.cpp |
| draft-072-D072 | Retest affected current scope, then fix or follow existing issue Repair exit-status/platform-preference/notification/readback wiring. A modeled transition cannot supply an observation or status that is never propagated. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/node_setup/test_settings.cpp |
| draft-074-RC57 | Catch and surface ordinary startup errors while retaining active-profile/onboarding recovery. Distinguish first-run configuration, recoverable startup failure, preparation and durable completion; preserve answers and define retry/cancel/recovery instead of treating partial setup as completed. 68 conflicting chain flags, 74 InitConfig return and 85 path assertion are not exact duplicates. | test/integration/node_setup/test_settings.cpp |
| draft-084-D084 | Retest affected current scope, then fix or follow existing issue Distinguish first-run configuration, recoverable startup failure, preparation and durable completion; preserve answers and define retry/cancel/recovery instead of treating partial setup as completed. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/node_setup/test_settings.cpp |
| draft-085-RC57 | Catch and surface ordinary startup errors while retaining active-profile/onboarding recovery. Validate effective configuration, editability and operation prerequisites before accepting/persisting them, and report the actual effective result. A state machine cannot make an invalid startup combination valid. 68 conflicting chain flags, 74 InitConfig return and 85 path assertion are not exact duplicates. | test/integration/node_setup/test_settings.cpp |
| draft-091-RC50 | Write/verify replacement first and retain recoverable source on every failure. Check the final write/sync result and durability before reporting success, clearing dirty intent, deleting migration input or reusing a backup path. This is a persistence protocol obligation, not necessarily a new FSM.  | test/integration/node_setup/test_settings.cpp |
| draft-263-D263 | Verify reachable user-visible impact before promoting Validate or canonicalize the actual value/identity/path used by the operation, including final-path overwrite confirmation, rather than relying on an earlier display or chooser check. Source/inventory candidate; separate runtime reproduction required | test/integration/node_setup/test_settings.cpp |
| draft-264-RC50 | Write/verify replacement first and retain recoverable source on every failure. Check the final write/sync result and durability before reporting success, clearing dirty intent, deleting migration input or reusing a backup path. This is a persistence protocol obligation, not necessarily a new FSM.  | test/integration/node_setup/test_settings.cpp |

## R36 — Runtime prompts and complete node lifecycle

Prerequisites: R08,R35,R10. Contracts: M19,M20,M21. Work packages: WP2.3.

Worker prompt queue and answer cells; network command observation; ordered stop/fatal cleanup.

**Specific acceptance:** Blocked question; multiple prompts; network RPC; initialization shutdown; exit codes; IPC interface_gui; O20,O21,O36; FSM review: Preserve authoritative sync completion and catch-up hysteresis, independent reachability/connectivity and consistent snapshot application; estimates never authorize readiness..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-071-D071, draft-234-D234, draft-237-D237, draft-238-D238, draft-239-D239, draft-240-D240, draft-241-D241, draft-242-D242, draft-243-D243.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-071-D071 | Retest affected current scope, then fix or follow existing issue Repair exit-status/platform-preference/notification/readback wiring. A modeled transition cannot supply an observation or status that is never propagated. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/node_tools/test_node.cpp |
| draft-234-D234 | Verify reachable user-visible impact before promoting Coordinate window visibility, runtime questions, startup attempts, observed network results and stopping/fatal transitions. Do not infer current failures from superseded shutdown premises. Source/inventory candidate; separate runtime reproduction required | test/integration/node_tools/test_node.cpp |
| draft-237-D237 | Verify reachable user-visible impact before promoting Coordinate window visibility, runtime questions, startup attempts, observed network results and stopping/fatal transitions. Do not infer current failures from superseded shutdown premises. Source/inventory candidate; separate runtime reproduction required | test/integration/node_tools/test_node.cpp |
| draft-238-D238 | Verify reachable user-visible impact before promoting Coordinate window visibility, runtime questions, startup attempts, observed network results and stopping/fatal transitions. Do not infer current failures from superseded shutdown premises. Source/inventory candidate; separate runtime reproduction required | test/integration/node_tools/test_node.cpp |
| draft-239-D239 | Verify reachable user-visible impact before promoting Coordinate window visibility, runtime questions, startup attempts, observed network results and stopping/fatal transitions. Do not infer current failures from superseded shutdown premises. Source/inventory candidate; separate runtime reproduction required | test/integration/node_tools/test_node.cpp |
| draft-240-D240 | Verify reachable user-visible impact before promoting Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Source/inventory candidate; separate runtime reproduction required | test/integration/node_tools/test_node.cpp |
| draft-241-D241 | Verify reachable user-visible impact before promoting Coordinate window visibility, runtime questions, startup attempts, observed network results and stopping/fatal transitions. Do not infer current failures from superseded shutdown premises. Source/inventory candidate; separate runtime reproduction required | test/integration/node_tools/test_node.cpp |
| draft-242-D242 | Verify reachable user-visible impact before promoting Clarify the intended UX/command/selection/capability contract first. The recorded observation does not by itself establish that another workflow machine is needed. Source/inventory candidate; separate runtime reproduction required | test/integration/node_tools/test_node.cpp |
| draft-243-D243 | Verify reachable user-visible impact before promoting Coordinate window visibility, runtime questions, startup attempts, observed network results and stopping/fatal transitions. Do not infer current failures from superseded shutdown premises. Source/inventory candidate; separate runtime reproduction required | test/integration/node_tools/test_node.cpp |

## R37 — Navigation and modal ownership

Prerequisites: R36,R11,R26. Contracts: M20. Work packages: WP2.1.

One admission policy; declarative owned prompt/operation rendering; no view-driven lifecycle mutation.

**Specific acceptance:** Menu/page equivalents; dirty modal; late attaching view; focus and no binding-triggered commands; FSM review: Retain popup/native-dialog cleanup on hide and stale-callback guards; presentation animation/focus never controls domain completion or cleanup.; Foundation: Test recreated views and independent presentation tokens; model-owned operations survive hiding and do not require signal replay..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-230-D230.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-230-D230 | Verify reachable user-visible impact before promoting Give dialogs/modal actions explicit ownership and open/close/reentrancy/admission rules; reject stale acceptance and define cancellation outcomes. Source/inventory candidate; separate runtime reproduction required | test/integration/node_setup/test_navigation.cpp |

## R38 — Console and log models

Prerequisites: R06,R08,R36. Contracts: M22,M27. Work packages: WP3.4,WP3.6.

Console target/input/output identities; bounded redacted storage; log reader offsets/rotation; completion acceptance.

**Specific acceptance:** Clear during execute; unavailable wallet no node fallback; exactly-one execute; log rotation/loadOlder; resource subscopes; FSM review: Specify activation per reader: traffic raw history can continue while hidden, logs can stop watching; test reactivation and stale publication without canceling unrelated operations..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-053-D053, draft-054-RC65, draft-063-D063, draft-079-D079, draft-099-RC72, draft-200-RC65, draft-201-RC66, draft-202-RC66, draft-203-RC67, draft-204-RC67, draft-205-D205, draft-206-D206, draft-207-RC66, draft-208-RC68, draft-208-RC72, draft-244-D244, draft-293-RC67.

**Cross-consumer follow-up:** draft-092-RC05 (fix R06).

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-053-D053 | No active standalone fix priority for this stated scope Fixed: missing-debug-log error is visible on merged-head retest. Fixed: missing-debug-log error is visible on merged-head retest. | test/integration/node_tools/test_console.cpp |
| draft-054-RC65 | Explicitly define selected suggestion and Enter behavior, with no unintended command replacement during busy state. Separate hovered/selected completion, typed command and execution state; Enter has a defined transition and may not accidentally replace input during execution. Substituted command in 54 is a bare sibling name without arguments; destructive command execution was not demonstrated. | test/integration/node_tools/test_console.cpp |
| draft-063-D063 | No active standalone fix priority for this stated scope Fixed: configured/disabled log handling passed merged-head retest. Fixed: configured/disabled log handling passed merged-head retest. | test/integration/node_tools/test_console.cpp |
| draft-079-D079 | Retest affected current scope, then fix or follow existing issue Repair exit-status/platform-preference/notification/readback wiring. A modeled transition cannot supply an observation or status that is never propagated. Earlier evidence retained; current-head reproduction not established by this prioritization | test/integration/node_tools/test_console.cpp |
| draft-099-RC72 | Bound bytes as well as rows or virtualize output. Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. No end-to-end OOM; 208 is covered by this component scope. | test/integration/node_tools/test_console.cpp |
| draft-200-RC65 | Explicitly define selected suggestion and Enter behavior, with no unintended command replacement during busy state. Separate hovered/selected completion, typed command and execution state; Enter has a defined transition and may not accidentally replace input during execution. Substituted command in 54 is a bare sibling name without arguments; destructive command execution was not demonstrated. | test/integration/node_tools/test_console.cpp |
| draft-201-RC66 | Define output-clear versus command-history/context behavior explicitly and reject stale presentation effects as needed. Define what Clear means for an in-flight operation, output generation, wallet context and history cursor; retain or suppress subsequent effects consistently. Not every retained field is a bug; fresh normal-user impact must be demonstrated. | test/integration/node_tools/test_console.cpp |
| draft-202-RC66 | Define output-clear versus command-history/context behavior explicitly and reject stale presentation effects as needed. Define what Clear means for an in-flight operation, output generation, wallet context and history cursor; retain or suppress subsequent effects consistently. Not every retained field is a bug; fresh normal-user impact must be demonstrated. | test/integration/node_tools/test_console.cpp |
| draft-203-RC67 | Decide/document wallet-target semantics and bind long-lived operations to explicit session context. Clarify the intended UX/command/selection/capability contract first. The recorded observation does not by itself establish that another workflow machine is needed. Chunk 3 explicitly defers wallet-scoped console; active-wallet recall alone is expected and does not establish wrong-wallet execution. | test/integration/node_tools/test_console.cpp |
| draft-204-RC67 | Decide/document wallet-target semantics and bind long-lived operations to explicit session context. Clarify the intended UX/command/selection/capability contract first. The recorded observation does not by itself establish that another workflow machine is needed. Chunk 3 explicitly defers wallet-scoped console; active-wallet recall alone is expected and does not establish wrong-wallet execution. | test/integration/node_tools/test_console.cpp |
| draft-205-D205 | No active standalone fix priority for this stated scope Expected redaction behavior: secret arguments require re-entry; do not preserve original secrets to fix recall. Expected redaction behavior: secret arguments require re-entry; do not preserve original secrets to fix recall. | test/integration/node_tools/test_console.cpp |
| draft-206-D206 | Verify reachable user-visible impact before promoting Clarify the intended UX/command/selection/capability contract first. The recorded observation does not by itself establish that another workflow machine is needed. Source/inventory candidate; separate runtime reproduction required | test/integration/node_tools/test_console.cpp |
| draft-207-RC66 | Define output-clear versus command-history/context behavior explicitly and reject stale presentation effects as needed. Define what Clear means for an in-flight operation, output generation, wallet context and history cursor; retain or suppress subsequent effects consistently. Not every retained field is a bug; fresh normal-user impact must be demonstrated. | test/integration/node_tools/test_console.cpp |
| draft-208-RC68 | Use per-component memory budgets/virtualization and avoid repeated copied payloads. Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. 99 is not one bug. Model/delegate memory was measured; no end-to-end OOM was attempted. 208 repeats console scope 99-F1. | test/integration/node_tools/test_console.cpp |
| draft-208-RC72 | Bound bytes as well as rows or virtualize output. Primary remedy is bounded/incremental work, allocation budgets, virtualization or scheduling progress. State labels alone do not change the algorithmic/resource envelope. Draft 99 additionally contains input-length/row-budget validation subscopes. No end-to-end OOM; 208 is covered by this component scope. | test/integration/node_tools/test_console.cpp |
| draft-244-D244 | Verify reachable user-visible impact before promoting Repair the projection, formatting, notification, derived range or visible error/result mapping; no new workflow phase is required for the recorded scope. Source/inventory candidate; separate runtime reproduction required | test/integration/node_tools/test_console.cpp |
| draft-293-RC67 | Decide/document wallet-target semantics and bind long-lived operations to explicit session context. Validate the actual wallet/model/signing capability or supported descriptor shape at the existing entry/projection boundary, rather than inferring it from lock state or one broad flag. Chunk 3 explicitly defers wallet-scoped console; active-wallet recall alone is expected and does not establish wrong-wallet execution. | test/integration/node_tools/test_console.cpp |

## R39 — Peer selection and command owner

Prerequisites: R08,R36. Contracts: M28. Work packages: WP3.1.

Owned detail snapshot and independent captured command target; validate ban/disconnect at backend.

**Specific acceptance:** Reused IDs/selection changes; unknown result; multi-node ban persistence; held98 requires reproduction.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-098-D098, draft-302-D302.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-098-D098 | Hold claim; reconcile evidence or demonstrate actual lifetime/impact before promotion Held lifetime concern: demonstrate an invalid raw-pointer lifetime before recommending a fix. If confirmed, use valid ownership/identity; an enum would not repair dangling storage. HOLD: invalid lifetime and runtime UAF have not been demonstrated. | test/integration/node_tools/test_node.cpp |
| draft-302-D302 | Fix or follow existing issue using recorded reproduction; confirm current head Correct accepted search/date normalization and domain-boundary rules in the existing filter; preserve valid localized/displayed inputs. Merged-head reproduction recorded in document; observe latest narrowed scope | test/integration/node_tools/test_node.cpp |

## R40 — Native intent delivery queue

Prerequisites: R26,R27,R36. Contracts: M29. Work packages: WP9.3.

Implement actual OS adapters; bounded readiness queue; acceptance acknowledgment and reconciliation.

**Specific acceptance:** Native startup/open-file/arguments; duplicate delivery; shutdown; platform process tests; O30,O36.

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** draft-245-D245.

**Cross-consumer follow-up:** None assigned..

| Scope | Requirement and caveat | Existing integration owner |
| --- | --- | --- |
| draft-245-D245 | Verify reachable user-visible impact before promoting Implement the OS URI-delivery integration; downstream state machines cannot process a platform event that has no listener. Source/inventory candidate; separate runtime reproduction required | test/integration/payment_import/test_payment_import.cpp |

## R41 — Remove migrated monolith paths and compatibility shims

Prerequisites: R13,R14,R24,R25,R27,R31,R32,R34,R37,R38,R39,R40. Contracts: -. Work packages: -.

Audit every old method/member/callback/QML binding; remove only fully migrated paths; verify one writer and no leftover GUI backend work.

**Specific acceptance:** All old test requirements accounted; API/schema notification tests; negative architecture checks; no duplicate commands; FSM review: Compare migrated behavior against the earlier FSM inventory before removing old guards; preserve semantics without retaining duplicate QML business state.; Foundation: Enforce domain/execution/adapter/projection/QML dependency boundaries with appropriate build/include and runtime checks; audit every compatibility facade caller before removal..

**Commit recipe:** Reproduce each assigned defect and add an independent oracle in a temporary red worktree; land fix plus regression together. For extraction, migrate one owner and consumer at a time. Infrastructure uses contract tests with a real consumer; removal uses parity tests. Each landed commit passes its acceptance gates.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** Infrastructure/qualification task; its specific acceptance still requires executable tests..

**Cross-consumer follow-up:** None assigned..

## R42 — Complete finding acceptance and whole-application qualification

Prerequisites: R41. Contracts: -. Work packages: -.

Resolve every scope with evidence; complete unit coverage audit; full supported test/CI/platform matrix on combined series.

**Specific acceptance:** All valid existing tests pass; all valid findings protected; all36 schedules mapped; no unresolved blockers represented as done; FSM review: Audit producer coverage, pending-state progress assumptions and preservation obligations as well as state/transition coverage; do not claim unconditional bounded shutdown for noncancelable work.; Foundation: Complete architecture-foundation evolution rehearsals and producer/progress audit; measured coverage and real runtime evidence required, not document counts..

**Commit recipe:** Inventory and reproduce the baseline; change no application behavior. Record test/coverage/CI results and blockers. Do not invent a defect-fix commit for this qualification task.

**Evidence required:** exact parent/head/Core revisions, selectors and test counts, before/after results, coverage and schedule traces, lint/tidy/sanitizer scope and results, affected integration/process tests, and any blocker. Use the common worker report. No scope is accepted solely because its state appears in the catalog.

**Primary scope obligations:** Infrastructure/qualification task; its specific acceptance still requires executable tests..

**Cross-consumer follow-up:** None assigned..
