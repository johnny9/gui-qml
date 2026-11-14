# R11 focused checkpoint — 2026-11-14

Implementation/focused checkpoint complete; final qualification pending R42.
The parent is `a83d301e4e982e95504b662b4e94405402feaaa4`.
The final tested candidate is `7fb5c9e8b32d9fc5e4de27389edba47a762689d2`;
Core is unchanged at `44901d8f64cb617714707bd025ecd83f108353c2`.
The branch checkpoint preserves those source/test bytes and adds this status/evidence documentation.

## Result and ownership

`WalletRegistry` owns exact live facade/opening identity, adoption and retirement.
The value-only catalog reader owns discovery, immutable row snapshots, explicit
availability/error state and generation-matching publication. `WalletSelectionModel`
owns one window's selection. The existing controller forwards to these owners;
completion retains its original result and selection changes require user intent.
QML uses typed bindings. Default-wallet identity is distinct from no wallet,
including its display and explicit `/wallet/` RPC context.

049 now reconciles an error-returning GUI load against the exact captured live
opening without changing its raw Native failure receipt or fabricating an effect.
075 subscribes to public unload notifications before first GUI publication and
releases native resources without waiting for an opening dialog to close.
137/284 keep admission closed until coherent original-result publication ends.
Native handles retire through the existing reserved handoff/Session ownership;
result metadata can remain observable after the backend is released.

## Executed focused checks

| Scope | Tested input | Actual result |
| --- | --- | --- |
| Catalog/selection/RPC/display/retained values/opening class | source06 `282f6c8eb1cd6fe9c78cb944b9480c552fae556e` | 106/106 bodies, zero skips |
| Controller ordering/admission/retirement/default identity | source06 | 86/86 bodies, zero skips |
| Entire affected lifecycle transport class | source06 | 76/76 bodies, zero skips |
| Independent actual Core/RPC integrations | final source07 | 6/6 bodies, zero skips |
| IPC startup bridge, close/quit/SIGINT during loading, Core `interface_gui.py` | final source07 | 5/5 scenarios; required GUI exit0/clean stderr; Core GUI raw receipt retained |
| App lint | final source07 | exit0, pinned app scope |
| Pinned Clang23/Bitcoin-plugin tidy | source05 unchanged units + source07 repairs/all MOCs | 89 unique affected units pass; 79 reused +10 current, including all 7 app MOC aggregates |
| QML static analysis | source05, unchanged relevant dependencies | 244 files for each of 2 compatibility variants; zero warnings/errors; 10 probes pass |
| Ordinary wallet-disabled build/unit link | source05, unchanged applicable dependencies | builds exit0;3/3 bodies, zero skips |
| Native Mac mocked QML components | source04, unchanged component/resources/build inputs | 24/24 unique bodies, zero skips;3 known Qt6.11 property-cache warnings retained |

The 268 native source/test bodies and production headers in source06 are identical
to final07. Only the integration test implementation differs; its 6 bodies were
freshly compiled/executed at final07. This is explicit reuse, not a claim that a
fresh final07 unit ELF was executed. No profiles from different ELFs were merged.
The Mac component result does not qualify the changed native C++ application.
Wallet-OFF remains distinct from runtime `-disablewallet`.

The 6 independent integration selectors are
`defaultWalletRpcContextIsExplicitWithMultipleWallets`,
`completionRetainsItsOriginalResultAndNeverSelectsIt`,
`closingAndReloadingReplacesTheModel`,
`selectorActionsTargetTheirOwnRowAndRequireCloseConfirmation`,
`rpcLoadRacingPickerOpenKeepsTheGuiIntentAndSelection` and
`rpcLoadAndUnloadBeforeGuiAdoptionKeepsPickerOpen`.
They use normal fixtures and `test/run_focused.py`; manual cases use the existing
controlled executors and real-thread lifecycle tests remain included.

## Scope accounting and failure evidence

All 14 primary scopes remain mapped in the retained R11 scope report.
049,075 and137/284 have mechanism-matching regressions. 064 retains its recorded
fixed/control disposition. 065 has actual empty-identity load/select/close and
RPC-context controls. 129/130/131/133/134/135/281 remain accurately classified as
source candidates/controls rather than newly promoted defects. 316 preserves the
user's generic Software/watch-only/external-signer capability policy; exact
wallet-level multisig classification remains unsupported and deferred.

Independent parent negative controls reproduced049 (4 failed bodies),137/284
(2 failed bodies) and075 (1 failed Core/RPC body). For075, production parent3059
with only the compatible test added failed the unchanged 30s RPC unload oracle;
failure-only recovery close then released it. These are negative evidence,
not positive passing tests. Original source05/source06 failures remain retained: source05
had 4 lifecycle ownership failures and integration compile errors; source06
passed all 268 native bodies but integration compiled0 bodies because of an
unused constexpr capture. Final repairs preserve the original assertions and
strengthen handler disconnect/destruction-lane checks. Earlier failed builds,
SQLite retained-backend failure, fixture errors and tool/environment failures
are retained in their original artifact directories.

## Evidence and limits

Workspace evidence root:
`.validation/qt6-r11-r40-20261114/R11/`.
Exact source/Core identities, commands, manifests, per-body XML, actual exits,
stderr and binary identities are recorded in `runtime-acceptance-07/RESULT.json`,
`gameboy/run-06/artifacts/`, `gameboy/run-07/artifacts/`,
`ipc-focused-07/`, `FOCUSED_REUSE_07.json`, `lint-07/`, `tidy-05/`, `tidy-07/`,
`qml-lint-05/LANES_RESULT.json`, `bazzite/results-05/`, `macos/remote04/`,
`scope049-red/`, `scope137-284-red/` and `scope075-parent-red-06/`.
The overall runtime receipt contains the authoritative actual per-lane paths.

[Public-interface follow-up](R11_UPSTREAM_NOTES.md): the startup `getWallets()`
snapshot and per-handle subscription are separate public operations. A durable
opening status or atomic subscription receipt would improve that broader
boundary. The original075 notification route is independently red/green here;
this checkpoint does not claim a broader atomic startup guarantee. No Core or
Qt source/pin changed, and no native Qt replacement was introduced.

Core lint/tidy definitions and plugin/toolchain are preserved. Per-step full
coverage, full suites/platform/configuration matrices and sanitizers are deferred
under the [approved cadence](R11_R40_VALIDATION.md). Native Windows/Mac, static
depends/Qt matrices, instrumented dependencies, Core IPC API qualification and
the known native Qt picker TSan result remain R42 obligations. Existing Qt
plugin/font diagnostics and shutdown debug traces remain recorded. Gameboy's
inherited continuous-coverage ccache parser limitation is distinct from capacity;
flags were preserved and cache directories were not cleared. No exhaustive
coverage percentage or sanitizer pass is claimed.

The unrelated original worktree retains its HEAD, 11-path dirty status and all 10
initially fingerprinted contents. One initial path had no digest recorded;
no writes were made to that checkout. Canonical shared planning/finding/schedule
registers remain unchanged: 318 findings, 329 scopes, 43 orders, 36 schedules.
This checkpoint was committed locally; no publication is authorized here.
