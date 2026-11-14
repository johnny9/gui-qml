# Sanitizer cadence decision

The user asked on 2026-11-14: “Run sanitizers and repair after all rXX are done?”
Routine sanitizer builds, runs and sanitizer-only repairs are deferred until the implementation work orders are complete. R42 owns final combined-series sanitizer qualification and repairs; final acceptance cannot close before those checks pass.

Continue focused ordinary tests, manual executors/clocks, real-thread tests, lint, clang-tidy and relevant ordinary integration. Preserve existing coverage evidence; exhaustive measurement and proof follow the approved deferral below. Ordinary correctness failures remain step blockers. Record implementation/ordinary-check completion separately from final qualification pending R42. Preserve existing results, exact revisions, nonempty counts and unresolved findings. Use incremental reruns and retain unchanged qualified scopes explicitly.

At R42, run the required pinned ASan/LSan/UBSan/integer and relevant TSan/MSan configurations on the combined head, including IPC-enabled GUI startup/shutdown with stderr and actual exit inspection. Repair findings and rerun affected checks before final qualification. Continue parallel validation on the authorized hosts. Native Windows is deferred to that final stage under the later instruction below; it is not permanently waived. Native Qt dependency deferrals, Core source/API boundaries and notify/wait rule remain unchanged. This changes timing, not final coverage or check definitions.

## Approved coverage and matrix deferral

The user selected “Final coverage and matrix (Recommended)” on 2026-11-14 and explicitly deferred native Windows until the end. R42 owns exhaustive 100% reachable coverage qualification, full unit/QML/integration and lifecycle sweeps, detailed compiler/profile mapping audits, full repository lint/tidy sweeps and the complete Qt/platform/build/wallet configuration matrix. The coverage target and original ledgers/scopes remain intact. Focused changed-scope lint/tidy remains part of each implementation step.

Retain focused ordinary behavior tests, manual timing/real-thread cases, relevant integration and checks for directly affected compile branches. Preserve actual command/source identity, nonempty selections, counts and failures. Per-step progress may advance after these checks while final qualification remains pending; no catalog or final acceptance is closed merely by this policy.

R10's valuable checks exposed stale/reentrant completion problems, fee/PSBT authority errors, admission/cleanup ownership failures and physical GUI interaction defects. Keep those issue-specific oracles and negative controls. Repeated whole matrices and exhaustive measurement-accounting work during changing implementation are deferred.

Current Mac Qt6.11 enabled-property override warnings and minimal/offscreen font-plugin failures remain explicitly unresolved for final compatibility qualification. Do not suppress them or claim those configurations passed. Existing native Qt dependency deferrals and the Core notify/wait rule remain binding.

## General targeting and fixture repairs

The user clarified that focused testing should use general targeting rather
than a new custom harness for each change. Unit tests already support class
selection through `BITCOIN_QML_TEST_CLASSES` and Qt Test function/data-row
arguments. QML supports per-file CTest entries and Quick Test case/function
selectors. Integration supports stage/module/wallet/function/row selection.
Use and document these existing entry points; do not create another test binary,
fixture or hard-coded Python runner solely to select a subset.

Keep selection manifests as data. Reuse common execution/reporting tooling; if
a missing general capability is necessary, extend the existing runner once.
Remote dispatch and evidence collection must also use reusable tooling rather
than copied per-change scripts. Preserve actual exits, warnings, nonempty body
counts and distinct pass/fail/skip results. Use CTest's `--no-tests=error` for
focused CTest selections and verify actual executed cases for direct binaries.

Behavioral fixtures remain necessary for mocks, initial state, controlled
executor/clock boundaries and cleanup. Reuse the shared manual execution
environment. A fixture repair must identify its faulty premise and preserve or
replace the intended behavioral oracle explicitly; targeting must never bypass
fixture setup, assertions or teardown.

The final R10 fixture repairs corrected premature publication assertions,
confusion between idle transport and retained cleanup ownership, native-owner
lifetimes, and an assertion before an existing color animation completed. They
were test bugs, not new targeting mechanisms or production fixes. The per-run
validation wrappers were separate orchestration/evidence scripts using existing
selectors; retain their historical artifacts, but do not repeat that pattern.
