# R10 continuation: parallel integration tests

Working code candidate: `cb22132687ec765abe599628f5ed6587d63b44c9` on
`work/qt6-r10-completion-20261113`, based on fetched completion head
`fc54e7408398e89958a8eb04ccf12885b2fd3985`. The seven optimization commits
through `7693db73bee4842c94f8edd95aa864228944830a` are cherry-picked from
`experiment/pr1090-parallel-tests`. Bitcoin Core remains
`44901d8f64cb617714707bd025ecd83f108353c2`.

**R10 remains unaccepted; R11 is unstarted.** Production application source,
Core code/pin, the accepted program head and shared finding registers are not
changed by this continuation. All changes are local commits. The original dirty
checkpoint checkout is preserved.

## Changes

Integration CTest groups run with up to four CI jobs. Sending uses two disjoint
partitions; explicitly selected sending/PSBT cases reuse private copies of a
cleanly stopped funded-wallet seed. Preserve the refactor's transaction-policy,
fee-bump and other added regressions, existing QML/static/sanitizer jobs and true
wallet-disabled support.

The adaptation guards wallet-only seed generation and checks, restores executable
permissions on the imported Python checks, and uses an explicit directory queue
instead of recursion under Core's tidy policy. The seven localized sending rows
are explicitly opted into the seed; their keyboard/clipboard/IME, display-unit,
exact decoded amount and empty-mempool assertions remain unchanged.

The pinned Core console-policy test now receives its source filename from CMake,
so it also works with relative `__FILE__` values and CTest's different working
directory. The original policy and redaction assertions remain intact.

The node journey reuses the visible Settings page and waits for the real nested
node/section stacks. A failing TSan object dump contains **three SettingsView
instances in node mode**: the hidden node button's click handler pushes another
instance on each section change. This corrects the earlier completion report's
single-instance analysis. The new fixture asserts node stack depth two and keeps
all geometry, proxy, language, restart, reorg/history and RPC checks.

## Validation

[Machine-readable results and provenance](evidence/R10_CONTINUATION_VALIDATION.json)
record exact source heads, immutable image IDs, commands, counts and retained
failures. Raw logs/scripts/builds are in the workspace directory
`.validation/qt6-r10-continue-20261113/`.

| Check | Result |
| --- | --- |
| Final ASan/LSan/UBSan/integer unit and fixture checks | Three CTest selectors pass; 2706 unit body passes, twelve fatal delegations paired with the passing fatal child-process selector |
| Final parallel ASan integration | Ten groups, 185 body rows, zero failures/skips; 319 seconds at four jobs |
| Localized sending regression | Seven rows pass separately and in the final combined integration run |
| Wallet disabled / IPC enabled | Native Qt 6.10.2 build; all three node groups and 36 body rows pass |
| Pinned clang-tidy 23 / Bitcoin plugin | 214 units checked initially; 213 pass and the imported recursive copier fails. Both changed units pass the affected rerun; remaining units are unchanged |
| Repository lint / actionlint | Both app and Core scopes pass; actionlint 1.7.12 passes |
| Corrected node process journeys | Two scenarios pass under ASan and frontend TSan; recorded GUI processes exit zero |
| GUI and independent Core IPC lifecycle | `interface_gui.py`, `interface_ipc_init.py`, `interface_ipc.py` pass under ASan; GUI raw stderr/exit inspected |
| Existing ASan node/sending/console journeys | Nine of eleven pass; the two confirmed legacy-fee matrices remain failed. Twenty fee oracles execute; do not claim all 24 passed |
| Frontend TSan node/sending/console journeys | Eleven scenarios pass when combining the corrected node fixture with seven sending and two console journeys; all 24 fee oracles execute |

The initial unit run used the wrong container permission/HOME setup. Its five
failures and two permission skips remain recorded; the corrected CI context runs
the permission-sensitive rows and passes the complete unit selector. The failed
Ninja launch with the test stack limit applied during compilation is retained
separately. Neither setup failure is an application pass.

The other twelve initial non-integration CTest selectors, including QML, fatal
children, lifecycle and sanitizer controls, pass on the same unchanged relevant
source. The final rerun covers the changed integration units, complete unit
selector and fixture checks; it does not repeat unchanged QML tests.

TSan deliberately reuses the retained frozen34 production ELF after comparing
production source hashes and runtime binary hashes. New Python fixtures are
executed directly. This qualifies those GUI journeys, not the new C++ runner or
a new complete TSan/platform build. An IPC-enabled GUI build/lifecycle is not an
active GUI IPC listener; the two Core IPC protocol checks are separate.

## Remaining work

1. Investigate the ASan confirmed `legacy-fee-v0` / `legacy-fee-v2` process
   journeys: `transactionReviewPopup.opened` remains false. The GUI exits zero;
   TSan and ordinary-runtime passes do not close this failure. Separate existing
   coin-selection/PSBT work in the original dirty checkout remains untouched.
2. Resolve the Qt 6.2 MainRouting teardown attribution. The detailed checkpoint
   leaves ownership unproved; the older completion JSON's native-defect label
   is not sufficient evidence for deferral.
3. Retain the user-directed Qt picker deferral and the existing coverage,
   static-depends, MSan, configuration and platform obligations. No new coverage
   percentage, Qt 6.2 pass, Windows pass or blanket R10 acceptance is claimed.

No original finding, scope or schedule is closed by these component results.
