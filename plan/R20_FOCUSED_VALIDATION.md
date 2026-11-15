# R20 focused candidate validation

This records the focused candidate checks; it does not accept R20. The
candidate is based on application revision `0449765cf907b61e939694b30c51bff7180fe982`
and Bitcoin Core `44901d8f64cb617714707bd025ecd83f108353c2`.

## Results

- Focused ordinary validation: 9 selections, 139 executed test bodies, 0
  failures and 0 skips. The retained raw QtTest `Totals:` lines sum to 157
  passing records: 139 selected bodies plus 18 `initTestCase`/`cleanupTestCase`
  lifecycle records (two for each selection). The earlier total of 148 is
  superseded: it is not derivable from this retained report. Detailed report:
  `.validation/qt6-r11-r40-20261114/R20/evidence/focused-26/actual/report.json`.
- Latest affected rerun: 2 selections, 90 bodies, 0 failures and 0 skips:
  `WalletSendTests` (87) and
  `sendOutcomeReportsObservationOrRejectsStaleReview` (3). Report:
  `.validation/qt6-r11-r40-20261114/R20/evidence/focused-28/actual/report.json`.
- Earlier failed focused attempts remain unmodified under `focused-02` through
  `focused-24`; they are not included in the final passing counts. Their
  individual reports and raw process output remain available beside each run.
- QML lint passed in focused-27. App and pinned-Core lint passed with
  `python3 ci/lint.py --scope all`; evidence is in
  `.validation/qt6-r11-r40-20261114/R20/evidence/lint-02.log` and
  `lint-02.exit`.
- Pinned Clang 23 tidy passed for the final changed C++ and generated MOC
  selection (10 changed C++ translation units and 7 MOC aggregates), with no
  findings. The initial test-only findings and their repair are retained in
  `tidy-01/results.json`; final recheck is `tidy-02/results.json`.
- `git diff --check` passed.

The focused candidate also fixes typed password-prompt routing during
preparation and replaces an invalid pre-spent-input “mempool rejection” case
with a real competing-spend/stale-review Core/RPC regression. The competing
spend case asserts the review is no longer sendable and no send outcome or
reviewed transaction is recorded.

## Decision still required

`interfaces::Wallet::commitTransaction()` returns `void`; the public interface
does not distinguish a Core refusal from broadcast being disabled. The app
does not infer either reason from `NotInMempool` and does not rebroadcast.
R20 acceptance remains held pending the user decision recorded in
[`R20_PUBLIC_RECEIPT_LIMIT.md`](R20_PUBLIC_RECEIPT_LIMIT.md). No sanitizer,
coverage, or broad cross-platform result is claimed here; those qualifications
are deferred to R42 under the current validation cadence.

The machine-readable focused validation record and raw reports are under
`.validation/qt6-r11-r40-20261114/R20/evidence/`.
