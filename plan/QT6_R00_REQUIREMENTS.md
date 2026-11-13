# Requirements added from R00 qualification

Current base amendment: [base-pr1109-core44901-20261111](QT6_BASE_UPDATE.md) adopts PR #1109 head `000e49c81e60ccf6560ccdd1a93e811a5a44c475` and stock Core `44901d8f64cb617714707bd025ecd83f108353c2`, explicitly authorized by the user. R00–R08 are amended and qualified at application `e89249aa7b3074fa2a33bd02d2cccacf8e993aa6`; the `new-base gate <.validation/issue-remediation-pr1090-20261109/execution/base-update-pr1109-20261111/qualification.json>` passed. Original evidence remains historical; R09 is still unaccepted. Current static depends qualification requires the new stock Qt6.8.4 recipe (historically6.8.3). Preserve the Core subtree unchanged at this new pin and retain the upstream-note/notify-and-wait policy.

Plan amendment `r00-requirements-20261109`, requested after R00 execution. These are binding requirements of the [total refactoring plan](QT6_TOTAL_REFACTOR_PLAN.md), in addition to its architecture, finding, coverage and validation contracts.

R00 tested application `02bbdb7711f8b85685e63aa18873e25968aeb848` and Core `dc282ff31d1cc97507530a541d9cec8a8f6a6ef4`. Its `report <.validation/issue-remediation-pr1090-20261109/execution/R00/REPORT.md>`, `qualification record <.validation/issue-remediation-pr1090-20261109/execution/R00/qualification.json>` and `commands <.validation/issue-remediation-pr1090-20261109/execution/R00/COMMANDS.md>` remain historical evidence. The original R00 execution finished with an unsatisfied gate; later accepted repair/base-amendment evidence is in the current notice above. That historical requirements update alone did not authorize application repair or R01; subsequent explicit user instructions authorized the executed program and this base amendment.

The following IDs supplement the existing 318 findings, 329 regression scopes, 43 work orders and O01–O36 schedules. They do not replace, renumber or close them. The coordinator assigns bounded repair work with these IDs, owned files, parent app/Core revisions and required checks. R01 still depends on accepted R00 qualification.

| Requirement | Accountable owner | Required gate |
| --- | --- | --- |
| R00-B01 — Applicable app lint and reproducible CI entry points | Coordinator-assigned baseline tooling/repair work | Before accepting R00; maintained for every code change |
| R00-B02 — All baseline app tidy findings | Coordinator-assigned baseline repair work | Before accepting R00; maintained through R42 |
| R00-B03 — Genuine compile-time wallet-disabled support | Coordinator-assigned baseline configuration work | Before accepting R00; both build modes in later qualification |
| R00-B04 — Stable native coverage denominator and exclusions | Baseline tooling owner; R41/R42 final audit | Verified measurement before R00 acceptance; full coverage at each applicable slice/R42 |
| R00-B05 — QML/JS and unmeasured source accounting | Each consumer owner; R41/R42 final audit | With migration of each consumer; complete at R42 |
| R00-B06 — Complete sanitizer scope and concurrency/memory profiles | Baseline tooling owner and each implementation owner | Before accepting R00 for its ASan scope; relevant profiles on changes and at R42 |
| R00-B07 — Supported platform/toolchain matrix | Platform/consumer owners; coordinator schedules R42 qualification | On affected platform changes; complete at R42 |
| R00-B08 — Core compatibility and tracing gaps | Coordinator-assigned Core validation work | Dedicated coverage before R42 acceptance |
| R00-B09 — Valid selectors, counts, lifecycle and metadata tools | Baseline tooling owner and every worker | Every validation report; applicable tooling before accepting R00 |
| R00-B10 — Closure, provenance and preservation | Series coordinator | Before advancing a dependent gate and at R42 |

## R00-B01 — App lint and CI tooling

Resolve all applicable baseline app lint findings and rerun the pinned checks on actual app files. The recorded findings include:

- Missing explicit build-config includes in `qml/legacy_settings_migration.cpp` and `qml/models/nodemodel.cpp`.
- Direct `std::filesystem` use in `qml/onboarding_settings.cpp:151`.
- Trailing whitespace, including `qml/components/BlockClock.qml` and translation sources; preserve intended translated text when resolving it.
- The broken relative link in `doc/icon-policy.md`.
- Functional-test cleanup helpers flagged by the pinned `rmtree` check.
- The remaining applicable findings in the pinned Python lint bundle, including include style/guards, locale dependence and file permissions. Preserve every valid test assertion.

