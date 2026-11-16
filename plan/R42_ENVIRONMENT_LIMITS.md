# R42 qualification limits

Windows is skipped for this R42 run by explicit user instruction. The existing
VM is suspended. Its historical build/manifest results do not establish a
runtime pass. This waiver does not waive Linux, macOS, coverage or sanitizers.

R42 remains unaccepted. Evidence below is in workspace
`.validation/qt6-r41-r42-20261115/R42/`; all Core checks retain stock revision
`44901d8f64cb617714707bd025ecd83f108353c2`.

| Obligation | Actual result and remaining limit |
| --- | --- |
| Core previous-release compatibility | Seven stock cases pass, zero skips; eleven downloaded releases pass their stock checksum checks. `core-compat-ordinary/AUDIT-03.json`. |
| Core USDT | Five actual cases skip for absent container BCC; zero pass. The host has BCC and matching kernel headers, but Docker is rootless and noninteractive host sudo is denied. No privileged tracing run is established. No kernel or privilege policy is changed. `core-usdt-01/EXECUTED.json`, `core-usdt-environment-02/HOST_PERMISSIONS.json`. |
| Static Linux Qt 6.8.4 | Actual depends build and semantic SDK audit pass, including all nine archive checks. Application configuration, static linkage, full tests and lifecycle against that exact prefix remain separate. `static-linux-sdk-audit-03/`. |
| Static native macOS Qt 6.8.4 | All nine archives verify, but Qt compilation fails with Xcode 27 / AppleClang 21 / SDK 27: `QTaggedIterator` lacks the subscript operator required by libc++ heap sorting. The earlier inventory covered only Xcode. Follow-up inspection finds CLT SDKs 15.4 and 26.5 as well as 27.0; compiler/header compatibility still needs qualification. No app build or test executes against the static SDK yet. `static-macos-02/BLOCKER.json`, `mac-sdk-followup-03/ROOT_INSPECTION.json`. |
| Native macOS application | Installed Qt 6.11.2 can build and run the app independently. A passing Cocoa bridge and native Dock unit linkage do not establish installed Dock/reopen, URI association or FileOpen producer coverage. No static-SDK pass is inferred. |
| TSan | **Skipped for acceptance by user decision 2026-11-16.** The original native Qt picker race remains explicitly deferred. The Qt engine dependency diagnostic remains separate. Existing app/fixture failures are repaired and retested independently; no whole-app suppression or replacement Qt component is permitted. |
| MSan | **Skipped for acceptance by user decision 2026-11-16.** A qualified instrumented Qt/libc++ dependency stack is unavailable. Zero app MSan tests are qualified. Core's `NO_QT=1` MSan job cannot certify the outer GUI. `coverage/msan-proof/BLOCKER.json`. |
| Coverage | Frozen 34b measurement is below 100%, includes failed executions and does not certify later heads. Native gaps, platform files without mapping, twelve native QML initializer spans and QML tool limits stay explicit. `coverage-current/STATUS.json`. |

The static macOS failure is a dependency/toolchain limitation, not authorization
to patch Qt or change the pinned recipe. Preserve the failed build and use a
compatible supported toolchain when available. Native application checks can
continue independently; they cannot close the blocked static configuration.

An unavailable tracing/MSan environment is not a passing test or an approved
scope exclusion. These limits remain in the final qualification report unless
actual execution or a further explicit user decision changes their status.

Application owner-loss, stale projection and leak failures are not covered by
these dependency notes. Their implementation/fixture owners must repair them
and preserve original effect, identity, persistence and cleanup assertions.
The [active checkpoint](R42_ACTIVE_CHECKPOINT.md) records those separately.
