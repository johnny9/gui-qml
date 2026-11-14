# R10 acceptance work

**Current checkpoint:** R10 implementation and focused ordinary checks are
complete at `c43fa40ee418655f0800fc92da29c482f836ed3d`, Core44901 unchanged.
The [checkpoint report](R10_FOCUSED_CHECKPOINT_20261114.md) records the three
repaired timing bodies and 20 current-executor O04/O27 bodies, all passing,
alongside source-qualified earlier results. Final qualification remains pending
R42. R11 implementation is next and has not started. Earlier status entries
below retain their historical source identities and acceptance policy.

The user also approved moving exhaustive coverage proof, full suites/matrices and detailed compiler/profile audits to R42; native Windows is deferred to final qualification. R10's current progress gate is focused ordinary correctness/timing/integration plus changed-scope lint/tidy. Its final coverage, sanitizer and matrix qualification remains open. Mac Qt6.11 enabled-property warnings and minimal/offscreen font failures are recorded compatibility findings for that final stage. No warning suppression or false platform pass is introduced.

Current user cadence defers routine sanitizer builds/runs and sanitizer-only repairs until implementation work orders are complete, with final qualification at R42. R10 proceeds toward its ordinary-check/coverage checkpoint; sanitizer status remains pending final qualification. Preserve completed sanitizer evidence and use ordinary incremental checks for the current fixture corrections. See [the recorded decision](QT6_VALIDATION_CADENCE_20261114.md).

The 2026-11-14 user instructions add QML performance commit992bf392 and parallel
validation builds on gameboy.local, bazzite.local, johnnys-virtual-machine.local
and this host. Performance integration and the executor/settings/UI repair
batches are implemented and statically reviewed; their combined working bytes
are unvalidated. All three hosts are accessible with the user-confirmed johnny
account. Initial authentication/name-resolution failures remain retained. Gameboy
and bazzite have imported the exact pinned coverage/ASAN image content; complete
runtime Config/RootFS/platform identities match. Their engines expose platform
config IDs rather than the local OCI-index IDs, with explicit mapping receipts.
The ARM Mac has AppleClang21/Qt6.11.2 for a separate native compatibility job.
Existing user jobs and global caches remain preserved. Evidence is under
`evidence/qml-performance-adoption/` and
`evidence/distributed-validation-20261114/`. No remote build pass is claimed.

Commit992bf392's performance changes are integrated with narrow R10 adaptations:
existing per-process settings identity, guarded dev-mode resource cleanup,
fail-closed dev/coverage combination and the existing warning harness's selected
probe paths. All61 component files retain their registered tests, per-file CTest
parallelism and120-second process limits. Default embedded resources remain the
acceptance/coverage mode; animations and timing oracles are preserved. The next
focused native selection expects343 bodies, including all84 executor bodies,
all150 PSBT worker bodies,70 PSBT-model bodies and the affected consumer cases.
The new executor18 and settings/controller9 rows are not yet runtime-qualified.
The complete native QtTest inventory now projects3,769 bodies; the QML projection
remains1,021. Neither full count is an executed pass at this new snapshot.

Frozen64 `66c6f52017c16e745061440be72203e040d7af6a` passes14 console-output
model bodies under pinned sanitizers and continuous coverage. It preserves the
two original bodies, adds12, and checks130-row capture in32-row GUI turns plus
real Domain/Cleanup execution and physical backend drain. The preceding13-body
snapshot63 passes independently and closes65 old source-coordinate outcomes.
The next20 QML rows are implemented/static-reviewed but not run; the projected
full QML count is1,021 before performance integration and inventory reconciliation.
Its then-current native projected count was3,742; no full replay at that count is claimed.

Fresh61 QML measurement has coordinator review:91 current sources,344 aliases,
17,382 points,11,332 starting lines and5,536 outcomes. Original/instrumented phases
each pass188 exact bodies on one ELF. The qualified source-coordinate union is
9,956/11,332 lines and4,563/5,536 outcomes; it carries only the90 byte-identical
sources and freshly measures changed LabeledField. It remains incomplete.

