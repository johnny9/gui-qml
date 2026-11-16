# R42 acceptance handoff

**Windows is excluded by explicit user instruction.**

**Broad Linux validation pass (2026-11-16):** every failure found on the Linux
matrix was fixed and requalified on the branch head — see
[linux-acceptance-20261116.json](linux-acceptance-20261116.json). Qt 6.10.2 and
Qt 6.4.2 wallet-on CTest pass 90/90 each, wallet-off passes 81/81, the unit
suite passes 5600/5600, all 36 functional journeys pass on both Qt lines under
Xvfb, app/Core lint, QML lint and tidy (322/322) pass, and the MainRouting
QtQml GC crash no longer reproduces (20/20 + 15/15 clean full-suite runs) after
collecting between cases. Sanitizer runs are no longer required for acceptance by user decision
(the executed ASan/UBSan evidence is retained). The remaining Linux gates are
the static Qt 6.8.4 application lane, Core USDT privileged tracing and the
final-head coverage measurement; upstream attribution for the GC crash remains
unproved. R42 acceptance still requires those lanes.
Core remains clean at `44901d8f64cb617714707bd025ecd83f108353c2`.
This branch includes the qualified implementation/fixture commits and plans.
Latest two fixture commits preserve exact manual clock and preparation admission
oracles. Their qualification: 37 focused bodies, whole Wallet 471, 190 repeated
bodies, app lint and eight pinned handwritten/MOC checks pass. Broader results
below certify parent `c2e8f0edb6a936f344d4332b2eaba20a47c9846f`, not this later head.

## Executed results

- Full stock ASan/LSan/UBSan/integer run: 122 native classes, 5355 passing bodies,
  12 delegated fatal skips; all 91 CTest entries pass. Independent startup18,
  shutdown14, backend1 and Core IPC startup/shutdown pass with stderr/exit checks.
- Bazzite GCC13.3/Qt6.4.2 Release: wallet ON90/90 and OFF81/81 CTest pass.
  Existing fatal, minimal-position and feature-dependent skips remain explicit
  in the copied audited result. No Windows pass is claimed.
- Final c2e app/Core lint passes. Both263-file QML lint variants pass with zero
  warnings/errors and1107 existing compatibility notes each. Integrated eight
  CPP/seven MOC checks pass; the later Wallet fixture has its separate8 checks.
- Static macOS Qt6.8.4 SDK build passes using installed CLT compiler/SDK15.4,
  without Qt/Core implementation changes. Exact nine archives, six static
  frameworks, package payloads, process feature and exported version qualify.
  Application build/runtime against this SDK is still pending.

## Open gates and exact failures

- Final coverage c2e build and direct native suite pass, but full CTest88/90:
  manualFeeClockSeparatesCheckpoints fails request capture (subsequently fixed
  by the qualified fixture commits on this branch), and MainRouting aborts.
- MainRouting debugger repetition reproduces SIGSEGV in QtQml GCStateMachine
  during preinit onboarding. Root cause is **unconfirmed**, not an approved
  vendor deferral. Stack: task `mainrouting-gdb-04/mainrouting-original-02.stdout`.
  One passing debugger run does not erase it. Initial Valgrind attempts execute
  zero app tests; matching glibc debug symbols unavailable. Isolated GDB17 works
  without runtime upgrades and captures the actual stack.
- Coverage remains below target. Historical34b raw unit85.429% lines/70.490%
  branches, combined88.107%/73.034%; not final-head qualification. Fresh c2e
  profiles exist. Header AST audits prove Qt zero-width metadata mapping issues,
  but apply zero metric adjustments. Handwritten/initializer gaps remain.
  Native ListModel controls are witnesses, not fabricated line counters.
- TSan05 is prepared but unexecuted. Prior Qt picker/engine dependency findings
  retain user-approved deferrals; new app failures are not automatically waived.
  MSan has zero qualified app tests; applicability/feasibility audit is retained.
- Mac native focused Controller290 plus signer1 pass; two preference-refresh
  removal tests still fail. FD256 reproduces exhaustion at249 descriptors;
  FD4096 original33-wallet case twice gives9→369→9 after drain. This measures
  peak demand, not an unconditional production resource-safety claim.
- Other Mac Core failures (backup SIGSEGV, receive click outside actual window,
  oversized deadline margin, PSBT timeout) need attribution/repair. Native OS
  AppleEvent producer checks are prepared, unexecuted; installed association is
  unsupported/unqualified and no synthetic bundle is certified.
- Scope/36schedule/producer/pending-progress/evolution overlays remain separate
  from exhaustive acceptance. Canonical318/329/36 registers were not rewritten.

## Resume and retained work

Workspace evidence root:
`.validation/qt6-r41-r42-20261115/R42/`. Exact commands, images, source/ELF
identities, stdout/stderr and failed attempts remain there. The copied JSON
receipts are portable summaries; do not substitute them for missing execution.

Gameboy static application baseline job remains running at handoff:
Root exec session34325, task `static-linux-app-04`, sourcea37. Collect it before
reusing its build/host lease; it does not certify this branch. Other agent jobs
are terminal and local tidy/Qt62/ASan leases released. Mac resource15 is terminal.

Patches alongside this file are **not integrated**: legacy-settings retirement
is unbuilt/unvalidated; D260 characterization only passes lint (five runtime
rows prepared, never dispatched); Mac canonical/TaskID fixture and FD diagnostics
have focused native results, with the two preference failures retained. Review
and qualify before applying. Node/peer header gap drafts remain task-local and
unbuilt. No new validation jobs should start from this handoff automatically.

Preserve original11 dirty files in the original issue worktree. Continue only
R42 acceptance. Approved upstream remainders R09/R17/R20/R30 remain documented.
