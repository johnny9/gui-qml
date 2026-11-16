# R42 qualification checkpoint

R41 is focused accepted at `90abf2fdd928bcdf8fc4ce3a831d2a2538d70a24`.
R42 remains active and **is not accepted**. The combined repair source is
`894c10e4047448d95794d46836df12d180303805`, with unchanged stock Core
`44901d8f64cb617714707bd025ecd83f108353c2`. Repairs have separate commits;
qualification does not hide application changes in a documentation commit.

The latest user instruction, **“Skip windows validation”**, supersedes the
earlier final-stage Windows requirement for this R42 run. Existing Windows
artifacts remain available. Its build and six executable manifests passed;
runtime qualification is incomplete and is not a Windows pass. The task's
stopped jobs were collected and the VM is suspended (`shut off`, managed save).
Linux, macOS, coverage and sanitizer obligations remain in scope.

Evidence lives under workspace
`.validation/qt6-r41-r42-20261115/R42/`. Exact commands, image identities,
source/binary hashes, executed selectors, stdout/stderr and exits are retained
there. A command template, discovery, old executable or empty selection is
never passing execution evidence. Final-source jobs use frozen inputs and
continue independent checks after a failure.

The baseline full suites found real application defects and stale fixtures.
Repairs include owned QString values, send admission/outcome ownership, coin
eligibility publication order, settings observation/intent fencing, and Activity
cleanup leases. Explicit import replacement, wallet load/selection, batch Save
and asynchronous readiness remain required user actions in process tests.
Fixture repairs preserve their original effect, persistence and stale-result
assertions. Timing tests use the general manual executors; physical-thread and
Core/RPC checks remain separate.

The latest bounded repairs have actual evidence:

- Activity patch disposal: its held-task schedule fails on the parent and
  passes after retaining the cleanup lease. Four native cases and nine pinned
  C++/MOC tidy entries pass (`repairs/activity-cleanup/ACCEPTANCE.json`).
- Signed amount presentation: the minimum signed integer originally triggers
  UBSan through public setters. Exact unsigned-magnitude formatting retains
  signed values and monetary validation. Both full affected native classes
  total 96 passing cases; all three public QML controls pass with stock
  ASan/UBSan/integer options (`amount-boundary-asan-03/`).
- GCC Release: the complete registry compiles; 249/250 focused cases initially
  pass. The remaining storage fixture counted independent readiness changes.
  Its corrected original two controls pass (`platform-matrix/linux-repair-08/`).

Subsequent repairs preserve the original contracts: reentrant controller owner
loss, public native-settings type decoding, the common PSBT serialization link
boundary, original action/readiness receipts, and lazy typed wallet presentation
ports. The wallet-disabled configuration now builds all targets, inventories all
78 configured unit classes, passes the original 20 file-export bodies and exits
the strict GUI startup/shutdown journey with empty stderr. This is focused
configuration evidence, not a full wallet-disabled test pass
(`platform-matrix/wallet-off-qml-11/ACTUAL_RESULTS_REVIEW.json`). The four new
shipped QML resources remain in the final coverage denominator (264 total).

The later focused ASan run at `1e5d1105303510c7ba8f065fe2ec2bd1ab9005a6`
builds all targets but still finds three failures: controller fixture callback
lifetime, reentrant wallet-preparation owner loss, and file-export leaks. These
are application/fixture repair work, not deferred Qt failures. Seven independent
selections pass, including all seven typed-send integration rows and both backup
integration rows. All 20 file-export assertions pass but its process fails LSan;
do not label that selection sanitizer-clean (`asan-cpp-current/run-01/`).

Core compatibility passes all seven selected cases with zero skips, using eleven
stock checksum-verified releases and the stock generated functional runner.
The five USDT cases actually skip for missing Python BCC and remain unqualified;
the stock runner's exit zero does not convert those skips into passes. Static
Linux Qt 6.8.4 builds and its semantic SDK audit passes; the app still needs to
build and run against that exact SDK. The actual macOS SDK attempt first stops
for missing GNU Make; the documented prerequisite is installed and a separate
retry retains all nine pinned archive checks. No SDK-only result certifies the
application. Qt 6.2, final combined ordinary suites and full matrix obligations
remain pending.

The original macOS 1,600-outpoint oversized-send error and 100-input recovery
case passes without increasing its 120-second deadline. Native RPC publication
takes about 65.2 seconds while GUI heartbeats continue (maximum gap 71 ms).
The one-script setup preserves 1,600 distinct outpoints and exact recovery
oracles; the result has a narrow deadline margin and does not establish that
the Native performance cost is fixed (`platform-matrix/macos-07-timing/`).

Coverage is a real acceptance gap. At frozen app `34b87996094e8fd5c3abe103deccc3443d5ad63e`,
the independently joined six instrumented executables and 799 raw profiles give
unit-only raw line/branch coverage of 85.429%/70.490%, and combined coverage of
88.107%/73.034%. There are 5,670 mapped line gaps after separately proved Qt
metadata, 12,082 nonfolded branch gaps and 54 native files without emitted
mapping in that Linux configuration. QML has uncovered AST sites, twelve
unmeasured native ListElement initializer spans and incomplete tool qualification.
These are not 100% coverage or evidence for a later repaired source. The direct
unit abort and three failed full-CTest entries remain failures even when their
retained counters contribute to coverage (`coverage-current/REPORT.md` and
`coverage-current/STATUS.json`).

Task-private `R42/LIVE_CHECKPOINT.json` identifies active lanes and new results;
earlier ledgers retain historical source-specific runs. A parent pass carries
only when the relevant source, configuration and producer are unchanged and
that equivalence is explicitly audited. New app/fixture repairs reopen their
responsible implementation slices; R42's qualification order does not hide
behavior changes in a documentation commit. All 329 original oracles, 36
schedules, real producer/progress obligations and seven evolution rehearsals
still require their source-qualified final accounting.

Keep the approved upstream remainders explicit: R09 device locking/cancellation,
R17 preview change policy (actual review is authoritative), R20 precise commit
receipts and R30 capability007. Native Qt picker and newly identified Qt engine
dependency diagnostics remain documented/deferred: do not edit Qt, suppress
the whole app or implement a replacement native component. MSan dependency
qualification is incomplete. A sanitizer build or ordinary pass does not close
these results.

The original dirty checkout and canonical 318-finding/329-scope/36-schedule
registers remain preserved. R42 adds evidence overlays; it does not rewrite
their historical dispositions. Stop at the R42 boundary. No R43 or push is
authorized by this checkpoint.