Allocation review records a real pre-admission bookkeeping defect: an exception
can leave a pending task without a returned Admission or queued execution turn.
The16-process diagnostic and raw failed outcomes are retained, not a unit pass;
scope allocation unwind is clean. A local transactional admission/pump repair is
implemented under the existing ownership contract and awaits runtime checks.
Core, its handler policy and
public wallet interfaces remain unchanged. Post-acceptance outcome ownership and
mandatory transport assertions must be preserved. These implementation changes
invalidate the old execution-source qualification until the new batch is frozen
and checked. No shared finding register is closed; R10 remains unaccepted.

Latest UI candidate61 is `379e8b3cd3f1a0aeaa03e25070d95fd1b718810d`,
with Core `44901d8f64cb617714707bd025ecd83f108353c2` unchanged. All188 affected
UI bodies pass on the same pinned sanitizer ELF: SignVerify24, RequestPayment101,
AmountInput39 and FormControls24; exit0, no warnings or stderr. The new physical
copy-button tests exposed a clipped signature action. A preferred-width candidate60
did not repair it. Candidate61 binds the existing header RowLayout visibility to
its field owner; all three clipboard actions and their ancestor-clip checks pass.
The exact one-predicate negative control62 fails only the signature row as
expected; fresh91-source mapping and188-body equality have coordinator review.
No native component is replaced and no Qt source is changed.

The focused native TSan run58 passes301 bodies (309 Qt totals), with zero race
reports. Four intentional assertion children retain their exact fatal receipts.
The initial configure-path error and the post-run result-parser error are preserved;
the latter was reconciled from unchanged raw logs, MOCs and objects without rerunning
or weakening tests. These results qualify the selected C++ sources, which remain
byte-identical through61; they do not certify the GUI lifecycle or deferred picker.
The public malformed activity snapshot regression also passes its one focused
sanitizer body and one continuous-coverage body on59. Its exact current-header
profile reaches the missing-rank tail and loop-exhaustion outcome. The separately
qualified identical-source union now covers105/105 lines and56/56 outcomes for
the retained activity snapshot scope; no profiles from different ELFs are merged.
Fourteen exact private-state invariant outcomes and six positive code-resumption
lines on58 have coordinator review receipts. Raw LLVM exports and the legacy
ledger remain unchanged; allocation exceptions are not waived by those receipts.

The76 new UI bodies raise the current expected full QML inventory to1,001.
No full1,001-body pass is claimed. The prior91-source coverage union below is
historical for the changed LabeledField; its new Boolean binding requires fresh
AST, alias and counter qualification. Allocation-failure admission/drain behavior
is being probed separately: Core installs its terminating new-handler after some
app startup queues can already run, so it cannot justify a blanket unreachable
disposition for pre-initialization allocations. No production allocator policy
change or Core modification is authorized by this investigation.

Latest additional evidence: `evidence/qml-affected-asan-61/`,
`evidence/qml-copy-layout-asan-61/`, `evidence/qml-gap-asan-59/`,
`evidence/qml-copy-layout-asan-60/`, `evidence/frontend-native-tsan-58/` and
`evidence/native-gap-asan-59/`, `evidence/native-gap-coverage-59/` and
`evidence/native-core-reachability-58/COORDINATOR_REVIEW.json` (plus its separate
line review). R10 remains unaccepted; R11 remains unstarted.

Current qualified focused snapshot58 is
`3c852ea77a1f70049ca1cc1602fbb32a2b8ce93d`, Core
`44901d8f64cb617714707bd025ecd83f108353c2`. It passes247 focused native
sanitizer cases,247 focused continuous-coverage cases, affected clang-tidy and
app lint. All32 MainRouting bodies pass under X11 sanitizers and in both
ordinary/instrumented Qt6.10 measurement phases. The full current native
coverage execution accounts for3,729 bodies:3,717 ordinary passes plus12
explicit fatal delegations, all12 independently verified;87 classes, no missing
functions or failures. Strict profile/mapping and remaining reachability review
still apply. These results do not establish full R10 acceptance.

The PSBT inspection authority repair now uses the typed metadata-failure state
and authenticated fee amount. A legal non-null empty translation previously
made a complete document appear broadcastable after a metadata failure. The
isolated red test53 proves that defect;150 current worker cases retain that
regression, success-status precedence, malformed public-provider results,
optional flow metadata bounds and public external-signer capability controls.
Source bytes and the reviewed proof remain unchanged on failure. Core is clean.

