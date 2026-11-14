# R11–R40 validation and build distribution

This is the current implementation-step validation contract. It consolidates
the user's approved focused-testing, distributed-build and final-qualification
instructions. It supersedes earlier per-step requirements to run exhaustive
coverage, full matrices or sanitizers. Those requirements remain due at R42.
The same focused cadence applies to R41 cleanup before final qualification.

R10's implementation/focused checkpoint is complete. R11 is in progress on
`work/qt6-r11-r40-20261114`; subsequent orders advance after their prerequisites
and focused gates pass. Evidence lives under workspace
`.validation/qt6-r11-r40-20261114/`, with one directory per order.
The Core pin remains `44901d8f64cb617714707bd025ecd83f108353c2`.
All 318 findings, 329 regression scopes, 43 orders and 36 schedules remain tracked.
Implementation progress and final qualification are separate statuses.

## Optimizations already integrated

- QML performance commit `992bf392156a8e707ec29901e5acd3d0fd235e48` is integrated
  with the R10 conflict adaptations. All 61 component files have separate CTest
  entries, scheduling hints and per-process settings isolation. Optional live
  development resources avoid rebuilding for edits to existing QML/JS files.
  Default embedded resources remain the qualification mode. Assertions,
  animations and warning policy are preserved.
- Wallet-enabled integration uses nine feature groups and ten CTest entries, with two sending
  shards and up to four independent groups in parallel. Each runtime case keeps
  its own application/regtest profile. Selected sending/PSBT cases use private
  copies of a prepared funded wallet; setup-specific cases keep fresh setup.
  Watch-only funding improvements are also adopted. See the [implementation and
  historical measurements](../doc/integration-parallelization.md); those timings
  are not a benchmark or full-suite pass for the current refactor head.
  The history includes partitioning `f9ecd8717b3`, prepared snapshots
  `f89ebb71f70` and the refactor adaptations `caeee302dda`/`529f0f95084`.
  Wallet-disabled builds retain the three applicable node feature groups.
- `test/run_focused.py` consumes JSON selections for existing unit, QML and
  integration binaries. It records actual body identities/counts and raw
  process results, rejects empty/mismatched runs and continues independent
  selections after a runtime failure. Unit class discovery is cheap and runs
  once per invocation. `ci/clang_tidy.py --files` selects affected compilation
  units while retaining app MOCs and pinned checks. Use the [shared tooling
  guide](../doc/focused-testing.md), not copied per-change runners.

## Gate for each implementation step

Before extraction, check every assigned confirmed finding against its original
failure oracle. A generic successful-ordering test does not establish coverage
of a failed Core call, a detached consumer or premature admission. Reuse existing
regressions where their trigger matches; preserve non-defect dispositions and
do not promote unverified source candidates. Add a focused RED proof for a
confirmed uncovered defect before implementing its fix.

Give each shared production module one implementation owner. Delegate disjoint
tests, consumers and validation concurrently. Batch the remaining known fixes
into a coherent snapshot before its consolidated gate, instead of freezing and
rebuilding after each small repair. Artifact packaging and auxiliary audits must
not serialize source work or block independent behavioral checks.

Delegate a complete owner/consumer/regression slice with its file boundary,
acceptance oracle and shared test manifest agreed at assignment. Keep one final
handoff per task; interrupt for genuine blockers or decisions rather than routine
microtask approval. Advance independent orders whose stated prerequisites are
already accepted in isolated branches, then integrate their changes through one
owner. A pending unrelated earlier order does not create a new prerequisite.
Freeze one coherent source per acceptance round and run disjoint validation lanes
in parallel. Never place competing writers in the same module or build tree.
A ready handoff must state whether the changed translation units actually compile.
Use an available isolated focused compile or pinned analysis lane to catch basic
compiler errors before the combined gate; a static diff check alone is not a
compile claim. Preserve all known failures in that handoff rather than sending
small partial fixes as separate integration requests.

1. Map the step's issue IDs, entry points and required schedules to concrete
   executable selectors in a data manifest. Keep the original failure oracle,
   valid existing assertions and any recorded non-defect disposition. Reuse the
   normal application/integration fixtures and shared manual executor/clock.
2. Build affected targets incrementally in an ordinary pinned Linux profile.
   Add a targeted configuration check when the changed code affects that
   branch: for example, moving a wallet-only type into a common target requires
   checking compile-time wallet-disabled support. A routine wallet-only change
   does not trigger the entire Qt/platform matrix.
3. Run focused behavioral regressions during implementation. At checkpoint
   readiness, include the affected cross-consumer and wiring tests. Async or
   lifetime work retains manual ordering tests for relevant admission, Core
   execution, Domain completion, GUI publication, stale/duplicate results,
   rejection and close/drain boundaries. Add relevant real-thread checks and
   actual Core/RPC integration; manual scheduling alone cannot prove them.
   Use the live QML mirror for edit loops; exercise affected production wiring
   with normal embedded resources at the checkpoint, without replaying all61
   component files or both resource modes for every change.
   Run focused lifecycle/IPC cases when those paths change. Keep full process
   sweeps for R42.
