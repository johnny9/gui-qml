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
