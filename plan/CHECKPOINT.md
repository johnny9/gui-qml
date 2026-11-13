# R10 implementation checkpoint — unfinished

Saved on 2026-11-13, with later immutable34 validation updates below, at the user's explicit request to commit the current state and
push it to their fork. **R10 is not accepted. R11 has not started.** This request
supersedes earlier no-commit/no-push instructions for this checkpoint only.

Application parent: `715fb5449366a9a267e6d111169f226b429947ef`.
Last focused-qualified frozen candidate: `8ae9ce530f76e3522e932997f7120af705598246`
(928 application files). Bitcoin Core: `44901d8f64cb617714707bd025ecd83f108353c2`.
Core's subtree, wallet/node internals and pin remain unchanged. The commit
containing this document is the saved application revision; validation of the
older frozen candidate does not automatically establish a pass for this commit.

## Architectural requirements and decisions

Keep QML declarative, with typed observations and narrow intent commands. Domain
decisions/effects, Core calls and filesystem work use injected bounded background
execution. GUI performs bounded presentation and necessary native handoffs.
Preserve explicit states, guarded transitions and original opening/action/outcome
ownership. Retain manual executors/clocks, real-thread tests and independent
Core/RPC integration.

Production wallet access stays behind public interfaces. If a required solution
needs Core changes, document the missing contract, notify the user and wait for
input before dependent work. Preserve the existing R09 conditional limits and
upstream notes about wallet locking, device waits and missing public contracts.

For defects attributable to native Qt components, retain the component, document
and defer the issue. Do not implement a replacement component or modify Qt.
Affected results remain explicitly unresolved. Continue independent work. This
policy does not excuse incorrect application ownership, threading or API use.

## Completed validation of frozen candidate 34

| Check | Actual result |
| --- | --- |
| Focused normal | 47 classes; 1840 body rows: 1828 passes and 12 paired fatal delegations |
| Focused ASan/LSan/UBSan/integer | 49 selections; 1879 executions / 1840 unique rows: 1867 passes and 12 paired delegations |
| Focused frontend TSan | 50 selections; 1915 rows: 1902 passes and 13 accounted skips, paired with actual fatal/permission companions |
| R10 O04/O27 and same-opening views | 310 bodies pass per focused profile, no failures/skips; four pertinent QML rows pass |
| Broad ASan CTest | 20/20 selectors pass; 3662 bodies = 3650 passes and 12 paired fatal delegates; actual 12 fatal children pass |
| Independent ASan Core/RPC groups | All nine nonempty groups pass; 185/185 bodies, no failures/skips |
| ASan process checks | Core GUI 1, public RPC 5, Core IPC 2, seven original controls plus seven raw control recordings pass; actual GUI stderr/exit inspected |
| ASan current GUI journeys | 13 suites; all 57 executed: 52 pass, five fail, no skips |
| Frontend TSan GUI journeys | 13 suites; 57 accounted: 56 executed, 51 pass, five fail, no skips; one deferred picker case remains unresolved with zero executions |
| Frontend TSan additional checks | Lifecycle/Core IPC 2 pass; two isolated controls produce the expected assertion failures without sanitizer diagnostics |
| Pinned clang-tidy 23 and Bitcoin plugin | 214/214 actual translation units pass; exit 0 |
| Native Qt 6.2.4 / GCC 12 | Build passes all 263 steps; 15/17 CTest selectors pass, Unit/QML abort; all nine integration groups pass |
| Native Qt 6.4.2 / GCC 13 | Build passes all 263 steps; 17/17 CTest selectors pass, all nine integration groups nonempty; 3651 bodies = 3639 passes and 12 expected fatal delegates |
| Native Qt 6.10.2 / GCC 15 | Build263 and static gates pass; CTest9/17 pass,8 fail from SVG/runtime warning failures; runtime SVG plugin absent. 3651 bodies:3228PASS/411FAIL/12paired fatal delegates. Profile not qualified |
| Shipped QML static checks | Qt 6.10 job checks 244 files per qt65/qt62 resource profile, zero warnings/errors, 1011 compatibility notes each; five negative probes pass |
| Lint | Fails on the console functional script's executable mode; correction saved here, rerun pending |
| Wallet-disabled | Frozen34 ENABLE_WALLET=OFF/IPC=ON: build186steps, CTest13/13 pass;2068 bodies=2056PASS+12paired fatal delegates;3 nonempty node groups,22startup/shutdown cases and independent Core GUI/IPC checks pass. Clang23.1.3/Qt6.10.2 |
| Coverage | Immutable34 coverage build running after wallet-disabled checks; no percentage or qualification claimed |
| Windows | Live preflight only: no existing jobs, vacant candidate paths, clock synchronized; zero builds/tests. VM suspended again with managed save |

Exact focused/broad summaries and schedule/source receipts are in [evidence](evidence/).
Separate runs overlap; do not add these counts as unique tests. The wallet-disabled GUI uses the in-process Core factory; an IPC-enabled build plus independent Core IPC tests does not establish an active GUI IPC listener. Qt 6.2 follows
the existing Qt < 6.3 warning-policy compatibility branch. Qt 6.4 enforces the
newer warning probes. No configuration is silently excluded.

