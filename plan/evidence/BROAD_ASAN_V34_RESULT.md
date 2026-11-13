# ASan34 broad finite result

Candidate `8ae9ce530f76e3522e932997f7120af705598246`; application base `715fb5449366a9a267e6d111169f226b429947ef`; Core `44901d8f64cb617714707bd025ecd83f108353c2`. All jobs finished; broad result is **not qualified** because5 functional scenarios fail. No source, Core, flags, suppressions or shared registers changed.

| Scope | Actual result |
|---|---|
| Full CTest |20/20 selectors PASS; all9 groups nonempty|
| QTest bodies in CTest |3662 total:3650 PASS,12 exact fatal delegates,0 FAIL|
| Complete Unit subset |2718 total:2706 PASS,12 delegates|
| Complete QML subset |751/751 PASS|
| Actual dedicated fatal children |12/12 expected exit1+acknowledgment, same Unit ELF, no sanitizer diagnostic; exact skips paired|
| Independent all9 Core/RPC stages |185/185 body rows PASS,0skip, all discovered functions present|
| Extra public maximum Core/RPC |5/5 PASS|
| True Core interface_gui |1PASS; actual GUI exit0, raw stderr0bytes|
| Actual Core IPC init/listener |2/2 PASS; PyCapnp2.2.4 probePASS|
| Original sanitizer controls |7CI controls +7raw diagnostic recordings PASS (intentional UBSan/LSan failures preserved)|
| Current GUI journeys |13suites,57discovered/selected/executed,52PASS/5FAIL/0skip; exact inventory parity|

Failures are retained at `asan-current-journeys-v34/results/`:

- `interface_qml_node/settings-and-history-restart`: old fixture watches CoreSettingsModel's all-operation counter instead of the existing OptionsQmlModel user Apply receipt. Root has prepared the next-source fixture correction; no rerun is included here.
- `interface_qml_wallet_sending/legacy-fee-v0`, `legacy-fee-v2`, `legacy-fee-unconfirmed-v0`, `legacy-fee-unconfirmed-v2`: `KeyError: feeKnown` at line282. This is our incomplete automation migration: the QML property now intentionally exposes only bounded `presentation`, while the fixture still indexes old root-map fee facts. Use the original bounded Native-prepared fee entry/typed review facts with identity guards; do not restore whole-flow exposure or weaken fee/signature/RPC oracles. These4 failed before completing their6-case inner matrices.

All5 failing GUI processes shut down with exit0 and only the permitted TestBridge stderr line; no sanitizer diagnostic was present in these failures. No ASan picker case was excluded. The existing Qt picker is unchanged; its separate TSan document/defer status is not an ASan pass or an R10 acceptance waiver.

Exact executed commands, image/source/ELF proofs and individual exits: `asan-broad-orchestration-v34.json`, each job's `result.json`/`command.log`, and `PREPARED_BROAD_V34_ASAN.md`. Full body counts: `asan-app-ctest-v34/qtest-body-summary.json`; fatal raw child receipts: `asan-app-ctest-v34/fatal-child-results.json`; stage XML: `asan-integration-stages-v34/`; all57 raw journey process logs: `asan-current-journeys-v34/results/`. Combined JSON: `BROAD_ASAN_V34_RESULT.json`.

Counts for separate stage/RPC runs repeat CTest coverage and must not be added as new unique tests. This Linux profile does not establish Windows, TSan or coverage qualification. Scratch remains under each job's disk-backed tmp/profile directory for the retained diagnostics; no other task's data or active process was removed.