The MainRouting batch preserves all14 original bodies and adds18. Native GUI
notifications and rendered/animated StackView readiness are awaited before
asserting effects or clicking controls. Minimal QPA does not emit native window
position-change signals, reproduced with the same binary that passes under X11.
Only that native geometry case is explicitly unsupported under minimal:
31 passes and1 skip, not an all32 pass. All32 remain required and pass under
X11; there is no Qt patch, replacement native component or blanket warning
exception. Raw first failures and the initial X11 setup mount error are retained.

Current source-coordinate QML measurement retains all91 source files,344
aliases,17,380 points,11,332 starting lines and5,534 explicit outcomes. The
separately retained identical-source union reaches9,879 lines and4,463 outcomes;
the MainRouting batch adds34 MainWindow outcomes and37 overall. Both current
measurement phases execute exactly32 identical rows with one ELF. This is not a
full925-body suite run, a mixed-ELF profile merge or a complete coverage claim.

Latest evidence is under `evidence/native-gap-asan-58/`,
`evidence/native-gap-coverage-58/`, `evidence/native-continuous-58/`,
`evidence/native-continuous-audit-58/`, `evidence/focused-tidy-58/`,
`evidence/lint-app-58/`, `evidence/qml-mainrouting-asan-58/`,
`evidence/qml-mainrouting-minimal-58/SUPPORTED_SCOPE_RESULT.json` and
`evidence/qml-mainrouting-measurement-58/`. Commands, source/test/ELF hashes,
actual exits, raw diagnostics and earlier failures remain retained. No shared
finding register is closed. Windows remains skipped by user instruction; the
native Qt picker TSan case remains deferred and unresolved. Current-source
threading evidence, remaining reachable coverage and final combined supported
configuration/integration/IPC lifecycle checks still block R10 acceptance.
R11 remains unstarted.

R10 remains unaccepted and R11 remains unstarted. This work uses the local
continuation at `85834d8780f12abb6946c5c1fb46769941ddc8dc` on the separate branch
`work/qt6-r10-acceptance-20261114`. Core remains
`44901d8f64cb617714707bd025ecd83f108353c2`. The original checkout, its unrelated
working changes, published refs and shared finding registers are preserved.

The continuation retains actual delegate retirement with animations enabled,
the PSBT import/coin-observation repair, callback-failure retirement ordering,
the checked custom-fee conversion and the repaired settings journeys. It does
not adopt the fork's animation-disabled teardown fixture.

The acceptance path is to close meaningful public-interface coverage gaps,
review genuinely unreachable outcomes with exact source proofs, qualify QML/JS
measurement, then freeze and run the required broad configuration and process
checks. Focused evidence is not a substitute for that final qualification.

## Current changes

- Add controlled DebugLog tests for complete/partial/oversized record framing,
  bounded retained bytes, exact newest content and background-owner drain.
- Add controller tests for external-signer discovery, creation outcomes and
  real managed wallet paths, with held Core/GUI stages and exact effect oracles.
- Add direct PSBT worker tests for authorization, passwords/relocking, retained
  signatures, exact file/clipboard output and metadata-assisted broadcast.
- Make credential materialization an explicit Native effect in `psbtwork::Context`.
  Production defaults to the same conversion. A deterministic failing port must
  leave the wallet locked and start no unlock, signing, inspection or broadcast.
- Make file saving an explicit Native effect with the existing QSaveFile default.
  A real committed file can outlive an exception reporting its outcome; the
  retained state must be uncertain and must not pretend the file was not saved.
- Stop startup readiness and notification continuation after reentrant shutdown,
  admission fencing or observer destruction. A clean exact-parent failing probe
  and 145 controlled signal-boundary rows protect this repair.
- Extract real startup diagnostic, preference lifetime, preparation ordering and
  log routing/write acknowledgement policies into tested owners, with actual
  production callers wired through their ports.
- Add optional test-process QML coverage collection. It is inactive without an
  explicit manifest/RCC/output triplet. Unsupported syntax and incomplete
  mappings must fail; tool controls and percentages alone do not establish
  complete QML coverage.

