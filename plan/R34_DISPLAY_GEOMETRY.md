# R34 focused acceptance

Tested application `be48cf5527fcb026c7fafe9c2ec92d9d54267dd6` is based on accepted `3806a197d05b1caebdf87896df1916c914c17ae1`. Core remains clean at `44901d8f64cb617714707bd025ecd83f108353c2`. This note changes no tested implementation or test bytes.

| Primary scope | Result and oracle |
| --- | --- |
| 044 | A bounded pure geometry policy and Qt native handoff restore a reachable rectangle. Original parent wallet/node processes retain200×100 and fail. Both current shells clamp, persist the actual rectangle and restore it after restart; eleven value rows cover boundary/screen cases. |
| 067 | Native reads use Core's actual default and total-cache range. Developer inputs reuse the shared typed editor. Real Core checks the default, deliberate450 persistence and running/next-start distinction; original cap/default controls fail. |
| 258 | Native catalog validation rejects unavailable app inputs. Repair only owns the GUI fallback source; external Core/CLI requests and independent GUI preferences are retained, with unavailable translations labelled explicitly. A repair-parent control proves the source boundary. |
| 259 | Persisted-unit/restart consistency is hardened. Widgets divergence alone is not promoted into an arithmetic defect. Four units×four locales, incomplete input, bound QML values and physical unit toggles preserve canonical satoshis. |
| 285 | Real external QSettings writers refresh existing display/window projections. Coalesced Native reads wait for pending writes. Exact intent-field writes preserve unrelated settings; retry queries actual state without write replay. Manual/real-worker tests cover held GUI delivery, rejection and retained outcomes. |

No Core/Qt changes, new executor framework or replacement native component. Settings/filesystem work and catalog validation run off GUI; GUI does bounded presentation and necessary Qt screen/window/watcher handoffs.

## Executed focused gate

Artifacts remain in workspace `.validation/qt6-r11-r40-20261114/R34/`:

- `bazzite-05/artifacts/green/results/report.json`: **110/110 Native/Core bodies**, eight nonempty selections, matching source manifests and configure/build/runtime success. Four bodies use real Core/settings pages.
- `macos-05/artifacts/green/focused/report.json`: **9/9 QML bodies**, two nonempty selections, matching source manifests and configure/build/runtime0.
- `bazzite-05/artifacts/green/process.*`: **3/3 GUI scenarios** on Xvfb1920×1080/xcb, including both shell restarts and independent INI readback. Original1200×800 assertion unchanged; exit0 and empty stderr.
- Original3806 controls: seven Native, one QML and two process failures as intended. Source03 repair-parent05 adds three intended assertion failures, zero infrastructure failures. They are negative evidence, not head passes.
- Pinned Clang23/Qt6.10.2 tidy: current four CPP plus seven mandatory MOC aggregates pass11/11 in `local-05`; six unchanged CPP passed `local-04`, with current headers/MOCs checked. Both250-file QML compatibility variants and ten negative gate probes pass with zero warnings/errors.
- Repository app lint passes in the qualified Core449 image (`lint-05`); final documentation-head lint is retained separately.

Exact selectors: `native-05.json`, `qml-05.json`, `repair-red-05.json`. Native/QML command: shared `test/run_focused.py --build-dir ... --manifest ... --output-dir ...`. Process command: `test/functional/interface_qml_node.py --case wallet-shell-geometry --case wallet-clamped-geometry-restart --case node-clamped-geometry-restart`. Repository tidy uses `ci/clang_tidy.py --files ...` with the pinned plugin and all app MOCs retained; app lint uses `ci/lint.py --no-build --scope app --image qt6-r17-linter-core449:20261114`.

All earlier failures remain. Source03's benign1200×800 fixture failed on minimal's240×320 display, where the supported minimum is800×665. The final run changes the display environment and preserves the assertion. Earlier107/9 positives are historical, not additional final bodies: **119 Qt bodies plus3 process scenarios** pass at current code.

## Qualification still pending

User-deferred R42 owns exhaustive coverage, sanitizers and complete platform/configuration matrices including Windows. Registry-backed preferences use explicit/page/activation refresh; automatic file notifications qualify file-backed stores only. QSettings has no cross-process compare/exchange; simultaneous uncooperative same-key writers have no atomic guarantee. Unrelated intent keys are preserved.

A potential desktop default-factory namespace retarget if application identity changes during a held effect is a source-review candidate for R35's profile/namespace scope072. No extra R34 namespace rewrite or runtime-defect promotion was made.
