# Focused testing

Use the existing binaries with a selection manifest. The shared runner performs
no builds and creates no replacement application fixtures:

```sh
cmake --build build --target bitcoinqml_unit_tests bitcoinqml_qmltests --parallel 4
python3 test/run_focused.py --build-dir build --manifest test/focused.example.json --output-dir .validation/focused-example
```

Each output directory must be new. It contains `report.json`, a result per
selection, exact commands/environment overrides, raw stdout/stderr, Qt text and
XML reports, actual body identities/counts and process exits. The runner exits
nonzero for failures or incomplete runs. Skips stay separate from passes; an
empty selection, setup failure or discovery-only invocation cannot pass.

CTest metadata is discovered once per invocation. Unit class discovery is also
performed once, without initializing QApplication, Core or test objects. There
is no separate function-discovery process for every selection. Unbuilt targets
outside the requested selection do not require a full build.

## Manifest

[The example](../test/focused.example.json) selects two unit bodies and one QML
body. Keep issue-specific selections as JSON data rather than copying a runner.
Every entry has a unique `id`, a `suite` and an optional positive `expected`
body count. Actual nonempty execution and successful setup/cleanup are always
required.

- `unit`: `class` names one registered class; optional `tests` names functions
  or `function:data-row` selectors. Omit `tests` for the whole class. Explicit
  empty lists and unknown classes are errors. The legacy class-filter environment
  is cleared explicitly so the manifest controls selection.
- `qml`: `file` is a registered basename such as `tst_formcontrols.qml`; optional
  `tests` contains `TestCase::function` selectors. Omit `tests` for the file.
  The runner uses its actual CTest command, resource path, rendering environment,
  working directory and timeout in embedded or development mode. It preserves
  the configured warning failure expressions, including Qt text redirected to
  a report. Unsupported execution policies fail explicitly.
- `integration`: `args` contains the existing integration selectors. For example,
  `["--module=node", "--wallet=disabled", "--test=networkActionsAndCoreNotificationsAgree:disable-network"]`
  selects one actual node integration row.
  Build `bitcoinqml_integration` first. The existing fixture still starts the
  production application/regtest node and owns setup, assertions and teardown.
  The configured profile directory is retained unless explicitly overridden.

Independent selections continue after a runtime failure. They run serially in
one invocation, preserving existing test-state assumptions. Use separate builds,
hosts or the existing CTest parallel groups for independent parallel work.

Warning classification follows the configured CTest policy. Raw diagnostics
outside that policy remain available for review; a focused pass is not full
platform qualification. For example, the known Qt6.11 property-cache override
warning is outside the current QML-console failure expression and remains an
explicit final-qualification finding.

`--timeout SECONDS` sets a per-process watchdog, default 180 seconds; a stricter
configured CTest timeout still applies. This does not change Qt assertions or
their existing watchdogs. Unix timeout cleanup terminates the owned process
group, including application descendants. Windows descendant cleanup remains a
final-platform qualification obligation; native Windows checks are deferred
under the refactoring cadence.

## Direct discovery and selection

For a header-heavy unit change, configure the same executable to compile only
the requested registered classes:

```sh
cmake -S . -B build -DAPP_UNIT_TEST_CLASSES='AmountParseTests;GuiArgsTests'
cmake --build build --target bitcoinqml_unit_tests --parallel 4
python3 test/run_focused.py --build-dir build --manifest selection.json --output-dir .validation/focused-result
```

The normal default compiles the complete executable. Emptying the cache option
with `-DAPP_UNIT_TEST_CLASSES=` restores that default for final suites. Typos,
unsupported classes, duplicate names and empty list entries fail configuration.
All classes requested by unit entries in a selection manifest must be included.
The existing registry, common probe providers, app libraries, test functions,
fixtures, compiler flags and assertions remain the same. Shared providers or
multiple registrations in one source can add compiled companion classes; the
manifest still controls which bodies execute. QML/Core integration targets are
independent of this unit-source filter.

`test/unit-build-selection.json` records the configured scope. The runner copies
its values and hash into `report.json` and checks focused class discovery against
the actual binary, so a stale/full executable cannot masquerade as a focused
build. CTest labels this unit target `focused-build`; passing it is not a complete
unit-suite result. Old builds without metadata remain explicitly unrecorded.

This reduces unrelated test compilation. It does not remove required changed-
scope static checks or avoid rebuilding application units that use a changed
production header. Class source declarations follow the existing one registry
macro per line convention and trigger CMake reconfiguration when they change.

```sh
build/bin/bitcoinqml_unit_tests --list-test-classes
build/bin/bitcoinqml_unit_tests --test-class=AmountParseTests -platform minimal -functions
build/bin/bitcoinqml_unit_tests --test-class=AmountParseTests -platform minimal exactInput:btc-one-sat
ctest --test-dir build -R '^bitcoinqml_qml_formcontrols$' --no-tests=error --output-on-failure
```

`BITCOIN_QML_TEST_CLASSES` remains supported. Unknown names, including a typo
mixed with valid names, fail before any class runs. Explicit empty CLI selection
also fails. Qt Test function/data-row and reporting arguments remain intact.

Focused clang-tidy uses the existing pinned driver:

```sh
python3 ci/clang_tidy.py --build-dir build-tidy --files test/test_unit_tests_main.cpp
```

This retains app MOC aggregates and the same compiler/plugin/check definitions.
For a header change, select all affected compilation units or use the complete
default scope. See [CI checks](ci-checks.md).