## Evidence and limits

Workspace artifact root:
`.validation/qt6-r10-acceptance-20261114/`.

`BASELINE.json` records base, Core and original dirty-file identities. The first
passing focused sanitizer snapshot is `29d445990a626295f2152ea43ac9021468bc279e`:
`evidence/native-gap-asan-05/result.json` records 179 body results (DebugLog88,
PSBT60, controller31), all passing, with separate class XML and exit statuses.
Earlier compile failures, the invalid Load More precondition and the PSBT
normalization premise failure remain retained in the earlier numbered runs.
Those runs are not passes. Later changes need their own results.

An earlier focused sanitizer pass is snapshot21,
`d2b7a86d2bc891a370df6affe3b5bd6db45f6fe7`: **702 body results**, no skips or
failures. Its per-class counts are DebugLog88, PSBT64, controller31,
preferences112, Node155, startup policy70, Qt log80, startup effects10 and Core
settings92. Commands, immutable image, source hashes, XML, exits and cache
statistics are in `evidence/native-gap-asan-21/`. This is not a full unit,
integration, platform or R10 acceptance claim.

The clean exact-parent Node probe is snapshot13,
`5f0223bae395d2e09bbb079ec80121a58fb8ba68`: one body fails with
`late_chain_ready == 1` versus expected zero; cleanup completes without a
sanitizer error. The same assertion passes among the corrected current rows.
Earlier attempts separately retain a stale-selector lookup and new fixture
lifetime errors. They are not application use-after-free claims or qualified
red probes. Preference/PSBT test premise corrections and fixture compile/string
ownership corrections retain their original failures and unchanged behavioral
requirements.

Stable build mounts now compare source input SHA256 values before Ninja/MOC.
Only identical inputs retain previous timestamps; changed/new inputs are forced
dirty. Frozen source bytes and Core stay unchanged. Final qualification must
also verify actual regenerated objects and ELF/profile identity.

Focused coverage07 adds105 covered selected native lines and65 outcomes in the
two SHA-identical DebugLog/controller files, preserving their raw denominators
and old ledger. Three diagnostic mismatches are independently checked Qt
translation helpers with zero profile hash. The exact PSBT branch-counter discrepancy is reproduced by a metadata exception
before the final guard. Its LLVM expression omits that exception edge. An exact
source/ELF-qualified review preserves all raw counts, declines credit for the
phantom positive outcome and records three unreachable final guard outcomes
and two unreachable body lines. The remaining PSBT gaps stay required.

QML collection has an integrated 12-original/12-instrumented passing component
probe on frozen15 and a failing invalid-counter control. The corrected overlay
retains57 files and records declarative aliases/IDs as metadata. Independent
review found binding-completion semantics and the full suite then falsified a
direct-function signal-handler wrapper. Both are instrumenter defects, with
actual counter/dispatch controls and retained failures. A corrected full run is
required; no prior QML percentage establishes acceptance.

`evidence/coverage-scope-audit/` retains the conservative scope, all eight primary
finding IDs, O04/O27, independent view interest and expanded GUI work. Exact
reviewed dispositions preserve the raw ledger; executable initializers,
automation bridge logic and 57 QML/JS source files remain accountable. The full
legacy ledger remains visible. No reduced denominator establishes whole R10
acceptance.

Core changes remain prohibited. A required fix that cannot be implemented
through allowed public interfaces must be documented and held for user input.
The native Qt picker remains retained under the user's explicit document/defer
decision, with its affected TSan result unresolved. No replacement chooser,
Qt patch or blanket sanitizer suppression is authorized. Configuration/native
producer obligations still need explicit qualification or recorded blockers;
an inherited branch note does not establish a new user-approved deferral.

## Subsequent qualification

Snapshot28 `4cf3a3df0c0f4fa25c2fb90c73eeb3a3495daa0c` passes **798 focused
sanitizer body results**, with no skips or failures. This includes139 preference
rows,143 Core settings rows and17 manual PSBT-model rows in addition to the
previous focused owners and the manual shutdown producer row. Separate class
XML, exact commands, source/image identities and actual exits are retained in
`evidence/native-gap-asan-28/`. The broader snapshot24 sanitizer run remains
separate and cannot certify later source by implication.