The complete inputs are `app lint results <.validation/issue-remediation-pr1090-20261109/execution/R00/lint/app-results.json>`, `adapted C++ scope/results <.validation/issue-remediation-pr1090-20261109/execution/R00/lint/app-adapted-cpp.json>` and their adjacent logs. Current-head Ruff passes; the older PR base has eleven Ruff errors. Do not manufacture a new repair for an already-passing check.

Supply durable app entry points/documentation for lint, configuration and clang-tidy, using `ci/lint.py`, `ci/configure.py`, `ci/clang_tidy.py` and `doc/ci-checks.md` or a documented repository-native equivalent. Reuse the pinned Core definitions, dependency/tool versions, Bitcoin plugin and suppression files. Record app and Core selections separately. Temporary R00 adapters are evidence of a baseline, not the final CI interface.

Review Core-only subtree/submodule, RPC-path, source-layout and dependency-policy assumptions explicitly. Adapt applicable checks to a nonempty app scope; document a genuinely inapplicable rule with its scope and reason. Do not delete the required Core submodule, change check definitions, suppress diagnostics broadly or call an empty `src/` selection a pass. Any policy exception needs the existing coordinator review and evidence-backed disposition.

Configure QML static analysis as a separate app check with the required imports, typed C++ registrations and supported Qt versions. Resolve valid diagnostics; identify unavailable version-specific capabilities explicitly. Retain component and warning-policy tests. Wire and verify the workflow's currently unused `QML_TEST_RENDERING_PLATFORM` option, or remove the ineffective option and document/test the actual rendering platform. Preserve the Qt 6.2 warning compatibility checks and strict warning enforcement where supported.

## R00-B02 — Complete app clang-tidy cleanup

Resolve the **80 baseline diagnostics across all 17 failing translation units**, not only an affected-file sample. Use `tidy-findings.json <.validation/issue-remediation-pr1090-20261109/execution/R00/tidy-findings.json>` for every exact diagnostic, source location and PR-base comparison.

| Translation unit | Diagnostics |
| --- | ---: |
| `qml/core_settings.cpp` | 1 |
| `qml/legacy_settings_migration.cpp` | 1 |
| `qml/models/addresslistmodel.cpp` | 2 |
| `qml/models/psbtqmlmodel.cpp` | 2 |
| `qml/models/rpcconsolemodel.cpp` | 3 |
| `qml/models/transaction.cpp` | 3 |
| `qml/models/transactionactivitymodel.cpp` | 1 |
| `qml/models/walletqmlmodel.cpp` | 2 |
| `qml/models/walletqmlmodeltransaction.cpp` | 1 |
| `qml/onboarding_settings.cpp` | 1 |
| `qml/test/testbridge.cpp` | 1 |
| `qml/walletqmlcontroller.cpp` | 6 |
| `test/test_addresslistmodel.cpp` | 1 |
| `test/test_coinslistmodel.cpp` | 2 |
| `test/test_options_model.cpp` | 49 |
| `test/test_transactionactivitymodel.cpp` | 2 |
| `test/test_walletqmlmodel.cpp` | 2 |

The check families are automatic moves, membership checks, recursion, member initialization, ineffective moves, emplacement and vector capacity. All twelve failing production/automation units reproduce the same diagnostics at the PR base. This establishes inheritance, not acceptance or a waiver. The base comparison used the head build context and does not certify a full base build.

Preserve observable behavior and assertions during style repairs. A recursion repair or other semantic change needs bounded-resource/lifetime behavior and meaningful regression evidence. A genuine false positive requires a specific reviewed explanation and equivalent protection; blanket `NOLINT`, check disabling or diagnostic deletion is not closure. Recheck the complete app compilation scope with CI's LLVM 22 and Bitcoin plugin, including the five failing test units. An unchanged-Core tidy pass does not satisfy this requirement.

## R00-B03 — Compile-time wallet-disabled support

Make `-DENABLE_WALLET=OFF` effective throughout configuration, compilation, linking and runtime capability publication. R00's `probe <.validation/issue-remediation-pr1090-20261109/execution/R00/wallet-off-configuration.json>` shows an OFF cache entry while the generated configuration still defines `ENABLE_WALLET 1`.

Acceptance requires all of the following:

- A generated disabled configuration, with wallet source/target/link dependencies correctly conditional. An OFF cache entry alone is insufficient.
- A successful node-only app build, startup/shutdown and applicable nonempty unit/component/integration/process selections. Exercise the foundation, node setup and node tools behavior available in that build.
- Typed unavailable capabilities and guarded commands for wallet features; no wallet-library or synchronous Core fallback hidden behind QML state.
- A preserved wallet-enabled default and passing enabled-wallet journeys, plus the existing runtime-disabled behavior in wallet-enabled builds.
- An explicit selector/configuration matrix. Wallet-only cases remain required in the enabled build; classify their absence from the disabled build by feature capability, retaining their ownership and protection.

