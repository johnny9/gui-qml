# R33 combined focused checkpoint

Application3806a197d05b1caebdf87896df1916c914c17ae1 integrates worker
639f40b7641500bf861e7aadee2dfd93eccc75ed with the accepted R21 PSBT owner
and the R24 finalized error-result guard. Core remains
44901d8f64cb617714707bd025ecd83f108353c2; the merge had no conflicts.

The combined focused gate passes246 distinct behavioral bodies:135 settings,
shared-editor and Core/RPC controls,33 signer/device bodies and78 PSBT-model
bodies. It also passes12 pinned static entries (five common glue C++ files and
seven fresh MOC aggregates), app lint and QML lint on250 files per compatibility
variant with zero warnings/errors. These reruns are not added to the worker's
272 distinct Qt bodies or counted as new finding resolutions.

The actual native startup failure/recovery scenario passes on the merged app:
the invalid proxy produces exit1 with markerfalse and retained answers, then
an explicit repaired launch reaches CoreReady, markertrue and exact550MiB with
exit0. The scenario inspects actual GUI stderr and exit statuses. Raw expected
Core diagnostics and known Qt minimal-font variants remain in its output.

Commands, source/ELF hashes, raw reports and exact limits are under workspace
.validation/qt6-r11-r40-20261114/integration-15/ACCEPTANCE.json.
The worker's30-scope map and upstream settings/history limits remain explicit.
R42 exhaustive coverage/platform/configuration/Windows/sanitizer qualification
is pending. No Core/Qt change or publication occurred.