A stale preferences retry failure could replace a newer confirmed write's error
state. The existing read failure callback now captures its operation ticket and
only changes the displayed error for the current ticket; every outcome still
settles its pending count and notifies. Exact-parent snapshot27
`16ccac7e5a5876f7a3ee6c0fc050af17ecc418ef` executes one body and fails the
expected empty-error assertion. Snapshot28 runs the identical regression and
passes. Snapshot25's deprecated Qt test macro compile failure executes zero
bodies and remains recorded; the replacement uses an equivalent explicit
exception oracle compatible with Qt6.2 and current warning policy.

Lint24 executes app and pinned Core scopes. Core passes; app finds a shebang
permission mismatch in the new instrumenter and locale-dependent formatting in
a new Node test. Mode755 and QString::arg are prepared; later frozen source
still needs lint qualification. Pinned tidy22 passes22 selected units; changed
scope tidy28 remains separately qualified by its actual result.

Windows snapshot24 provenance includes exact app/Core archives and a manifest
adapter retaining Core's vcpkg baseline/features plus required QML/SVG packages.
The initial dual-build triplet configure was stopped before application tests
to use Core's release-only triplets. MSVC then fails in unchanged Core LevelDB
with warnings treated as errors; zero test bodies run. No Core edit or warning
waiver is made. A portable signed LLVM23.1.5 archive is verified using official
release keys and SHA256 for the ClangCL follow-up. The installed gh lacks
attestation verification, and guest tar's Zstd window limit requires a byte-
preserving gzip repack. These tool setup failures do not count as app passes.
Windows11 results cannot establish Windows Server CI parity, and Core forces
IPC off on Windows; the required IPC-enabled lifecycle remains a Linux check.

Follow-up upstream note: LLVM's source-based coverage infers a final Boolean
outcome and its following region as positive after an earlier metadata call
throws. Retain the exact ELF counter expression, isolated legal metadata
exception/error/success cases and independent broadcast-call oracle in
`evidence/coverage-scope-audit/psbt-exact-metadata-controls-21/`. Report this
upstream later; no compiler patch or general exception-count subtraction is
authorised. Existing public Core interface improvement notes and the strict
notify/wait rule for an unsolved Core/API requirement remain binding.

Windows validation is explicitly skipped by the user for this acceptance run
("windows validation can be skipped"). Stop the owned ClangCL build, preserve
its partial compile evidence and save the task-resumed VM. No Windows test
pass is claimed, and Windows is no longer an R10 acceptance blocker under
this direct instruction. Other platform/final-series claims remain separate.
`evidence/windows-native-24/USER_SKIP.json` records provenance and the log
collection limit. Continue the remaining Linux, coverage and lifecycle gates.

The corrected QML instrumenter752dc measurement runs all755 original-resource
and755 instrumented bodies on one exact snapshot26 ELF, each with110
setup/cleanup incidents, exit0 and empty stderr. Exact result/row identities
and the known Qt warning multiset match. All57 files,344 aliases,14,791 sites,
9,500 starting lines and4,850 explicit outcomes remain in the raw ledger:
7,731 lines and3,586 outcomes are covered. This is real measurement, not100%
coverage. Independent policy review and exact introduced/migrated owner
classification remain separate; raw tool_qualified staysfalse.

`evidence/coverage-scope-audit/actual-regression-scope-crosswalk-21-26.json`
maps all eight original finding scopes plus O04/O27 and independent view
interest to43 selector seams,28 distinct executed body identities, and no
unmatched selector. Cross-scope duplication is explicitly counted rather than
reported as additional tests. Native source21 and QML source26 provenance
remain distinct; no canonical finding register or acceptance status is closed.

Snapshot29's public startup-facade regression also proves that closing the
diagnostic source leaves busy true after its operation is retired. Snapshot30
`ee7de83e1c8387ece52398208acd244d7c4d40d2` passes all18 diagnostic-model bodies
with clear busy/public change notification and silent destructor retirement.
Independent review then requires balanced row reset and deferred reentrant
notification checks; their further implementation is not certified by30.

## Current acceptance blockers and qualified repairs

