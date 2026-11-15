# R13 restore and migration checkpoint

Worker `e9a668128ac677a29732f1a02d1f38ad3a955239`, based on accepted R12
`3824ba47c2f5f9244e107f20a103683a45e2e79f`, passes its focused gate with
unchanged Core `44901d8f64cb617714707bd025ecd83f108353c2`. Integration commit
`2df977a789f` applies that exact R13 range. The combined focused gate at
`ce039570ebe3a40f347a000372a5d0a15f60b21c` passes:339 bodies (including
all172 R13 QtTest bodies),23 pinned Clang23/Bitcoin-plugin entries,247 QML
files per compatibility variant, app lint, and five IPC/lifecycle scenarios.
The two worker compatibility scenarios remain explicitly carried from their
recorded inputs; no combined compatibility rerun is claimed.

Restore and migration have background-owned operations with immutable original
source, target, credential prompt and result. They retain actual warnings,
partial or uncertain effects, files and recovery paths. Publication retries
do not repeat Core effects. The registry remains the facade writer, and Open
acts on the original result rather than a later selected wallet.

The worker qualifies 110 native, 49 QML and 13 public Core bodies, plus two
actual compatibility scenarios. Source10 provides the unchanged native/Core,
31 QML and production executable inputs; final source13 reruns the affected
18 Desktop QML bodies and two scenarios. Source13 differs only in those two
test fixtures. Both Qt Widgets processes exit zero with empty stderr. Five
parent controls produce two intended failures and three passing controls.
App lint, pinned Clang23/Bitcoin-plugin tidy, QML lint and strict compilation
pass for their explicitly recorded inputs.

Evidence is under `.validation/qt6-r11-r40-20261114/R13/`, especially
`FOCUSED_ACCEPTANCE.json`, `ORIGINAL_TRIGGER_DISPOSITIONS.json`,
`SOURCE_EQUIVALENCE.json` in `gameboy/run-05/`, and the raw numbered runs.
The final reduced run takes 45.53 seconds. The receipt maps carried checks
to exact source and executable hashes; these counts are not a final matrix.

Three integration resolutions retain both consumers: the QML fixture links
fee, signer and operation sources; its mock controller retains signer and
restore fields; obsolete GUI restore-name and signer-name helpers are removed.
R14's separate versioned application preferences remain in force. R13 changes
no Core/Qt code or wallet-file format. See [interface notes](R13_UPSTREAM_NOTES.md).
Exhaustive coverage, platform/configuration suites and sanitizers remain R42.
