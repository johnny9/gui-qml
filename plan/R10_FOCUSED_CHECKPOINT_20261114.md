# R10 focused progress checkpoint

R10 implementation and its focused ordinary progress gate are complete at tested
snapshot `c43fa40ee418655f0800fc92da29c482f836ed3d`, with unchanged Core
`44901d8f64cb617714707bd025ecd83f108353c2`. Final qualification remains pending
R42 under the [user-approved cadence](QT6_VALIDATION_CADENCE_20261114.md).
R11 implementation has not started. No shared finding or schedule register is
closed by this checkpoint.

The branch includes the [structured result](evidence/R10_FOCUSED_CHECKPOINT_20261114.json)
and [original scope/schedule provenance](evidence/R10_SCOPE_PROVENANCE_20261114.json).

The application branch parent is `85834d8780f12abb6946c5c1fb46769941ddc8dc`.
The current-production batch was frozen as source65,
`80d4af9d875e98a22eda2db867ed97bd244f7452`. Sources66–68 change test fixtures
and plan documents only. All 232 tracked handwritten native production files
match source65 by SHA-256; Core remains clean. Source68 also retains the exact
QML sources from source66. A later documentation/checkpoint commit must retain
this distinction between its Git HEAD and the tested snapshot.

## Focused results

| Tested snapshot | Executed bodies | Actual result |
| --- | --- | --- |
| source68 | Three Options manual timing cases | 3 passed, exit0, no skips |
| source68, same ELF | O04 opening/notification/reopen cases 17; O27 close-consent cases 3 | 20 passed, both processes exit0, no skips |
| source67 | Corrected settings/controller cases9 | 6 passed; three failures subsequently repaired and passed on source68 |
| source66 | Corrected FormControls animation case | 1 passed, exit0, no unexpected QML warning |
| source65 | Selected native bodies343 | 334 passed; nine fixture failures subsequently covered by the source67/68 results above |

The source68 ELF SHA-256 is
`5d71cb8e7cf6c1db247d9ad1ec254e0358babc9246134e02b20856d670a0c000`;
its build ID is `3f9b1727a5f9a271f75a785265a4f255189ac6ca`. The three-case
incremental configure/build/run took about 29 seconds on gameboy.local. The
20-case consumer check reused that ELF without rebuilding. These are separate
focused runs, not a full343-body or full-suite pass on source68.

Manual fixtures now separate Core effect execution, Domain outcome consumption
and GUI publication. They distinguish idle transport from live retained cleanup
leases and keep native/configuration owners alive through final drain. The
Onboarding schedule drains eligible Domain work before releasing the intended
disk effect. Storage remains pending through derived projection publication.
Latest intent, stale-result suppression, prune choice, exact signal counts and
final quiescence assertions are preserved. The color assertion waits for the
existing animation with its existing watchdog. No production behavior or Core
code changes in this final fixture repair.

Source66's native build failed before executing any body because an obsolete
test helper was unused under the pinned warning-as-error policy. The helper was
removed in source67; flags and check definitions were preserved. All earlier
failures remain in their original artifacts.

Changed-scope app lint passes on source68. Pinned Clang23/Bitcoin-plugin tidy
passes the one revised handwritten test unit plus required app MOC aggregates;
source67 already passed both changed test units and source65 passed its 35-unit
selection. No older tidy version is substituted. The lint checkout's branch,
HEAD and clean app/Core status were checked after the driver.

## Evidence and commands

All paths below are relative to the retained workspace task
`.validation/qt6-r10-acceptance-20261114/`:

- `NATIVE_GAP_SNAPSHOT_65.json` through `NATIVE_GAP_SNAPSHOT_68.json` retain
  immutable source identities, input hashes and exact selectors/counts.
- `evidence/distributed-validation-20261114/gameboy-hot-68/` retains the actual
  build/test command manifests, XML, stdout/stderr, exits, source-carry checks
  and the separate O04/O27 result. Prior runs remain in `gameboy-hot-66/`,
  `gameboy-hot-67/` and `gameboy-native-65/`.
- `evidence/lint-app-68/RESULT.json` records
  `python3 ci/lint.py --no-build --scope app`, the nonempty source65-to68 range,
  pinned image and post-lint source checks. The driver ran in a private clone.
- `evidence/focused-tidy-68/` records
  `R10_TIDY_PREVIOUS=67 python3 run_focused_tidy.py 68`, its nonempty scope,
  unchanged check definitions and pinned plugin/toolchain provenance.
- `evidence/R10_FOCUSED_CHECKPOINT_AUDIT.json` maps all eight original R10
  scopes and O04/O27 to existing concrete selectors and historical evidence.
  It predates the final three passing rows and does not close canonical scopes.

The performance changes from commit
`992bf392156a8e707ec29901e5acd3d0fd235e48` are integrated: per-file QML CTest,
parallel execution and optional development resources, preserving embedded
default resources, settings isolation, animations and warning/coverage guards.
Source65 validation used all four authorized hosts. Linux ordinary checks,
Bazzite embedded/development component checks, local lint/tidy/thread checks and
native ARM Mac builds retain separate source/toolchain receipts. Their old
partial/failing results are not relabelled as full current passes.

## Final qualification and unresolved issues

R42 owns exhaustive 100% reachable handwritten line/branch coverage, detailed
compiler/profile mapping audits, full suites and IPC startup/shutdown sweeps,
the complete Qt/platform/build/wallet matrix, native Windows, and the required
pinned sanitizers plus repairs. Existing ledgers and all 318 findings and 329 scopes,
43 work orders and 36 schedules remain preserved. Current complete inventories
project 3769 native bodies and 1021 QML bodies; those are not executed full-suite
pass counts for source68.

Mac Qt6.11 enabled-property warnings and minimal/offscreen font stderr failures
remain unresolved for final compatibility qualification. The native Qt picker
race stays documented/deferred, with its affected TSan result unresolved; no Qt
patch, replacement chooser or blanket suppression is introduced. Core interface
limitations and the notify-and-wait rule remain binding.

Ccache capacity is ample. The pinned ccache parser rejects
`-fprofile-continuous` in the coverage build. A task-only adapter preserving that
flag passed bounded compile/runtime/compiler/diagnostic controls. Deployment is
reserved for a needed broader build because changing the launcher can invalidate
the hot Ninja commands. This checkpoint does not claim full cache utilization.

The 11 unrelated changes and original HEAD in the older source worktree were
rechecked and preserved. Shared planning/evidence registers and Core were not
modified. This checkpoint does not authorize a push.