R10 is still unaccepted. The latest worker snapshot37 is
`8c7dff34b8c9c55242f420733483aaf2dd10b419`, with unchanged Core44901. Its
pinned sanitizer run passes279 body rows: receipts49, requests106,
reservations57 and send67, with no skips. Separate class XML, actual exits,
commands and source hashes are in `evidence/native-gap-asan-37/`.

The new request regressions exposed a fallible projection after confirmed
Native persistence. Both standard and non-standard exceptions could strand
the task instead of reporting the retained effect. Confirmed saved facts now
survive projection failure, which settles as an explicit partial outcome and
requires reload before another write. Exact-parent failures and their real
task leaks remain in `evidence/native-gap-asan-34/` and35; all106 current
request cases pass in37. No successful Native write is automatically replayed.

The reservation history scan also decremented an unsigned zero after its
256-item Domain budget was exhausted. Snapshot36's existing manual
`largeHistoricalScanYieldsBeforeAllocation:continue` regression aborts with
the integer sanitizer diagnostic; its partial XML is not a pass or57 executed
results. The37 fix checks the remaining budget before decrementing and keeps
the same bounded continuation and close/rejection/new-intent oracles. Its
full57 reservation bodies pass. See `evidence/reservation-budget-repair-37/`.

The complete diagnostic reset/reentrancy protocol is qualified separately by
snapshot33:48 diagnostic,155 Node and70 startup-policy bodies pass under the
pinned sanitizers. A focused frontend TSan33 run passes875 native body rows,
103 QML totals including setup/cleanup, actual IPC/Core RPC node restart and
four six-case fee journeys. Seven GUI exits are zero with retained stderr and
no TSan report. This is source33 evidence, not qualification of later worker
changes; their affected TSan follow-up remains required. The native Qt picker
case stays explicitly deferred and unresolved, and Windows is skipped.

The original broad sanitizer24 run exited zero, but source qualification
rejected its native result because the lifecycle object executed one
untagged row while the exact source declares two data rows. Preserve the
runtime evidence without claiming a complete current-source pass. A new
focused build forces all tracked app/test/Core inputs through Ninja and
ccache once; snapshots32 onward verify the restored two lifecycle rows.
Changed input hashes continue to force recompilation. Final full validation
must use the freshly qualified objects and verify exact selector identities.

Continuous native coverage uses Clang23's supported continuous-profile mode
to retain intentional fatal-process execution. Separate normal/abort/_Exit
tool controls preserve real exit codes. Corrected runtime-only replay33 drops
both DAC capabilities and passes3,556 ordinary bodies with12 explicitly
delegated skips plus12 checked terminal cases. The initial root-capability
permission failures remain diagnostic evidence. Mapping warnings and profile
selection remain under independent audit; no coverage gate is closed.

The116 new focused QML cases on snapshot36 execute99 passes and17 failures.
The original results remain in `evidence/qml-focused-asan-36/`. Fixture
readiness/lifetime premises and application issues are being distinguished
with independent probes. In particular, Qt6.4's empty sliced-string match
behavior needs a narrow app workaround; no Qt patch or replacement component
is planned. The expanded91-file coverage inventory and the corrected full
measurement must retain the old57-file ledger and all reachable owner logic.

App lint37 runs a nonempty app range and finds the collector's quoted moc
include. The prepared angle-bracket include retains the generated header and
requires qualification. Core lint24 passed against unchanged Core44901;
affected tidy and final app lint remain required. No shared finding register
or R11 work is advanced by these partial results.

Snapshot39 `ce9c4261475f6f1de3e988ef07c3d4aedee74dc1` passes the full884 QML
bodies plus120 setup/cleanup incidents under the pinned ASan/LSan/UBSan/integer
configuration and X11. It retains every original755 body and all129 distinct
new cases; actual ELF exit, stderr and sanitizer scan are clean. See
`evidence/qml-full-asan-39-x11/`. An offline accounting-path error in the host
wrapper is retained and corrected from the original XML and recorded process
exit, without rerunning or relabeling a failed application process.