Do not equate the 31 runtime-disabled integration rows with a wallet-disabled compilation. Record both build and runtime modes on the actual fix revision.

## R00-B04 — Native coverage measurement

Retain the `370-file source ledger <.validation/issue-remediation-pr1090-20261109/execution/R00/coverage-source-ledger.json>` and expand it as files move or are introduced. The R00 native denominator is **17,764 emitted handwritten lines and 8,640 branch outcomes**. Unit coverage is 12,874 lines / 5,817 outcomes; combined coverage is 14,566 lines / 6,481 outcomes. These are baseline measurements, not the final reachable-unit-testable denominator or a full-coverage pass.

Keep the unit and combined layers separate. Verify profile/binary identities, merge per binary, and retain a stable union denominator that includes unexecuted emitted locations. R00 verified 220 profiles against five ELF build IDs. Do not accept mixed-binary hash mismatches, dropped profiles or changing denominators as improved coverage.

Review the exact Qt-generated metadata exclusions and zero-hash mismatch audit in `the measured report <.validation/issue-remediation-pr1090-20261109/execution/R00/coverage-report/summary.json>`. Preserve symbols, source coordinates, macro text and reasons. Any handwritten mismatch remains a tooling blocker. Do not silently exclude compiler-generated-looking or unreachable-looking code.

Account explicitly for fatal `_Exit` paths that bypass profile flushing. Supply equivalent executable coverage evidence or a reliable measurement seam without changing fatal-exit semantics. The plan's 100% reachable, unit-testable handwritten line/branch and state/action targets remain unchanged; ordinary, component and integration execution is not a substitute for measured unit coverage.

## R00-B05 — QML/JS and unmeasured files

Establish a defensible denominator and executed behavioral protection for retained QML/JS decisions. LLVM's native percentages do not measure these files. Profiler observations, callable counts and component instantiation alone do not establish line/branch or decision coverage.

Move business decisions and transformations into the typed, deterministic owners required by the architecture; test the production logic there. For retained presentation logic, supply appropriate component/interaction tests and verified measurement of reachable unit-testable logic, with exact reviewed exclusions where needed. Preserve bindings, IME/raw input, focus/modality, enabled/error/busy states, view recreation and late callbacks.

Account for every ledger file through migration and R41/R42, including `qml/androidnotifier.cpp` and `qml/models/macdockiconhandler.mm`, which were not emitted in the Linux build. A supported platform feature needs its real producer/platform tests. A retired file needs a caller/build audit and validated removal. Missing instrumentation or platform execution does not make code unreachable.

## R00-B06 — Sanitizer completeness

Make sanitizer propagation reproducible in the app build/CI interface. R00 found that the bootstrap and QML component executables did not inherit the sanitizer flags; its external `adapter <.validation/issue-remediation-pr1090-20261109/execution/R00/sanitizer.cmake>` linked them to Core's existing `sanitize_interface`. Retain that complete scope in the production validation setup and prove it from the compilation database, not only a sanitized main executable. R00's accepted setup instruments all 140 app compilation entries.

Use the pinned compiler, optimization/warning policy, initialization/debug flags, runtime options and suppression files. Include ASan/LSan/UBSan/integer checks and the actual startup/shutdown suites with IPC compiled in. Keep raw GUI stderr and exact exits after shutdown in both runtime wallet modes. Retain an unrelated-leak negative check whenever adding a dependency suppression; no executable-wide suppression is permitted.

Run TSan for the program's concurrency/lifetime migrations and relevant real-thread tests. Provide an MSan-capable configuration with instrumented dependencies for uninitialized-memory work and its final qualification scope. Record actual selections and tool/platform limits; ordinary builds or ASan results do not establish TSan/MSan passes. Every required unavailable profile remains an explicit blocker until executable evidence or an evidence-backed supported-scope disposition closes it.

## R00-B07 — Platform/toolchain qualification

Execute and preserve the supported Qt 6.2/6.4/6.10 matrix and both wallet build/runtime modes as applicable. Add the exact static depends configuration: the pinned recipe uses Qt 6.8.3; R00's cached 6.8.4 SDK is not an exact substitute. Run native macOS qualification and affected native producers, including dock/window/dialog/intent behavior. Linux invocations of model events do not certify native macOS behavior.

Assign Windows qualification for Windows-supported behavior and native findings, using the existing VM and Windows skill. Record exact source revisions or transferred artifact provenance, execute relevant Windows builds/tests and real event producers, preserve active jobs, and suspend a VM resumed for the task after evidence collection unless it must remain active. A missing Windows job in the current app workflow does not establish Windows coverage.