4. Run app lint on the reviewed range and pinned clang-tidy on new/affected C++
   units, retaining MOCs. Review header dependencies and expand the selected
   compilation units when required. A native Mac compiler check does not replace
   the pinned Linux CI version/plugin. Tests of runner plumbing do not replace
   the changed feature's behavioral tests.
5. Record app/Core revisions and dirty-input hashes where applicable, host,
   toolchain/image, configuration/resource mode, commands, actual selected and
   executed bodies, pass/fail/skip counts, raw diagnostics and actual exits.
   Discovery is not execution; empty or incomplete selections cannot be reported
   as passing. Reuse earlier passing evidence only with explicit source/tool
   identity and dependency review. Never merge profiles from different ELFs.
   Start focused runtime checks when their affected targets build. Source/Core
   identities, actual build commands/exits and binary hashes are sufficient
   per-step provenance; auxiliary object inventories must not block independent
   tests. Detailed compiler/profile/DWARF mapping audits remain at R42.
6. Resolve introduced ordinary correctness failures before advancing. Record
   "implementation/focused checkpoint complete; final qualification pending
   R42" when this gate passes, then proceed to the next order. Do not repeat
   unrelated suites or coverage audits at that boundary. This status does not
   close the canonical finding inventory or claim final matrix qualification.

A required architectural/API problem that cannot be solved through allowed
public interfaces still requires a note, notification and user input before
dependent work continues. Core remains unchanged. Continue independent work.
Native Qt component defects retain the standing document/defer policy.

## Parallel host allocation

These are starting caps from the established hosts, not guaranteed future
capacity. Check current memory, disk, access and existing jobs before dispatch;
lower concurrency when needed. Use all available hosts for independent useful
work, without manufacturing four full rebuilds or counting duplicate tests as
additional coverage. Every host has already completed work in the distributed
R10/tooling validation; a full new build on every host is not required per step.

| Host/account | Normal implementation-step role | Build concurrency cap | Existing ccache cap |
| --- | --- | ---: | ---: |
| `johnny@gameboy.local` | Primary pinned Linux affected-target build, focused native tests and actual integration rows | 6 | 32GiB |
| `johnny@bazzite.local` | Independent ordinary build/configuration when relevant; disjoint QML files and tooling checks | 4 | 16GiB |
| `johnny@johnnys-Virtual-Machine.local` | Relevant native Mac/Qt checks; cached embedded/dev QML or tooling work otherwise | 3 | 24GiB |
| Local workspace | Pinned app lint/tidy and independent focused tests/integration | 2 per heavy job; at most 2 heavy jobs | 64GiB |

For independent test processes, start with at most 4 on Gameboy/Bazzite and 2 on
Mac/local, subject to memory and other work. Count simultaneous builds and test
processes together when assessing memory. The shared manifest runner remains
serial within one invocation; distribute separate manifests or use the existing
CTest groups/files for safe parallelism. Do not run competing writers against
one build tree.

Use task-owned disk-backed directories, warm compatible builds and compiler
content checking. Preserve source hashes and unchanged input timestamps when
transferring a new checkout so Ninja does not rebuild unchanged inputs. Verify
the exact app/Core snapshot after transfer. Native AppleClang/Qt results retain
their own scope; only matching pinned tools qualify the corresponding CI gate.

Collect small logs/XML/JSON, commands, source identities and exits locally.
Keep large binaries, caches and raw profiles on the build host unless a failure
needs them. Do not repeat detailed profile/mapping audits or transfer full ELFs
for every small edit. Use short real temporary paths for IPC. Preserve existing
jobs and useful failure evidence; continue independent checks if one host is
unavailable. Cache growth requires evidence of capacity pressure. The known
continuous-coverage ccache parser limitation is separate from capacity; do not
remove compiler flags or force a broad cache bootstrap for a small repair.

Earlier dispatch records assigned ASan to Bazzite and TSan locally. Those are
historical R10 jobs. Routine sanitizer jobs are now deferred on every host.

## Deferred final qualification

R42 owns the final combined head's exhaustive reachable handwritten line/branch
coverage and retained legacy-ledger accounting; full unit/QML/integration and
process suites; detailed compiler/profile mapping audits; full repository
lint/tidy; supported Qt/platform/build/wallet matrices, static depends,
compile-time wallet-disabled and runtime `-disablewallet` journeys; native
Windows and Mac qualification; pinned ASan/LSan/UBSan/integer and relevant
TSan/MSan configurations with required dependencies and repairs.

That includes IPC-enabled GUI startup/shutdown, raw stderr and actual exit
inspection, plus the separate Core IPC/integration obligations. An ordinary
CTest pass does not qualify those lifecycle or sanitizer scopes.

Preserve existing failures and explicitly accepted deferrals. The native Qt
picker's affected TSan result stays unresolved. Mac font/Qt6.11 diagnostics and
warnings outside the current QML-console CTest regex remain recorded; a focused
component pass is not a full compatibility pass. No silent exclusions, broad
suppression, Qt replacement component or new Core exception is authorized.