## Unresolved issues and next work

1. **Qt 6.2 MainRouting teardown SIGSEGV remains unresolved.** Exact native ELF
   diagnosis shows an icon color-animation callback reloading an image with
   `qmlEngine() == nullptr` after the containing MainWindow context ended.
   Captured ancestry is a deferred WalletView/ReceivePage control under
   QGuiApplication. The earlier `setContextForObject` warning belongs to a
   parentless QML singleton, not a proven mock-wallet ownership error. Three
   small Qt-only probes do not reproduce the crash; upstream-only attribution
   is unproved, so no waiver is claimed. This selector already failed on frozen31,
   but identical root cause is unproved. A fixture teardown using actual delegate
   destruction receipts is proposed, **unimplemented and unvalidated**. Do not
   add arbitrary waits, disable animations or replace native components.
2. **Settings navigation visibility remains unresolved.** The original TSan
   journey failed at language visibility. One diagnostic using the corrected
   Apply receipt reached language successfully, then failed at post-restart
   proxy visibility. Duplicate nested settings stacks exist. The saved display
   stack readiness correction is not proof that every navigation failure is
   fixed. Investigate active-instance/stack identity while retaining geometry,
   proxy, language, restart, reorg/history and RPC assertions.
3. **Requalify five saved test-only corrections after frozen34.** Dead-QThread
   formatting after physical drain is corrected to opaque-address equality in
   WalletRequests and WalletReservations, retaining exact worker/effect/cleanup
   assertions. Proxy Apply now observes the existing OptionsQmlModel user-effect
   ticket and typed persistence receipt, rather than CoreSettingsModel's counter
   for all mutations. Four legacy-fee fixtures use typed review
   `feeKnown`/`feeAmount.satoshi` and bounded graph fee consistency, retaining
   exact 500000-satoshi, unknown, send/broadcast, mempool and Core assertions.
   The console script's executable bit is restored. These corrections await
   runtime/lint/tidy checks on the saved revision; production source bytes match
   frozen34. Original failures remain retained.
4. **Finish configuration, coverage and platform gates.** Native Qt 6.10 QML
   reports SVG icon decoding failures (`Unsupported image format`). Read-only
   inspection confirms its image lacks the runtime `imageformats/libqsvg.so`
   plugin despite the CMake QtSvg package being present. Retain this environment
   blocker and downstream warning failures; build/static passes do not establish
   runtime success. The true wallet-disabled configuration now passes on
   frozen34; retain its distinct Clang23.1.3 toolchain and source identity. Report reachable
   handwritten line/branch coverage and explicit gaps, qualify the saved source
   on Windows, and account for other required native-platform/toolchain
   limitations. No 100% coverage or Windows/platform pass exists.
5. **Deferred Qt picker race.** Stock Qt 6.10.2 QFileDialog starts its directory
   worker before installing connections. A Qt-only reproducer yields TSan exit
   66. The user explicitly chose document/defer. Retain the existing picker;
   no Qt patch, new suppression or replacement is authorized. The affected TSan
   case remains unresolved, not passing or skipped.
6. **Final acceptance/accounting.** Reconcile all eight primary R10 scopes plus
   O04/O27, independent view interest and discovered GUI-work regressions with
   the final revision and required process/configuration/coverage evidence.
   Preserve 318 findings, 329 regression scopes, 43 work orders and 36 schedules.
   Shared registers are unchanged; this checkpoint closes none of them.

The five ASan journey failures were the incorrect Apply observation and four
removed-map fee reads. TSan's five were settings visibility and the same four
fee reads. No sanitizer diagnostic occurred in these failures; the GUI processes
exited 0. Those fee failures occurred before their six-case inner matrices
completed, so those inner cases are not passing evidence.

## Resume and provenance

[Saved total plan](QT6_TOTAL_REFACTOR_PLAN.md) preserve the workspace plan, architecture,
requirements and upstream notes. [Tracking snapshots](tracking/) are
copies of the shared orders/scopes/schedules, with LF line endings and one final newline. CSV fields and rows are verified unchanged; original shared files are untouched. Historical `.validation/`
links in these snapshots refer to the original workspace layout, not bundled
files. These snapshots are not an acceptance-status export. This report and
newer explicit user decisions supersede historical pending-choice, prepare-only
and no-push statements.

Raw logs, exact commands, ELF/library hashes, diagnostic stacks and original
failures remain under the workspace path
`.validation/issue-remediation-pr1090-20261109/execution/gui-threading-implementation-20261112/`.
Build trees/caches, private credentials and raw runtime data are excluded from
Git. The native matrix uses immutable `source-v34`; later results retain that
candidate identity. Before producing another frozen candidate, update helper
assumptions that still name application parent 715fb544. Preserve historical
snapshots, unrelated changes and the Core pin.