The minimal backend's two unsupported native size-hint messages are now
asserted only immediately before their two known details-toggle actions.
Missing, duplicate, unrelated and unguarded warnings still fail independent
controls. The global warning policy and all original UI/content/height
oracles remain. Snapshot41 `92deb9d3bd58cba4dcab51e88a46518ee95c1a5a` passes
all five native-window bodies both on minimal and X11; the original minimal
failure stays recorded. This test compatibility treatment does not suppress
sanitizers or replace a Qt component. The Qt6.4 empty-slice workaround in the
application preserves the original512-character empty-editor case and13
additional provenance/context controls. Expanded91-file QML coverage is
being independently qualified; these runtime passes do not establish100%.

The fresh snapshot37 worker TSan qualification passes all279 body rows,
287 totals including setup/cleanup, with no failures, skips or race reports.
The first attempt had stale MOC/object files and is rejected. The private
rebuild dirties984 outer tracked inputs once, preserves Core source, and
independently verifies all156 registered functions with generated MOC and
actual object symbols. Its211 cache calls yield202 direct hits,9 misses;
64GiB/content remains configured. See
`evidence/frontend-worker-tsan-37/requalified/results/final-audit.json`.
The v31.0 compatibility request test is discovered but unrun because that
binary is unavailable; current Core32 is not a substitute. This is a separate
compatibility limitation, not a worker TSan failure.

Snapshot40 `8a822e2d65a85d6500bba5dcdfbd06f5257c66c5` passes129 focused
sanitizer bodies:121 PSBT and eight new execution/budget cases. App lint40
passes; affected tidy40 passes its three changed handwritten units and six
app moc aggregates. The earlier tidy38 report retains two emplace findings,
now fixed without changing the test oracles. Focused continuous coverage40
passes the same129 bodies and exercises14 previously uncovered exact source
branch coordinates. Its first scope-auditor preflight ran zero bodies and
failed an overly strict Core-path assertion: two unchanged Core glue files
are already compiled in app targets. The retry verifies that all Core-owned
targets remain uninstrumented and excludes those glue files from the app
denominator. Both attempts remain retained; no mixed-ELF raw profile merge or
full-coverage claim is made.

The native33 review retains all478 ledger files and verifies all336 data
providers. Exact Qt6.10.2 header/runtime/library provenance is recorded; an
audit's erroneous Qt6.4 cross-artifact assumption is explicitly rejected.
Three private lifecycle guard outcomes have exact-source/noexcept proofs
approved by the coordinator, with raw measurements preserved. See
`evidence/native-continuous-audit-33/LIFECYCLE_COORDINATOR_REVIEW.json`.
Other reachable gaps remain required.

Snapshot42 adds an internal last-issued scope-ID seed
to execution Limits, defaultzero preserving normal production identities.
Six controlled legacy/routed cases cover default, final-ID and exhausted
admission without waiting for2^64 operations or forging private state. Its
pinned sanitizer run passes all135 selected bodies. No R11 work has started.

The eight-row receiving-address facade batch confirms two application defects
on snapshot44: duplicate resume clears a successful address's readiness, and
stale resume hides the successor password prompt. A third failing row was a
fixture error: it asserted shared-executor quiescence before closing both
fixtures. Its final quiescence assertion remains after both close. Pre-run
review also removed self-owning callback cycles from the new fake wallet.
All original failures and the reasons for the fixture changes are retained.

Resume and Retry now require the current intent, original opening and current
view revision, with no receiving command pending. These bounded cached guards
reject stale/duplicate commands before capturing a password or submitting work.
The domain still checks authority independently. The tests require this early
rejection and retain the original successful address, successor prompt, exact
effect counts and final quiescence checks. Restoring only the old guards in
isolated snapshot46 makes four of the identical15 focused cases fail; the
negative mutation is not applied to the working branch.

Restored snapshot47 `e5e091b16323c9cd8d0defaa5abc5b76297e9aa4` passes all429
affected sanitizer bodies: facade15, execution13, clock1, PSBT121, receipts49,
requests106, reservations57 and send67. There are no skips. App lint47 passes
and post-lint branch/revision/Core checks remain clean. Tidy45 passes all37
selected units against the same application/test source content. Commands,
exact source identities, XML, stderr and actual exits are retained in
`evidence/native-gap-asan-47/`, `evidence/lint-app-47/` and
`evidence/focused-tidy-45/`. Later source still needs its own qualification.

