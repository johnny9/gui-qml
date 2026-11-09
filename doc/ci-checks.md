# CI checks

The workflow in `.github/workflows/ci.yml` runs separate app lint, Core lint,
clang-tidy and sanitizer jobs alongside the Qt 6.2, 6.4 and 6.10 build/test
matrix. The Qt 6.10 build also runs [QML static analysis](qml-lint.md).
Native analysis and sanitizer jobs run in the matching Core Ubuntu container
images, including Clang's selected C++ standard-library headers. The job checks
that its image still matches the pinned Core configuration.

## Repository and Core lint

Run both lint scopes with the pinned Core CI dependencies:

```sh
./ci/lint.py
```

This builds the image from `bitcoin/ci/lint_imagefile`, then runs:

- Core's complete, unchanged lint runner against the pinned `bitcoin/` checkout.
- Core's applicable repository checks against the app: configuration includes,
  Ruff, controlled test cleanup, filesystem APIs, assertions, commit messages,
  scripted diffs, whitespace, final newlines, Markdown links and release notes.
- Core's filename/permission and Python type checks against the app, plus shell
  locale and ShellCheck checks when shell scripts are present.

Use `--scope app` or `--scope core` to select one scope. Set `COMMIT_RANGE` to
the app commits being reviewed, for example `origin/qt6..HEAD`. The GitHub job
derives this range from the event's base commit. Core has its own Git history;
the app commit range is never passed into the submodule. Its default explicit
nonempty range is `HEAD~1..HEAD`, overridable with `CORE_COMMIT_RANGE`.

The app adapter copies the pinned Rust lint runner into `build/lint/` and
changes repository paths only. App Python settings extend `bitcoin/ruff.toml`.
The controlled-cleanup exception points to `qml_test_harness.py`, which only
removes directories registered by its creation helper and rejects replaced
symlinks. Positive and negative probes verify that the adapted checks still
catch real violations. Core-specific argument documentation, subtree, RPC and
source-layout policies run in the Core scope rather than against empty app
paths. Core's files remain unchanged.

The app lint job also validates all GitHub workflows with actionlint 1.7.12.

## Clang-tidy

Install the dependencies from Core's `00_setup_env_native_tidy.sh` and the
app's Qt dependencies, then run:

```sh
python3 ci/configure.py --mode tidy --build-dir build-tidy
cmake --build build-tidy --parallel 2
python3 ci/clang_tidy.py --build-dir build-tidy --jobs 2
```

The configuration reads the compiler version, options and developer feature
settings from the pinned Core CI files. Core's compiler warnings, including
`-Wunreachable-code-aggressive` when supported, are treated as errors.
The checker builds and tests the
matching Bitcoin tidy plugin, then checks all configured app compilation units
and app headers using `bitcoin/src/.clang-tidy`. Generated code and Core's own
compilation units are excluded from this app pass. An empty selection fails.
Per-file diagnostics and results are retained in `build-tidy/clang-tidy/`.

## Sanitizers

```sh
python3 ci/configure.py --mode asan --build-dir build-asan
cmake --build build-asan --parallel 2
```

This uses Core's Clang version and address, leak, undefined-behavior, float
division and integer checks. The app keeps the pinned Core optimization/debug defaults. Explicit CMake
overrides remain available for separately labelled diagnostic builds. IPC, app tests and test automation are
enabled. The workflow runs CTest and the real `interface_qml_startup.py` and `interface_qml_shutdown.py` lifecycle
test with Core's 512 KiB stack limit, the original timeout, and stderr/exit-status
assertions.

The workflow uses `ci/with_sanitizer_env.py`. This delegates to Core's generated
wrapper when available. At the R00 Core pin, which predates that wrapper, it
evaluates only the three sanitizer exports from Core's `03_test_script.sh`,
without executing the CI job. It preserves Core's options and combines its
suppression files with the app's specific dependency rules.
`ci/lsan_suppressions.py` appends the independently reproduced Qt software
texture-cache exception while retaining Core's options and rules.
[Sanitizer exceptions](sanitizers.md) documents the narrowly scoped Qt hash
exception. The workflow uploads CTest and clang-tidy diagnostics even on failure.

## R00 configuration and scope additions

`ci/configure.py --mode native --build-dir build -- <CMake options>` provides
ordinary and coverage builds. `--profile` aliases `--mode`. A separate
`-DENABLE_WALLET=OFF` build excludes wallet implementations and links and exposes
an unavailable capability. The enabled default retains all wallet cases and
runtime-disablewallet journeys. Node-only tests retain independent unit tests,
mock-based components, process checks and stages 1–3. Wallet-only units and
stages 4–9 remain required in the enabled build.

Use `-DENABLE_APP_COVERAGE=ON` with Clang in a separate build. Preserve profile
ELF identities, per-binary merges and stable empty-profile denominators. Keep
unit and combined measurements separate; fatal `_Exit` children do not flush
profiles and need their separate expected-exit evidence.

The extra layout adapter applies Core's includes, guards, locale, translation
and argument-documentation definitions to nonempty app paths. Guard names retain
the complete `qml/` and `qml/test/` namespaces. The app's reviewed direct Boost
inventory is empty; seven inherited dependency cycles are listed in
`ci/lint-policy.json`, checked with Core's graph algorithm, and owned by their
original consumer orders and R41/R42. These inventories cannot grow or shrink
without review. Core's inventories still run unchanged inside Core.

The whole-app tidy selection includes app moc aggregates and app headers as
well as handwritten units. Bootstrap and QuickTest link Core's sanitizer
interface directly. Audit every compile entry. Retain full `ctest -V` logs,
raw GUI stderr and exact exit codes. Compile-time wallet OFF and runtime
`-disablewallet` are separate configurations. IPC is compiled for lifecycle
checks; pinned `MakeGuiInit` is not a GUI IPC listener. The independent Core IPC
scope remains separate. Qt 6.2 uses `QML_TEST_RENDERING_PLATFORM=xcb` under Xvfb
for component image tests; newer configurations use minimal/software rendering.

TSan/MSan with instrumented dependencies, exact static Qt 6.8.3, native Windows
and macOS producers, QML/JS coverage and the ten baseline Core compatibility/USDT
skips remain explicit consumer/platform/R41/R42 obligations. Linux ASan does not
certify those scopes.
