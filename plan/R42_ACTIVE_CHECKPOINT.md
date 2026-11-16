# R42 qualification checkpoint

R41 is focused accepted at `90abf2fdd928bcdf8fc4ce3a831d2a2538d70a24`.
R42 remains active and **is not accepted**. The integrated repair source before
this checkpoint is `9670cc23704711ac6f6e802e0803975104a8f72e`; stock Core remains
`44901d8f64cb617714707bd025ecd83f108353c2`, unchanged. Repairs retain separate
commits and evidence; this document does not confer acceptance.

Windows is excluded by the user's explicit instruction. The VM is suspended.
Linux, macOS, coverage and sanitizer requirements remain. No R43 or push is
authorized. Evidence paths below are relative to workspace
`.validation/qt6-r41-r42-20261115/R42/`.

## Qualified repairs and source-specific results

- The pending-coin-selection Maximum defect is repaired at `a37df7c3d13`.
  The original controlled regression fails on its parent and all 25 affected
  bodies pass after the fix. Full pinned Clang 23 tidy passes 322/322 entries
  at a37; full app/Core lint passes. See `repairs/maximum-notice-audit/`,
  `final-tidy-a37-03/` and `lint-final-04/`.
- Qt 6.2 Release qualification at `7290ec7e025` passes all 78 ordinary CTest
  entries: 6,577 Qt bodies pass, with 12 existing delegated fatal skips whose
  child exits are independently checked. Its complete 121-class native
  registry finishes. All ten integration groups passed at a37; ELF/source
  identity audits are retained rather than assuming a later revision passes.
  The subsequent fee fixture relay checks remain under qualification.
  See `qt62-fee-receipt-currency/QUALIFICATION.json`.
- The Qt 6.2 QThread abort came from our test formatting a retired QObject
  pointer. Opaque thread-address equality preserves its affinity assertion.
  This is separate from the approved deferred Qt picker issue. The portable
  Main rendering helper preserves actual frame checks where supported and
  requires the original render receipt where minimal QPA returns a 0x0 frame.
  These changes are integrated at `8de713cefc3` and `e9009342bb9`.
- Stock ASan/LSan/UBSan/integer full qualification at a37 found fixture-owned
  retained leases: 50,097 bytes in 211 allocations. Exact parent controls
  reproduce them. Explicit fixture retirement passes the nine exact controls
  and all 89 affected bodies without leaks. The legacy receive fixture now
  waits for public reconciliation readiness before its single commit; all four
  real Core metadata rows pass. These repairs are integrated at `398bec66a37`
  and `1d11dca8b25`. The full final-source sanitizer result remains pending.
  Independent a37 lifecycle qualification passes 34 cases with 45 inspected
  process receipts; source equivalence is required before carrying it forward.
- A PSBT Node replacement retained a paging notifier bound to the previous
  generation. Two public/manual rows reproduce the stuck pending state. Query
  retirement preserves the immutable summary and stale-result fencing; all
  88 affected model bodies pass. The regression and repair are integrated at
  `12abc87e8b4` and `795c2da50f7`. No Core implementation is accessed or changed.
  See `repairs/psbt-summary-node/`.
- GCC 15 rejected the original settings fixture's variant copy at the actual
  Release/O3 flags. Active-type construction preserves every original value;
  the identical TU compile fails on the parent and succeeds after the repair,
  without suppressing diagnostics. Integrated at `9670cc23704`.
- The integrated eight handwritten C++ files plus seven fresh MOC aggregates
  pass pinned tidy: 15/15, `integration-tidy-04/`. Later fee fixture changes
  still require their affected checks.

## Remaining acceptance gates

The full a37 platform results retain actual failures in
`platform-matrix/final-remote-audit-a37/`. Gameboy GCC Release never reached
runtime after the compile failure and generated TestBridge MOC cache failure;
zero tests is not a pass. Bazzite wallet-enabled and wallet-disabled both ran,
with Main rendering and separate fixture failures still recorded. Native macOS
has real test/process failures and descriptor exhaustion, not an assumed Qt
waiver. A new inspection shell reports soft NOFILE 256, hard unlimited; the
historical failing process did not retain those measurements. Its 33-wallet
fixture can legitimately create 175 executor threads. Measure peak use and
post-drain baselines before attributing exhaustion to capacity or retention.

The static Linux Qt 6.8.4 SDK and archive audit pass; the application must still
build, link and run against that exact SDK. The static macOS attempt failed in
Qt with SDK 27. Follow-up finds older CLT SDKs; compatibility must be qualified
without changing Core/Qt implementation or pinned archives. Actual native OS
Dock/reopen, URI and FileOpen producers remain separate from Cocoa bridge tests.
See [environment limits](R42_ENVIRONMENT_LIMITS.md).

Coverage remains a blocking measured gap. At historical frozen `34b87996094`,
raw unit line/branch coverage is 85.429%/70.490%; combined is 88.107%/73.034%.
There are 5,670 mapped line gaps, 12,082 nonfolded branch gaps and 54 files
without emitted mapping in that Linux configuration. Some files are tooling or
platform-only, but handwritten header initializers need qualified attribution.
Constructor counters alone do not prove initializer execution: retained negative
controls demonstrate that distinction. QML AST tooling also has unmeasured
initializer spans and incomplete qualification. No denominator reduction or
metric adjustment has been granted. The target remains 100% of reachable,
unit-testable handwritten logic, with all source files accounted for. See
`coverage-current/` and `qt62-fee-receipt-currency/coverage-attribution/`.

Final combined native/configuration/platform tests, sanitizer qualification,
coverage, changed-scope/static checks and the 329-scope/36-schedule producer,
progress and seven evolution-rehearsal audit are still required. Passing parent
results carry only with explicit source, executable and toolchain equivalence.
Original failures and skipped bodies remain distinct from passing execution.

Keep approved upstream remainders explicit: R09 device locking/cancellation,
R17 preview change policy (actual review authoritative), R20 precise commit
receipts and R30 capability007. Native Qt dependency diagnostics are documented
and deferred as authorized; do not patch Qt, suppress the whole application or
replace native components. Unavailable MSan dependencies and tracing privileges
remain actual limitations, with applicability assessed independently.

The original 11 dirty files and canonical 318-finding/329-scope/36-schedule
registers remain preserved. Task-private `R42/LIVE_CHECKPOINT.json` identifies
live owners; source-specific receipts retain exact commands, selected and
executed counts, revisions, hashes, stderr and exit statuses.