The expanded QML measurement on source41 runs884 ordinary and884 instrumented
bodies with identical row identities and one exact ELF. Both exit zero. All91
files and344 aliases remain accounted for:9,748/11,332 AST starting lines and
4,381/5,534 explicit outcomes are hit. The remaining1,584 lines and1,153
outcomes are recorded, not waived. Independent mapping qualification remains
separate. These measurements are historical component evidence for the
unchanged QML sources, not full qualification of newer native source.

R10 remains unaccepted. Required reachable unit coverage, remaining focused
implementation/guard checks and final combined configuration/lifecycle
qualification still block acceptance. Later native changes also require
their affected TSan evidence. Windows stays skipped, the native Qt picker
result stays explicitly deferred, and Core44901 remains unchanged.

## Latest focused qualification

Snapshot50 `a24d2cfb28e09c6a2b05693abbc95f0c21a49a4b` passes160 focused
native sanitizer bodies and the same160 continuous-coverage bodies:23 executor
cases and137 PSBT worker cases. Both processes exit zero with no skipped rows.
App lint50 passes its nonempty application range; affected clang-tidy50 also
exits zero. Exact commands, immutable toolchain images, source hashes, XML,
ELF/profile identities and exits remain in `evidence/native-gap-asan-50/`,
`evidence/native-gap-coverage-50/`, `evidence/lint-app-50/` and
`evidence/focused-tidy-50/`.

Task IDs now reject sequence exhaustion before increment, insertion, usage
accounting or dispatch. Four bounded legacy/routed rows admit the last real
IDs, reject further ordinary and cleanup work, and verify original work,
outcome and borrowed-resource drain ownership. Default sequence zero preserves
normal admission. Public immutable constructor seeds avoid an unbounded test
or private-state mutation. No Core source or interface changes are involved.

PSBT file policy now uses narrow reader/writer ports with the same production
QFile/QSaveFile defaults. Deterministic tests cover open/read/size failures,
positive and negative short writes, cancellation, commit failure and successful
exact bytes. The existing native file picker is retained. This is not a Qt
patch, replacement chooser or claim that the deferred picker race is resolved.

All146 SendCreate bodies pass under the pinned sanitizers on snapshot50,
including23 new cases. Ordinary and instrumented Qt6.10 runs execute the same
146 row identities with the same ELF, zero warnings and empty stderr. The new
cases exercise35 previously missing SendCreate outcomes. Exact-source QML
measurement qualification now accounts for91 files and344 aliases. The
source-coordinate union retains raw totals of9,801/11,332 starting lines and
4,426/5,534 explicit outcomes; it is not a full907-body suite run or a complete
coverage claim. See `evidence/qml-send-file-asan-50/`,
`evidence/qml-send-measurement-50/` and
`evidence/coverage-scope-audit/qml-scope-independent-91-41/`.

Snapshot51 `a4f1959870de70e11cf2860f2339d2216519e5e9` preserves all snapshot50
production bytes and adds only four retained-transport subprocess cases to
the executor tests. All27 selected executor bodies pass under sanitizers and
continuous coverage. Each new parent verifies its child's exact rejection
marker, assertion function, nonzero assertion exit and absence of extra stderr.
All four child profiles match the exact coverage ELF; their real exit codes
remain recorded separately from the successful parents. Evidence is under
`evidence/native-gap-asan-51/` and `evidence/native-gap-coverage-51/`, including
`transport-child-probes/` and `profile-identities.json`. The standalone tests
require no artifact destination. The validation runner retains optional child
receipts without changing production logging or assertion/sanitizer options.

Remaining work includes reachable native and QML coverage, exact-source review
of invariant/compiler mappings, a suspected localized-text PSBT authority
regression under investigation, affected current-source TSan, and final combined
supported-configuration/integration/IPC startup and shutdown qualification with
actual stderr and exit inspection. No required Core/API blocker has been
established. R10 remains unaccepted; R11 remains unstarted. Windows is skipped
by explicit user instruction. Historical checks keep their original source
identities, and the native Qt picker result stays deferred and unresolved.
