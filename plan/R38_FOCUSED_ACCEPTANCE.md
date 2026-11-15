# R38 focused acceptance and composed delivery

Tested application: `56812dd840ad34a5451539f7b5fd47142029bd90`.
Bitcoin Core: `44901d8f64cb617714707bd025ecd83f108353c2`, unchanged and clean.
Accepted composition base: `2718d5981b6775992873187dbbd1737a934e9180`
(R27/R36/R37/R39). Parent `5ca18bd843a` changes progress notes only.

The console retains the original admitted command, explicit Node/wallet endpoint
and outcome independently of Clear or a view. Redaction precedes durable
history/display; unavailable wallets cannot become implicit Node requests.
Autocomplete hover and keyboard consent are distinct. Log reads validate the
opened file/offset/anchor and activation, bound work/storage, and retire without
canceling unrelated commands. Current chain information follows public readback
and notification versions, including real header invalidation/recovery.

Composition also corrected a settings pipeline defect: admitting a two-MiB write
inside the still-charged two-MiB review callback left no room in the unchanged
four-MiB ordinary budget for unrelated work. One native Domain turn now dispatches
the original write after review release, using the original scope/token and the
accepted Save already retained in State. No large Save is captured by the small
handoff, no budget/flag is weakened, and no retry or GUI wait is introduced.
Refused or unstarted handoffs retain a known no-write receipt. Five manual cases
cover pressure, later edit, parent close/retirement, refusal and cancellation.

## Executed evidence

Evidence is retained under workspace
`.validation/qt6-r11-r40-20261114/R38/`.

- `gameboy/composed-07/artifacts`: **102/102 current Qt bodies**: full
  `SettingsEditorModelTests`38 (including real workers), SettingsNavigation45,
  EffectiveSettings14, and five public Core settings rows. Both real GUI privacy
  scenarios passed, with their actual process exits0 and raw pipe bytes retained.
  No skipped/empty selections or setup failures passed as tests.
- `COMPOSED_ACCEPTANCE.json`: current102 plus86 specifically reviewed earlier
  positive composition identities = **188 distinct composition-scope bodies**
  (Native91/QML76/Core21). This is an explicit source/caller carry, not one final
  default-suite run. `WORKER_ACCEPTANCE.json` separately records296 historical
  worker identities; these sets overlap and must not be added.
- Current configure/build/bootstrap/QML lint/pinned tidy all exit0. Tidy checked
  the two changed Cpp units plus all seven fresh MOC units. `TIDY_SCOPE_AUDIT.json`
  matches actual pinned passes to all13 changed Cpp files, plus the unchanged
  composition unit `qml/bitcoin.cpp`. Original worker01 had18 actual tidy entries
  (11Cpp+7MOC), not an empty submodule-only check. Exact-head app lint passed;
  documentation-only delivery receives its own final lint.
- `gameboy/pressure-red-01`: original settings production bytes equal accepted
  main2718. Exactly two actual bodies: the one-KiB pressure trigger fails on the
  original implementation; the unchanged no-pressure control passes. No setup
  failure. Diagnostic04/05 distinguish real click admission from write
  `Capacity`; their logging-assisted runs are not qualification.
- `red-parent-01/AUDIT.json` retains original assertions/controls without credit
  for mining-fixture exceptions. `red-parent-02/AUDIT.json` records two genuine
  public-Core stale-header failures and all four recovery controls. All failed
  worker/composition runs, diagnostic sources, and unrun superseded06 remain.

Current commands are recorded verbatim in per-selection `result.json` and
`docker-command.json`. The common runner executed:

```text
python3 /review/source/test/run_focused.py --build-dir /review/build --manifest /r38/native.json --output-dir /r38/artifacts/native
python3 /review/source/test/run_focused.py --build-dir /review/build --manifest /r38/qml.json --output-dir /r38/artifacts/qml
python3 /review/source/test/run_focused.py --build-dir /review/build --manifest /r38/integration.json --output-dir /r38/artifacts/integration
```

The strict runner retains actual discovered CTest environment/row identities.
Native/QML and Core test children used its minimal-platform policy; the two
independent Python GUI journeys used X11. Raw Native stderr contains only the
existing two exact debug shutdown lines; GUI stderr is checked by the unchanged
strict GUI checker, including the exact test-bridge messages. No new warning or
sanitizer allowance was added. Builds used pinned Clang23/Qt6.10.2, ordinary
coverage-OFF flags, content-checked32GiB ccache and j6. All370 actual Core target
commands remain identical; meaningful flags were not ignored.

## Boundaries

All17 primary R38 scope dispositions and R06 follow-up are recorded in
`R38_PRIMARY_SCOPE_MAP.json`. Fixed/parity or unpromoted original observations
are distinguished from demonstrated defects. The discovered settings-budget
follow-up belongs to R33's contract and is qualified here without inventing a
new canonical issue ID or overwriting shared registers.

Exhaustive coverage, the default full suite, sanitizers and the full platform/
configuration matrix remain R42. Wallet-enabled compilation with runtime wallet
modes is not a wallet-OFF build qualification. Native Windows/macOS log identity
paths remain for that matrix. RPC named endpoints are not stable wallet-instance
capabilities; separate public chain reads are not an atomic Core snapshot; file
identity/stamp/anchor checks are not adversarial atomic-file guarantees. Existing
Core-lock conditions remain. [Interface notes](R38_INTERFACE_NOTES.md) record
adapter guarantees, upstream opportunities, evidence and retirement conditions.
No Core/Qt subtree or persisted format changed. No push or R41/R42 work occurred.
