# Implementation progress

Current correctness follow-up (2026-11-15): the R24 contributed-signature audit
reproduced the same unfinished-signature installation gap in local SignOnly and
standalone PSBT Sign. The R21/R22 signing-result components are reopened while
shared typed validity/quarantine handling is propagated. Their previously
qualified export/admission behavior retains its evidence; whole-order completion
must not be inferred from those earlier receipts. See
[the follow-up record](SIGNATURE_CONTRIBUTION_FOLLOWUP_20261115.md).

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
| R20 | Focused conditional checkpoint integrated at644abba:139worker distinct bodies; merged387-body/31-static gate, app/QML lint and2IPC process scenarios pass. Receipt reason precision is user-deferred upstream; no disabled/rejected/relay inference. R42 pending |
| R21 | Focused acceptance restored after signing-contribution repair:374 component bodies at55a,17 static and4 process checks; composed229-body/14-static gate at a218 passes with app/QML lint. Scope095 remains a reconciled held allegation. R42 pending |
| R24 | Focused acceptance satisfied:177 contribution-verification identities with explicit source carries, shared374-body follow-up and composed229 at a218 pass. Unknown partial results remain separately recoverable; R09 device/Core limits and R42 stay explicit |
| R22 | Focused acceptance restored: prior158 export/admission cases retained; shared signing-contribution repair qualified at55a and composed229-body/14-static gate at a218 passes, app/QML lint and3processes. Existing two WalletOpening failures remain explicit baseline debt. R42 pending |
| R30 | Focused conditional checkpoint integrated at9c71709:317 current worker bodies plus explicit earlier-source carries, original025 parent failure and independent QR decoding; merged294-body/20-static/lint/QML and2actualIPC pass. Capability007 stays user-deferred upstream; R42 pending |
| R34 | Focused checkpoint integrated at644abba:119worker Qt bodies plus3X11 geometry scenarios; merged387-body/31-static gate and2IPC scenarios pass. Five scopes reconciled, known Qt/upstream limits retained; R42 pending |
| R35 | Focused accepted/integrated at eec5:95 composed bodies,14 pinned static entries, app/QML lint and3actualGUI processes pass; count-only manifest mismatch corrected by9-body rerun. Worker226 distinct source-specific identities and Mac controls retained separately. Public helper removes private wallet-directory access;066/263 caveats andR42 pending |
| R31 | Focused checkpoint integrated at6200e8c6:196 current Native/QML/Core bodies plus52 byte-verified earlier-source carries and the original two-process restart scenario pass. Pinned15-entry tidy, app/QML lint and original reorg parent failure recorded; R42 pending |
| R36 | Typed lifecycle, runtime prompts and network commands integrated at d48129bb:394 distinct successful bodies across source-qualified receipts,8 actual process scenarios and pinned static/app/QML checks pass. Shared peer/lifecycle composition is being checked separately; R42 remains open |
| R23 | Focused acceptance satisfied with R22 restored at e3508980. Public Node admission-read adapter at51c40864 has29 passing bodies/11 static entries/app lint. Combined626 distinct passing identities use explicit source carries; no required Core API gap. R42 remains open |
| R25 | Focused accepted at a218:127 isolated ordinary bodies including5 real Core/RPC cases;15 current static plus2 explicit CPP carries, app/QML lint and3processes; composed229-body/14-static gate passes. Nineteen assigned scopes reconciled. R42 pending |
| R32 | Focused accepted/integrated:194 current worker bodies plus205 explicit old-source carries; composed159 at799 and targeted229 at a218 pass. Pinned static/app/QML lint green. R42 pending |
| R26 | Focused accepted/integrated with cancellation correction520c6ef5:194 actual bodies pass after2 new queued-publication parent failures;10 affected pinned static entries/lint/IPC pass, earlier22-unit union and259QML carry retained. App/test/CMake/Core bytes match tested829; R27/R40 follow-ups andR42 remain separate |
| R27, R37–R40 | R27 clipboard/chooser/drop migration active; R39 repair02 runtime green pending detailed reconciliation/R36 composition; R37/R38/R40 independent implementation active. R42 final qualification remains open |
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
[R20 public receipt limitation](R20_PUBLIC_RECEIPT_LIMIT.md) has a user-approved narrow
upstream deferral; it does not authorize Core changes or automatic rebroadcast.

The [device-switch review](execution/R24-device-switch.md) records the required
held admission/process/Domain/GUI schedules and the remaining partial-result
qualification. R21 verifies finalized inputs; it does not establish every R24
partial-signature or physical-device lifecycle guarantee.

The [R20/R30 API decision](API_DEFERRALS_20261115.md) records the user-approved
upstream remainders and the required truthful app behavior. These narrow
deferrals do not authorize Core changes or waive other acceptance gates.

The [parallel implementation strategy](PARALLEL_IMPLEMENTATION_20261115.md)
keeps three workers on related families while the coordinator integrates and
dispatches frozen candidates. Each build tree retains one writer.
