# R18 coin selection and lock checkpoint

Application implementation is integrated at `9c99b71c8c777a977fce21f5b0cfff4534a6d3dd`,
with Core unchanged at `44901d8f64cb617714707bd025ecd83f108353c2`.
The combined focused gate at `ce039570ebe3a40f347a000372a5d0a15f60b21c`
passes339 bodies,23 pinned Clang23/Bitcoin-plugin entries, QML lint, app lint
and five ordinary IPC/lifecycle scenarios. All84 focused R18 bodies pass on
that same source, including the final translation change. The receipt is
`.validation/qt6-r11-r40-20261114/integration-11/ACCEPTANCE.json`.
Final exhaustive qualification remains pending R42.

The background owner distinguishes catalog, explicit Automatic/Manual policy,
committed selection, temporary edits and lock marks. Empty, missing, externally
locked or newly immature Manual inputs cannot silently broaden preparation to
wallet-wide inputs. Preparation rechecks selected locks on the Native executor
before unlock/construction. Coin lock commands query the original public wallet
at the effect boundary, retain per-item outcomes and reconcile partial success.
Cancel restores the committed choice with successful locks removed; it cannot
resurrect a newly locked input. Failed items retain their marks.

Root followup evidence is under
`.validation/qt6-r11-r40-20261114/R18/followup/`. There are 84 distinct passing
focused bodies across the explicitly retained inputs through `8a5f6d1d281`;
the last translation-only change at `ed8abc66104` is verified by the combined gate.
The 78-, 83- and 5-body reports overlap and must not be added together.
A general SetText fixture repair waits for enabled/focused controls before
editing: the earlier wallet-switch failure occurred before a valid fee input,
not because the selected-wallet policy had changed.

Independent parent controls at `R18/evidence/parent-controls-03/` retain actual
parent `256e850c979217fc10789d07a006f94105d5cee1` failures for an externally
spent Manual input and avoid-reuse balance, plus a passing disabled-policy
control. Current production `55e294eafff478db8f95875548de2bacdf171072` passes all
three. Earlier setup failures are retained and are not counted as reproductions.
New controlled production-model schedules reproduce and fix lock/Cancel baseline
resurrection and partial validation before/after Native effects. Real Core tests
cover maturity reversal, external RPC lock, wallet switch, original selection,
recipient/rate retention, preparation, and lock persistence refusal.

| Scope | Disposition and evidence |
| --- | --- |
| 003, 030 | Original real spent-input failure; explicit Manual reconciliation and no wallet-wide fallback. |
| 009 | Parent/current avoid-reuse comparison with enabled and disabled controls. |
| 011, 222 | Validate actual outpoints and retain failed marks. The earlier wrong-live-output premise is narrowed, not invented as a reproduction. |
| 017 | Real maturity reversal at preparation; authoritative construction remains Core-owned. |
| 032 | Synthetic 5,000-coin production-model trace: 157 turns, at most 32 rows per turn, about 0.16 seconds total and under 0.4 ms maximum measured presentation turn. This is not a full visible GUI or historical six-second benchmark. |
| 038 | Core persistence bug reproduced; public-interface app refusal workaround and retained upstream note, not a Core fix. |
| 217, 218, 224 | Explicit input intent survives wallet switching, equal values, missing rows and Apply/Cancel; real Core/UI counterpart retained. |
| 219, 220, 223 | Native current lock queries and explicit refresh reconcile externally observed RPC lock state; public atomicity limitation retained. |
| 221 | Per-item partial success/failure, unchanged selection and cancel baseline tested with manual executors. |
| 267 | Source/inventory candidate remains unpromoted: no reachable rounding defect is claimed. Canonical satoshi values and actual boundary assertions remain required. |

O14 has composed production-model manual-executor schedules plus separate
physical UI/Core counterparts. The selection picker deliberately disables lock
context actions; tests must not force an unreachable bulk-lock UI. O35 is owned
by R39, and cross-consumer 266 belongs to R21. These owners remain explicit.
Pinned app lint/tidy and merged runtime/lifecycle qualification now pass.
Exhaustive coverage, sanitizer and platform/configuration suites remain R42.
