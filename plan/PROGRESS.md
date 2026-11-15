# Implementation progress

The [approved cadence](QT6_VALIDATION_CADENCE_20261114.md) separates implementation
progress from final qualification. The copied CSV registers in `tracking/`
retain their historical provenance; this file records later branch-local
progress without changing shared finding or schedule registers.

| Orders | Current progress |
| --- | --- |
| R00–R08 | Previously accepted on the amended base; original evidence retained |
| R09 | User-accepted conditional app-side prerequisite; upstream limitations retained |
| R10 | Implementation/focused ordinary checkpoint complete; final qualification pending R42 |
| R11 | Implementation/focused checkpoint complete:274 behavioral bodies,5 IPC scenarios; final qualification pending R42 |
| R16 | Focused checkpoint integrated:227 native,19 Core integration,17 Linux QML bodies across recorded snapshots; final qualification pending R42 |
| R17 | Focused conditional checkpoint integrated:177 worker ordinary bodies plus25 Mac; merged265-body gate passes. D048 remains unresolved, requires Core interface changes, with user-accepted deferral |
| R14 | Focused checkpoint integrated:310 bodies across preserved snapshots,26 pinned static entries andlint pass; merged375-body gate plus9 static entries/QMLlint/applint pass. Final qualification R42 |
| R28 | Focused checkpoint integrated: 51 native, 56 Activity QML and one public Core/RPC body across recorded inputs; final qualification pending R42 |
| R18 | Focused app checkpoint integrated:84 focused bodies pass on combined ce039; parent failures reproduced; partial locks/Cancel, external lock, maturity and bounded5,000-coin projection covered. Core038 itself remains upstream with an app refusal workaround; R42 pending |
| R12 | Focused checkpoint integrated:327 distinct bodies across recorded worker snapshots; merged50-body gate, QML lint, pinned tidy and app lint pass |
| R15 | Focused checkpoint integrated:135 worker Linux bodies and31 supplemental Mac; merged206 actual behaviors pass, manifest count corrected by affected7-case rerun plus1newCore regression (207 distinct). QMLlint,11pinned static entries and app lint pass; R24 signing handoffs/R42 qualification pending |
| R13 | Focused checkpoint integrated:172 worker QtTest bodies plus2 compatibility scenarios pass; same172 QtTest bodies rerun in combined339-body gate with pinned23-entry tidy, QML/app lint and5 IPC/lifecycle scenarios |
| R19 | Focused checkpoint integrated:42 worker bodies,3 exact-parent REDs and merged248-body/17-static gate pass; source-refuted RC80 retained, R42 pending |
| R29 | Focused checkpoint integrated:255 distinct worker bodies (135 native,95 QML,25 Core); parent25 executes20 intended failures and5 controls. Pinned static/QML/app lint pass. Combined122 native/Core bodies pass with R21; R42 remains pending |
| R33 | Focused checkpoint integrated at3806a197:272 distinct worker Qt bodies plus2process scenarios with explicit source carry. Merged246 bodies,12pinnedstatic,app/QMLlint and startup failure/recovery pass;30scopes reconciled, R42 pending |
| R20 | Candidate6f5100c9:139 distinct focused bodies and pinned lint/tidy/QML checks pass. Missing receipt reason distinction is user-deferred upstream; combined integration gate pending; no disabled/rejected/relay inference |
| R21 | Focused checkpoint integrated atffe3637d:270 R21 bodies plus122 R29 composition bodies pass (392 distinct combined);16 pinned static entries,app/QML lint pass. Scope095 held; R24 lifecycle and R42 remain open |
| R24 | Compatibility guard and error-return repair integrated: prior157 bodies plus later focused33-body/10-static/actualCoreHWI gates pass at recorded inputs. Six new held-device schedules pass again in merged246. Whole R24 Domain/lifetime/partial/timeout gate remains open |
| R22 | Independent implementation starting on R21/R20 candidates; R20 API distinction is user-deferred; combined prerequisites and its own focused gates remain |
| R30 | Reservation/request editor work active; exact receive-type capability filtering007 is user-deferred as an upstream improvement; other implementation/gates remain |
| R34 | Independent display/geometry implementation active on accepted R33/R07; focused gate pending |
| R35 | Isolated source prepared on3806a197; durability/migration/path audit active, no acceptance claim |
| R23, R25–R27, R31–R32, R36–R40 | Authorized according to actual prerequisites; remaining implementation/gates pending |
| R41 | Cleanup pending |
| R42 | Final coverage, full suites/matrices, native Windows and sanitizers pending |

The [R10 checkpoint](R10_FOCUSED_CHECKPOINT_20261114.md) records exact tested
app/Core identities, nonempty counts, commands, artifacts and unresolved issues.
The program remains 318 findings, 329 scopes, 43 orders and 36 schedules.

The [R11 checkpoint](R11_FOCUSED_CHECKPOINT_20261114.md) records its exact tested
inputs, scoped reuse, resolved failures and remaining upstream/platform limits.

The [R16 draft checkpoint](R16_SEND_DRAFT.md) records its new typed draft/mode
interface and focused validation; final platform/coverage/sanitizer status stays
pending. Independent steps advance while accepted changes are integrated.

The [R28 integration record](R28_ACTIVITY_READER.md) retains exact inputs,
original comparisons and the passing 151-body Send follow-up. The
[R17 preview limitation](R17_PUBLIC_PREVIEW_LIMIT.md) records the accepted upstream
deferral; independent steps continue.

The [R15 identity clarification](R15_SIGNER_IDENTITY_SEMANTICS.md) distinguishes
key-origin fingerprint hints from physical devices and wallet identity. R15
preserves Core's existing single-signer creation policy; discovery freshness
and truthful provenance are the app's responsibility.

The [R13 checkpoint](R13_RESTORATION_CHECKPOINT.md) and
[R18 checkpoint](R18_COIN_SELECTION_CHECKPOINT.md) retain the combined focused
receipt at ce039. The [R18 upstream note](R18_UPSTREAM_NOTES.md) records the
actual Core lock-persistence defect and the public-interface guard's limits.
[R24 handoff requirements](R24_SIGNER_HANDOFF_REQUIREMENTS.md) require compatible
and incompatible replacement tests against the exact reviewed keys/transaction,
not a matching fingerprint alone. Creation swaps pass; signing acceptance is pending.

The [R19 checkpoint](R19_PREPARATION_CHECKPOINT.md) records background-owned
preparation and exact scope dispositions. The [signer repair](R24_SIGNER_GUARD_CHECKPOINT.md)
records key compatibility without physical-device identity assumptions. The
[R20 public receipt limitation](R20_PUBLIC_RECEIPT_LIMIT.md) is a separate pending
user decision; it does not authorize Core changes or automatic rebroadcast.

The [device-switch review](execution/R24-device-switch.md) records the required
held admission/process/Domain/GUI schedules and the remaining partial-result
qualification. R21 verifies finalized inputs; it does not establish every R24
partial-signature or physical-device lifecycle guarantee.

The [R20/R30 API decision](API_DEFERRALS_20261115.md) records the user-approved
upstream remainders and the required truthful app behavior. These narrow
deferrals do not authorize Core changes or waive other acceptance gates.
