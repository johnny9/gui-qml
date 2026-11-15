# R19 preparation and immutable review checkpoint

Worker `84b985e9b1db05a29c8c6838aa586aa5ae8608a1` is integrated at
`7a19c78e6ee09191d79d34d0c028d5acbdcc3ec6`. The focused merged gate passes
at app `7fbc2126c06f5a3f403020165167e548e13e595f`, with unchanged Core
`44901d8f64cb617714707bd025ecd83f108353c2`.

transactionattempt::Owner serializes admission, intent/review revisions,
Prepare/Review/Revalidate decisions and immutable candidate installation on
Domain. The Qt model captures facts and intents and projects snapshots. Public
wallet effects use the existing Native executor and original opening/action.
Display-only changes preserve the candidate; stale results, draft edits,
failed preparation, wallet close and successor attempts retain exact ownership.

The worker runs42 distinct focused/original bodies. Three exact-parent controls
fail at their intended canonical-recipient classification, stale send admission
and stale QML button assertions. The merged gate executes248 bodies with no
failures/skips,17 pinned Clang23.1.6/Bitcoin-plugin entries,247 QML files per
compatibility variant, app lint and one actual Core/HWI scenario. Two old R18
tests now await the Domain rejection and still require the exact request ID and
zero Core transaction construction. The first failed merged report is retained.

RC80/draft307's preparation-time chain-limit allegation is refuted at this pin:
automatic and manual maximum use the ordinary public createTransaction builder,
whose final Core checkChainLimits runs before success under the existing default
policy. No lowered-cluster-limit app test or new Core functional run is claimed.
The separate RC11 submission/result issue belongs to R20. Source-only candidates
and the sound unload control are not promoted into reproduced defects.

Evidence: `.validation/qt6-r11-r40-20261114/R19/FOCUSED_ACCEPTANCE.json`,
`R19/evidence/OWNER_HANDOFF.md`, and
`integration-12/ACCEPTANCE-02.json`. See [scope dispositions](R19_SCOPE_MAP.md).
Final coverage, full matrices, Windows and sanitizers remain R42.
