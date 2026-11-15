# R42 qualification checkpoint

R41 is focused accepted at `90abf2fdd928bcdf8fc4ce3a831d2a2538d70a24`.
R42 remains active and **is not accepted**. The combined repair source is
`e2c0c8cd1dac9685fc20fc25ff02091e010191c0`, with unchanged stock Core
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

Final combined ASan, Qt 6.2 Release and native macOS jobs are running. Core
compatibility uses eleven stock checksum-verified releases and the stock
generated functional runner. Exact static depends, wallet-disabled builds,
final ordinary suites and current-source coverage still require results.

Coverage is a real acceptance gap. Baseline native measurement reaches 88.459%
of source-accounted lines and 72.971% of true branch counters. QML coverage also
has uncovered sites and twelve explicitly unmeasured native ListElement
initializers. These are not 100% coverage or a final repaired-source result;
see `coverage/REPORT.md` and `coverage/STATUS.json`.

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
