# R35 revisioned onboarding and durable profile handoff

Implementation candidate: `52084a8faaa32b44d4764e4ad5a0cf6d38bf8ea4`, Core
`44901d8f64cb617714707bd025ecd83f108353c2` unchanged. Base:
`a2af8d230547e1f333becf8e1a4ad2bc4cb5955a` (R33/R09 prerequisites accepted).
Final pinned static passes13/13 (6 changed CPP+all7 fresh MOCs); QMLlint passes.
Exact delivery-head app lint is recorded in `R35/lint-final/RESULT.json`.
Exhaustive coverage, full suites,
sanitizers and platform/configuration matrices remain user-deferred to R42.

`onboarding::Flow` owns exact path/config revisions, positive preview/storage/
effective checks, immutable attempts and actual outcomes on Domain. The existing
scheduler runs effects on Core and publishes bounded GUI caches. Native cleanup
retains results and borrowed preparation dependencies through the drain barrier.
QML binds typed phase/pending/partial/uncertain/result properties and sends narrow
commands. Preparation is admitted once. A known saved profile can finish only its
remaining captured phases; an unknown result queries its original handoff and
never repeats the mutation. Unknown preparation requires explicit restart rather
than replaying node factories. Profile completion is separate from node/sync readiness.

Legacy conversion stages values without changing its source. Physical Core
replacement, exact readback, checked file/parent commit, GUI-path persistence and
guarded source cleanup form an ordered handoff. Per-source cleanup receipts survive
continuation; reset does not overwrite its original backup inside the same attempt.
This is not an atomic cross-file transaction or interprocess compare-and-swap with
unrelated Qt writers. Source changes observed before cleanup remain recoverable.
No Core/Qt source or storage-format changes.

Startup retains typed config/profile errors and reports a visible failure with a
real failure exit, instead of opening an unusable wizard or choosing another default.
The onboarded marker stays false until actual startup readiness. Captured scalar
QSettings namespaces keep delayed work in its original profile; legacy Mac native
reads use the actual writer's organization domain. Qt's INI cache can lose an earlier
instance's FormatError, so a bounded unique snapshot is validated by Qt's existing
parser. The malformed source remains intact; no replacement native component/parser.

## Focused evidence

All artifacts are under `.validation/qt6-r11-r40-20261114/R35/`.

| Check | Evidence/result |
| --- | --- |
| Linux Native/Core | `bazzite-09/artifacts/green/results/report.json`:105/105 bodies. `bazzite-11/artifacts/green/results/report.json`:new configured-profile1/1. `bazzite-07/artifacts/green/results/report.json`:43 unchanged startup-policy/Desktop/preferences bodies, source-qualified by `evidence/repair-09/CARRY_QUALIFICATION.json`. Total149 distinct Linux bodies, no repeated-body inflation. |
| macOS | `macos-11/artifacts/green/focused/report.json`:8/8 declarative QML bodies. `macos-09/artifacts/green/focused/report.json`:actual NativeFormat1/1, additional platform evidence for an identity also tested on Linux. |
| Process lifecycle | `bazzite-09/artifacts/green/{process,held-config}.{stdout,stderr}` plus exits:exact reset backup retained; DNS failure exit1/markerfalse, then recovery to actual Core readiness/proxyfalse/exact550MiB/exit0; held config close/drain exit0. Three existing Qt automation-socket scenarios in an IPC-enabled build. No claim that full `interface_gui.py`/sanitizer matrices ran. |
| Startup children | Eight source09 children plus configured-profile source11 child show the actual visible error, matched diagnostic, app exit1 and wrapper exit0; raw `startup-child-probes` retained. |
| Original negatives | Linux parent07:8 startup+1 namespace failures/1 domain control; Mac parent07:domain1fail; parent11:configured-profile1fail; root parent01:2 source-retention failures/1 preview control. Actual parent reset process failure also retained. |
| Static/lint | Official app `lint-09` passes with qualified Core449 image, actual462-file app scope. Initial `local-01`43/44 tidy and fixture-only failed `local-02` are retained. `local-03` final pinned tidy13/13 and official QMLlint pass; exact delivery-head app lint receipt is `lint-final/RESULT.json`. |

Exact commands/manifests are retained. Shared `test/run_focused.py` uses
`native-07.json` (unchanged43), `native-09.json` (105),
`configured-profile-11.json` (1), `qml-11.json` (8). No custom runner or empty passes.
All failed07/08/09 runs remain intact. Macro/profile/full-suite qualification is R42.

## Scope map

| Scope | Exact disposition/oracle |
| --- | --- |
| 066 | Preserve explicit deferred/coverage classification; no stronger ordinary-user impact established, no standalone fix claimed. |
| 068 | Actual CLI/config conflicting-network parent failures become visible diagnostic+failure exits: StartupProfileTests `cli-networks`/`config-networks` rows. |
| 070 | Actual reset process and `capturedResetKeepsOriginalBackupAcrossCompletion` require exact original backup after continuation. |
| 072 | Actual Mac NativeFormat parent/head `legacyNativeOrganizationDomainMatchesWriter`; Linux is a control. |
| 074 | Distinct `config-return-error` child checks actual InitConfig status; DNS failure/recovery process checks retained marker/answers/readiness/exit. |
| 084 | Distinct `configured-unavailable` child retains exact bitcoin.conf; `saved-unavailable` separately covers remembered unavailable profile. |
| 085 | Actual `missing-absolute`/`missing-relative` entry-point children report controlled errors without assertion. |
| 091 | `failedProfileHandoffRetainsLegacyMigrationSource` covers actual write and complete-batch rejection; exact source/target tests guard cleanup after staged conversion. |
| 263 | Preserve inventory-candidate provenance. Present-empty/wrong-type/malformed public cases distinguish damaged preference from valid unset/default; no blanket promotion of historical scenarios. |
| 264 | Same durability owner as091: exact replacement/source, idempotent cleanup, changed-target recovery refusal and remaining-phase continuation tests. |
| 262 cross | Preserve R33 receipts; `capturedResetRejectsChangedLegacySource` plus actual reset process covers onboarding consumer. |

Limitations: unknown persistence retains recovery state and never authorizes blind
replay. Native Windows directory durability/platform qualification remains R42.
Core's void directory-commit helper provides no checked receipt; this app adapter
checks its own Native file/directory commit. An upstream checked receipt would
simplify it. Independent legacy writers have no shared conditional-delete
transaction; no stronger guarantee is claimed. Runtime prompts/final node exit
status belong to R36.
