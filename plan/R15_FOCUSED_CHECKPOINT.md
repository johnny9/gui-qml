# R15 focused checkpoint — 2026-11-15

R15 separates signer discovery/configuration from Qt presentation, retains edited versus effective settings, keeps signer process work away from the GUI and unrelated Core reads, and exposes explicit current/stale/error/retry states. The app uses only public Core interfaces; Core remains unchanged at `44901d8f64cb617714707bd025ecd83f108353c2`.

The focused gate passed against immutable source-05 (parent app `11a1a7f0529c6e92866ac60eb3b8c7a1a6df8cab` plus the R15 candidate):

* Linux pinned Clang23 / Qt6.10.2: 92 native bodies, 12 public Core/process integration bodies, 31 QML bodies; all 135 pass, no skips/empty selections.
* Mac AppleClang21 / Qt6.11.2: the same 31 QML bodies pass; supplemental portability evidence, not another 31 unique regression scopes or a full platform pass.
* Repository app lint passes using the image built from pinned Core's unchanged lint Dockerfile. Production QML lint passes in both app variants (246 QML files each).
* Pinned clang-tidy with the Bitcoin plugin passes all 18 changed C++ units plus all seven app MOC aggregates. After later repairs, the exact changed units and fresh MOCs pass again (two units plus seven MOCs, then three units plus seven MOCs). No check definitions or compiler flags were weakened.

The shared `test/run_focused.py` runner and manifests retain exact selections, XML identities/counts, commands, real process exits and raw stderr. Builds use the existing task cache with content-based compiler checking. Current final incremental compilation has four cacheable calls/four misses for changed inputs; this is not a claim of universal cache hits.

Original parent controls execute six bodies against unchanged parent production with test-only fixtures: five expected failures (CLI precedence two, missing signer label one, direct missing-executable setter one, catalog/picker blocked by held process one), and one explicit-disabled control passes. The same selected oracles pass on the candidate.

Useful earlier failures remain recorded: initial strict syntax errors; missing QML-test linkage on Mac with zero runtime; two incorrect mock signer-capability premises; held-view publication incorrectly delaying physical retirement; an extra independent metadata-read mock expectation; a temporary public RPC result lifetime in the new descriptor fixture; the real missing canApply notify signal; an incorrect test expectation of known failure after Core threw with an artifact; an include-style lint finding; and an unused helper rejected in the reduced parent-only fixture. Corrections preserve actual effects, uncertainty, test identity and existing success/error oracles.

Device-swap guarantees and exact scope mapping are in [R15_SCOPE_MAP.md](R15_SCOPE_MAP.md). Compatible replacement can service fixed descriptors. Incompatible replacement never retargets the operation or publishes a wrong wallet; a thrown Core call with a possible artifact remains explicitly uncertain and cannot replay automatically. Fingerprints are key-origin hints, not physical-device or cryptographic wallet identities. Actual signing/review handoff cases remain R24.

The subprocess hard-cancellation/output-bound limitation remains the already recorded public Core limitation. No invented explicit-device-selection or atomic physical-device-binding requirement is added. See [R15_UPSTREAM_NOTES.md](R15_UPSTREAM_NOTES.md).

All exhaustive coverage, complete suites, sanitizer qualification and Qt/platform/configuration matrices, including native Windows and wallet-disabled variants, remain R42 obligations under the user's revised cadence. This checkpoint claims the focused R15 gate only.

Task evidence: `.validation/qt6-r11-r40-20261114/R15/` — `local-04`, `macos-03/artifacts`, `lint-02`, `EXECUTED_SELECTORS.json`, `PARENT_ORACLES.json`, `SNAPSHOT_05.json`, and `C_HAND_BACK.json`. Raw failures are retained in prior numbered directories.