The coordinator must provide the missing environments and schedule these qualifications before R42 acceptance. Preserve explicit unsupported-capability decisions; do not silently remove a supported configuration to obtain green results.

## R00-B08 — Core compatibility and tracing coverage

R00's Core sanitizer profile selected 473 functional cases: 463 passed and ten skipped. Preserve this accurate baseline and close the missing coverage in dedicated environments before R42:

- Previous-release gates: `feature_coinstatsindex_compatibility.py`, `feature_unsupported_utxo_db.py`, `mempool_compatibility.py`, `wallet_backwards_compatibility.py`, `wallet_migration.py`.
- USDT/BCC/BPF gates: `interface_usdt_coinselection.py`, `interface_usdt_mempool.py`, `interface_usdt_net.py`, `interface_usdt_utxocache.py`, `interface_usdt_validation.py`.

Supply the required release binaries, tracing dependencies, kernel support and permissions; preserve their provenance and the pinned checks. Capture the actual fired skip reason. The quiet R00 driver did not identify which USDT prerequisite failed, so source-gate names are not proof of the runtime cause. Skips are never passing executions. The app's v28/v31 compatibility journeys do not replace this Core matrix.

Keep Core and app validation scopes distinct. Retest changed Core guarantees on their actual revision, including independent RPC/IPC writers where required. A genuine version/feature retirement requires a recorded plan amendment and replacement protection or evidence-backed non-applicability, not a blanket compatibility/tracing waiver.

## R00-B09 — Selectors, lifecycle and tooling validity

Preserve all valid existing suites and nonempty selections. R00's complete CTest runs execute 47 unit classes, 863 ordinary unit rows, twelve separately checked fatal child rows, 616 QML rows and 116 integration rows (31 runtime-disabled / 85 enabled). Bootstrap, warning-policy and process checks also remain required. Preserve all eleven functional suites / 46 scenarios, including the 26 startup/shutdown scenarios. Counts must track the actual future revision as tests grow; they are not fixed maximums.

Use verified selector interfaces. QuickTest `-datatags` executed tests instead of listing them; do not certify its interrupted partial attempt or copy native QtTest discovery assumptions into QuickTest. Count executed rows from untruncated logs/results; CTest JUnit system-out truncation cannot establish complete counts. Separate behavioral rows, setup/cleanup, delegated child cases and repeated configurations.

Read Qt versions from the configured toolchain, such as CMake/qmake, without assuming a pkg-config entry exists. Match coverage through read-only ELF/profile IDs; a GUI `-version` dialog is not a terminating metadata probe. Keep invalid attempts, watchdog terminations and compiler/configuration failures separate from accepted results.

Retain real file-permission fixture coverage when adapting container execution; do not accept root-capability skips as proof of read-only-directory handling. Keep test data isolated in disk-backed task directories, short real socket paths and appropriate quota checks. Record container capability, network, CPU, path and configuration adaptations.

Use the actual retained lifecycle drivers: this baseline has `interface_qml_startup.py` and `interface_qml_shutdown.py`, not `interface_gui.py`. IPC-enabled compilation and an active IPC listener are separate claims: pinned GUI `MakeGuiInit` cannot listen. Maintain the compiled-IPC GUI lifecycle checks and independent Core IPC tests; any new GUI transport/listener requirement needs an explicit design, real producer and process test.

## R00-B10 — Closure and evidence

For each R00-B requirement, record the assigned order/owner, repair or applicability decision, exact app/Core revisions, actual commands/selectors/counts, result and artifact paths, and which gate it satisfies. Link every B02 diagnostic to its disposition. Baseline inheritance and successful unrelated Core checks do not close app findings.

Keep R00's original report, raw evidence, input snapshots and frozen packet intact. Repairs receive new execution artifacts and qualification on their actual fix head; do not rewrite the original red result or reuse it as after-evidence. Recheck branch, revisions and dirty status after lint. Preserve the original finding/scope IDs and coordinator-owned registers.

Before accepting R00 and assigning R01, close B01–B03 and the baseline-tooling/lifecycle obligations in B04/B06/B09, with passing applicable checks and reviewed applicability decisions. Assign every later-gate obligation to its consumer/platform/final-audit owner. R42 must close all remaining requirements along with all original scopes and schedules. Recording this amendment does not mark any requirement repaired, R00 accepted or R01 started.

The additive `amendment packet <.validation/issue-remediation-pr1090-20261109/refactor-handoff/amendments/r00-requirements-20261109/packet-files.json>` records the new input version and its relationship to the frozen packet. Affected future assignments must read this document and record both versions. Revalidate affected acceptance evidence when a requirement or supported scope changes.
