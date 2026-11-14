# R28 Activity reader and integration

The initial integration commit is `053886262033ea5eec2d3cc7df3e133d078be45e`,
from worker source `226e18bc135377fe601a09d153c59d708d97bb7b`.
Core remains clean at `44901d8f64cb617714707bd025ecd83f108353c2`.
The focused checkpoint is complete subject to the final documentation lint
record. Final qualification remains due at R42.

The value-only activitystore reader uses public Wallet calls on the original
opening's Core lane. Immutable frames retain separate history, transaction,
label, status and detail revisions. Domain computes projections/diffs; Qt
getters read cached values. Relevant stale successes/errors retain pending work.
Poll ticks coalesce without inventing Core changes or starving a held read.
Detail views retain original opening/transaction, private result/error and
independent interest.

| Boundary | Ownership/progress |
| --- | --- |
| Wallet notifications | Session retains original opening and Native revision; bounded transaction keys fall back to a full read on overflow |
| Read/result | Existing injected Core/Domain lanes, immutable values and reserved Native cleanup; no GUI wait |
| GUI publication | Exact active job/mailbox and relevant revision vector; a stale wakeup carries no old patch authority |
| Bounded Qt apply | Stable rows; reentrant deletion stays pending; loading remains pending through queued work |
| Retry/error | Current error remains visible with rows; retry is an explicit intent |
| Close/view loss | Stop admission/polling, retain accepted work/cleanup, ignore obsolete presentation |

Evidence is under workspace `.validation/qt6-r11-r40-20261114/`:

- `R28/evidence/R28_FOCUSED_VALIDATION_B33.json`: 49 full Activity native bodies,
  56 full Activity QML bodies and one independent public Core/RPC maturity/reorg
  case pass, no skips. Targeted selections overlap those bodies. Bazzite ordinary
  wallet/IPC build; mandatory Clang 23/plugin analysis ran locally because its
  image lacks tidy 23.
- `integration-02`: combined Activity 49, Send draft 28 and Activity QML 56 pass.
  Full Send QML exposes four failures also reproduced before R28; follow-up
  integration changes repair them.
- `R28/red-native-01`: strict pre-R28 production `e4d4dd22` plus test-only original
  oracles at `506d748ac6058332708a7880d0a0ccee67711986`. Obsolete failed reload
  publishes an error; the error-with-existing-rows QML oracle also fails. Row
  retention and replacement-total controls pass. The stale-error case also
  records a 10-second parent teardown failure; no clean-parent teardown claim
  is made. The positive test now disconnects its stack-bound observation before
  fixture teardown. This parent comparison was reconstructed after implementation.
- `integration-03`, source `e02d491f93758a97b3a0dbdf07b706a552080621`: all 51
  Activity native bodies pass, including duplicate wakeup/tombstone and
  notification-during-apply cases, plus the exact error QML consumer. Production
  QML lint passes 244 files per variant with zero warnings/errors; raw
  compatibility notes remain. Five Send fixture assertions needed to distinguish
  imports that actually replace a private note; failures remain recorded.
- `integration-04`, source `23666faf20ec89315a908e0e0b5d1e3a2e79346a`: all 151
  Send QML bodies pass after the input guard and explicit overwrite-flow fixture
  migration. Original expected imported notes/messages remain asserted. Only
  that test file differs from integration-03; unchanged Activity/static evidence
  is reused explicitly. Final integration tidy/lint logs live here.

The PSBT follow-up captures the model's exact string selectionToken and rejects
held import presentation when selection is pending or changes. Recipient revision
alone cannot represent input selection. Its mock now models that token/pending
state; a count-only notification preserves the pending import.

Original RC58 list/detail/error/retry/deletion oracles remain distinct. RC12
replacement arithmetic has passing strict-parent controls; no new arithmetic
repair is claimed. Drafts 165/168/170/171/173/260 remain unpromoted candidates;
174/277 retain the R32 consumer follow-up. The worker's original
`R28_SCOPE_ORACLE_MAP.json` remains historical; parent evidence adds the missing
comparisons and O22 cases. Other O07/O22 log/peer consumers belong to their own
orders; R42 owns the full cross-consumer sweep. Canonical registers are unchanged.

Full coverage/suites, platform/configuration matrices and sanitizers remain due
at R42, including IPC GUI lifecycle and the unresolved native Qt picker TSan
result. No Core/Qt change, external publication or push is part of this work.
