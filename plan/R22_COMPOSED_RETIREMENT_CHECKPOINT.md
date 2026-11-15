# R22 composed retirement checkpoint

Exact tested and integrated app:
`e3508980eb283a4ab939aec2eda32ce2b9db97ba`; unchanged Core:
`44901d8f64cb617714707bd025ecd83f108353c2`.

An accepted PSBT Save survives loss of its GUI projection for a fresh path.
If destination inspection requires an unanswered overwrite decision, projection
retirement produces one Native Retired receipt. Retirement during inspection,
before Start admission returns, or after a write is admitted preserves the
original effect's actual meaning. A reserved Cleanup receipt shares a private
action key with file work, so queue pressure cannot silently drop that outcome.
Generic submit-only wallet backup consumers keep their existing interface.

`evidence/R22_COMPOSED_ACCEPTANCE.json` records commands and raw paths under
`.validation/qt6-r11-r40-20261114/R22/composed/`.

| Check | Actual result |
| --- | --- |
| FileExport operation class | 20/20 pass |
| Shared backup operation class | 73/73 pass |
| Wallet backup consumers | 6/6 pass |
| Original PSBT admission schedules | 21/21 pass |
| Save/preparation continuation schedules | 23/23 pass |
| Composed signer facade schedules | 15/15 pass |
| Expanded WalletOpening class | 57 pass,2 fail; zero skips |
| Actual Core import/sign/PSBT-save/broadcast/known identity | 1/1 pass |
| Actual signer substitution process | Pass; exact retry succeeds |
| IPC normal/fatal lifecycle | Normal exit0; actual Core failure exit1; both pass |
| Pinned Clang23/Bitcoin tidy | 5 changed C++ plus7 generated MOC entries pass |
| Exact-head app lint and production QML lint | Pass |

The expanded opening failures are
`unsupportedOrUnconfirmedPreferenceNeverClaimsPersistence:no-address-capability`
and `queuedPreferenceChangesKeepLatestOutcomePending`. The unchanged four-body
selection at test-only parent `7d3aa493de049bb3f57914ee67d13dfeb75c11c6`
reproduces both failures with two controls passing. Its production code equals
accepted `0224c4c0`/`71b5080f`, and the opening test file is byte-identical.
Those preference policy/fixture questions remain explicit reconciliation debt;
the expanded class is not reported green and its oracles were not weakened.

The first combined process driver used one unsupported startup selector and
returned2 before executing lifecycle scenarios. That result is retained.
`lifecycle-02` runs the two supported scenarios against identical binary hashes,
with no rebuild; both GUI exit statuses and stderr are inspected. The Core
save and signer checks ran independently and passed in `final-01`.

R22's affected focused gate and R23's prerequisite are satisfied. Full suites,
exhaustive coverage/platform/configuration checks and sanitizers remain pending
at R42. This receipt does not assert that the entire repository test suite passes.
