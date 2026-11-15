# R22 save continuation reopened

Resolved at the [merged focused checkpoint](R22_R30_MERGED_CHECKPOINT.md).
The original21 timing cases pass after the background continuation, withdrawal
and immediate lifetime guards. The historical diagnosis below remains evidence.

The earlier focused checkpoint remains valid evidence for its selected198worker
and387merged cases. It missed the existing `psbtAdmissionKeepsOriginalEffectAndView`
schedules. Expanding those during R23 qualification found an application ownership
gap: when the PSBT must first be prepared, accepted Save waits for GUI
`publishPsbtAction()` to call `startPsbtFileExport()`. A quiet/closed GUI therefore
prevents admission of the original file write. The delete row also exposes unsafe
fixture cleanup after its failed oracle; preserve the failure and repair cleanup.

Root evidence is workspace `.validation/qt6-r11-r40-20261114/R23/head-03/actual/`.
The file-existence oracle remains required. Its malformed XML follows the actual
process crash; it is not an empty passing selection or an environment blocker.

The signing/export worker owns a separate R22 repair using the existing backup
operation state and Services. A per-opening backend continuation must receive the
original retained document/path/review on Domain and admit inspection/write without
GUI delivery. The GUI projects typed phases and supplies overwrite consent only.
Reserve Native ownership before admission and preserve close/refusal policy.

This is solvable in the app. No Core/API change or custom native chooser is
needed. R22 acceptance is reopened until the original quiet/delete schedules,
confirmation/refusal controls, exact bytes and cleanup ownership pass. R23 continues
its independent checks. R42 exhaustive qualification remains pending.
