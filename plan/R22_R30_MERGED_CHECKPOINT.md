# R22 and R30 merged focused checkpoint

Tested code9c71709c629b4b07029c7afd2aabe6f2eec5b0c0; unchanged Core
44901d8f64cb617714707bd025ecd83f108353c2. Root receipt:
`.validation/qt6-r11-r40-20261114/integration-17/ACCEPTANCE.json`.

The combined Gameboy gate executes294 distinct bodies:265 native,20 QML and9
actual Core integration. All pass with no skipped/incomplete selections. This
includes the unchanged21 original Save timing cases,23 Save/preparation cases,
typed receive/editor/sharing work, PSBT/fee projections, independent spends,
external unload/reopen, fresh receive affinity and original QR admission controls.
Pinned Clang23/Bitcoin-plugin tidy passes13 affected C++ units plus7 fresh MOCs.
QML lint checks259 files per variant; app lint checks the exact merged range in
an isolated clone and its app/Core identities are rechecked. All pass.

Two actual GUI IPC lifecycle scenarios pass on the same hashed merged binary:
native startup failure/recovery and bridge protocol. The expected failed GUI
exits1/markerfalse; recovery reaches actual Core readiness, retains exact550MiB
and exits0/markertrue. GUI stderr and exits are inspected; both outer stderr
files are empty. The build is mounted read-only for these runtime checks.
The initial wrapper's directory-permission failure executes zero scenarios and
is retained separately; it is not an application failure or passing selection.

R22's reopened continuation and pre-admission withdrawal/deletion gaps are now
resolved at the focused checkpoint. R30 passes with its existing user-approved
scope007 upstream limitation. Preserve the worker's explicit earlier-source
QML/Core carries and independent QR decoding; do not sum overlapping reruns.

The merged R23 typed-policy/ownership portion also passes this composition gate.
Its additional Native adapter for the existing public Node `testmempoolaccept`
read is still pending before final R23 acceptance; no new Core API is required.
R35's proposed-directory boundary decision remains held independently.

All131 compilation calls are cacheable:68 direct hits,63 changed-input misses,
content-based compiler identity and32GiB limit. All370 actual Core target
commands remain identical; no semantic flags are ignored. The last Gameboy
compiled source is this integration17 source. Runtime-only checks do not
change build inputs. Final exhaustive coverage, full configuration/platform
matrices, native Windows and sanitizers remain explicitly pending at R42.
