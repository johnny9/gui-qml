# R41 focused acceptance

R41 satisfies the implementation-step gate at app
`90abf2fdd928bcdf8fc4ce3a831d2a2538d70a24`, stock Core
`44901d8f64cb617714707bd025ecd83f108353c2`. R42 final qualification remains open.
Evidence: workspace `.validation/qt6-r41-r42-20261115/R41/ACCEPTANCE.json`.

The audited cleanup removes unused wallet/PSBT helpers, the synchronous unlock
shim, obsolete loaded-wallet schema, duplicate Node peer/traffic facades,
settings/console aliases and bookkeeping, an unused settings page and redundant
QML import state. Live compatibility, persistent formats, original outcomes and
guards remain with their tested owners. Retained facade cycles and callers are
accounted for in the wallet, node and QML worker reports.

The composed evidence contains **211 distinct passing bodies**: 171 native,
34 QML and six actual public Core integrations; zero body failures/skips.
An initial selector capitalization error executed no body and exited 1. Its
corrected original case passed separately against unchanged binaries; the
original failure and a read-only CTest-log adaptation remain recorded. The
211-body count does not count either failed empty selection as a pass.

App lint passes across the full R41 range. Pinned Clang23/Bitcoin-plugin tidy
passes 25 selected handwritten compilation units and seven fresh app/test MOC
aggregates. Both shipped QML compatibility variants pass all 260 files with
zero warnings/errors. Real generated-schema and typed-consumer negative checks
enforce selected projection/include boundaries; runtime/manual/physical-thread
and Core tests supply their separate behavioral evidence.

Seven pre-existing assertions were reproduced before correction: two wallet
fee/output checks and five setup-commit checks. The genuine fee-cause defect is
repaired in a separate signing follow-up without permitting protected effects.
Stale fixtures now use public ownership and current revisioned profile semantics,
retaining fee authority, output visibility, partial receipts, retry, original
shutdown outcomes and no-replay assertions. Parent failures remain in evidence.

This ordinary focused gate uses the approved O0/g0 cadence. It does not certify
full default suites, exhaustive coverage, platform/configuration matrices or
sanitizers. Those obligations now move to R42. Approved Core/API and Qt picker
limitations remain unresolved. Core/Qt and the original unrelated working
changes are unchanged; shared finding/schedule registers are untouched.
