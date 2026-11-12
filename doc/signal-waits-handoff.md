# Signal-driven integration waits: handoff

Continue the experiment replacing polling waits in [GUI-QML PR #1090](https://github.com/bitcoin-core/gui-qml/pull/1090), then measure whether the integration suite becomes faster. Work remains before this is ready to merge.

## Branch and revisions

- Fork: [`johnny9/gui-qml`](https://github.com/johnny9/gui-qml).
- Branch: [`experiment/pr1090-signal-waits`](https://github.com/johnny9/gui-qml/tree/experiment/pr1090-signal-waits).
- Implementation commit: `1151305e484faceb9866ad0fd72ca03165d45164`.
- Parent, the published PR head used as the baseline: `1137c33186d24cabdd33813e94d039bedbf51b0c`.
- Bitcoin Core submodule: `dc282ff31d1cc97507530a541d9cec8a8f6a6ef4`, unchanged.
- PR target: `qt6`; the workspace's staging-only ancestor validation rule does not apply.

The original local `optimize-functional-tests` checkout has unrelated uncommitted changes. They were preserved and are absent from this branch.

## Implementation

All explicit `QTRY_*`, `QTest::qWait*`, and future `wait_for` polling sites under `test/integration` were converted. Python functional and QML component-test waits are outside this experiment's scope.

- `fixture/test_wait.h` and `.cpp` provide `SignalWait`: an initial predicate check, notification subscriptions, an event loop, and one timeout. There is no periodic timer to recheck the predicate.
- `Property` subscribes through `QMetaProperty::notifySignal()`. `Rows` observes model changes. `Ui` observes construction and readiness in the QObject and visual trees. Frame and destruction notifications cover the corresponding conditions.
- `Async` uses `QThread::create`, `std::packaged_task`/future for results and exceptions, and `QThread::finished` for completion. Its ordinary event loop also works during failure cleanup, where `QSignalSpy::wait()` can return early because the test has already failed. A worker timeout terminates the supervised child rather than destroying objects still borrowed by that worker.
- `RpcConsoleModel::commandStarted` forwards worker entry to the GUI thread. The responsiveness test uses it instead of polling `getrpcinfo`.
- Wallet closure tests observe retirement/destruction before checking Core's wallet list. `openWalletsChanged` alone occurs before backend closure finishes.
- Click retries are driven by frame/readiness notifications. A delivered but canceled press still fails without repeating the gesture.
- `bitcoinqml_signal_wait_tests` adds seven focused cases: synchronous emission, intermediate property values, unrelated signals not rechecking the predicate, timeout, object destruction, waiting for destruction, and worker results/exceptions. These are separate from the 122 integration cases.

## Critical: the committed optimization is untested

The last normal and sanitizer binaries were built **before the final edit to `test_util.h`**. That tested working tree version waited for a rendered frame on every polish check. It passed, but was slower. There is no separate commit for that tested working tree version.

Commit `1151305e484` includes a subsequent **unbuilt and untested** optimization:

- `WaitForPolish` crosses a queued event boundary to dispatch deferred QML work, then waits for rendering only while polish remains pending.
- The click helper checks that the scene position lies inside the window before sending input.

Rebuild and test the actual branch before attributing any test or timing result to it. Pay particular attention to deferred layout moving a control after it initially appears ready.

## Results so far

Both runs used Qt 6.10.2, the `minimal` platform, software rendering, and disabled QML disk cache. Timing runs used serial CTest over the nine integration groups.

| Version | Wall time | Result |
| --- | ---: | --- |
| Published PR baseline, run 1 | 229.108 s | All 122 cases passed |
| Published PR baseline, run 2 | 230.403 s | All 122 cases passed |
| Published PR baseline, run 3 | 236.177 s | All 122 cases passed |
| Tested conversion with unconditional frame waits | 285.02 s | All 122 cases and helper tests passed |

The baseline median is **230.403 seconds**. The tested conversion was approximately **24% slower**. **No speedup has been established.** There was one candidate correctness run, not three final candidate benchmark samples. Some baseline samples overlapped other build/validation activity; repeat comparable samples on an otherwise idle machine for a final performance claim.

The ASan/LSan/UBSan/integer run also passed all ten CTest entries: nine integration groups containing all 122 runtime cases, plus the helper tests. It used LLVM 22, Core's options and suppressions, and the existing app UBSan suppression. Its wall time was 230.34 seconds with three groups running concurrently, so this is not a serial performance comparison. It tests the pre-optimization working tree version described above.

## Outstanding validation

- Rebuild and validate the committed queued-boundary optimization, then repeat the serial timing comparison.
- Preserve the existing canceled-press and deferred-scroll negative controls. Exercise address-details clicks, password settings, wallet retirement, Send completion, and real watch-only review/export.
- LLVM 22 clang-tidy with the pinned Bitcoin plugin checked 22 selected translation units. It reported three existing `misc-no-recursion` diagnostics in `AppendObject`, `AppendArray`, and `AppendValue` in `rpcconsolemodel.cpp`. The same diagnostics were reproduced on the untouched PR head. This is not a full tidy pass.
- Pinned `ci/lint.py` ran against a Core overlay containing all 24 changed app files under `src/qml` and `src/qml/test`. Its remaining failure is the unchanged `std::to_string(branch)` in `test_wallet.h`. A full baseline overlay lint run was not done. The initial overlay had header-guard path errors; those were corrected before rerunning.
- The supplemental TSan build failed because its reused cache embeds different source/build paths. No TSan result is established. The pinned native TSan configuration uses instrumented libc++ and disables Qt; the old GUI cache uses libstdc++, so do not claim CI equivalence from that cache.
- Full native tidy/sanitizer CI drivers have not been run against an adapted outer app tree. The drivers exist under `bitcoin/ci`. Follow the workspace's scope requirements; checking only unchanged Core does not validate the app.
- Rerun IPC-enabled GUI startup/shutdown functional checks and inspect stderr and exit status. This standalone app uses `interface_qml_startup.py` and `interface_qml_shutdown.py`; `interface_gui.py` is absent.
- Qt 6.2 and other platforms remain untested for this conversion.

## Continue on another machine

```bash
git clone --branch experiment/pr1090-signal-waits --recurse-submodules \
  https://github.com/johnny9/gui-qml.git
cd gui-qml
```

Follow the repository's build instructions and workspace validation guidance. Build at least `bitcoinqml_integration_tests`, `bitcoinqml_signal_wait_tests`, and `bitcoin-core-app`. A normal correctness run on the tested Qt platform is:

```bash
QT_QPA_PLATFORM=minimal QT_QUICK_BACKEND=software QML_DISABLE_DISK_CACHE=1 \
  ctest --test-dir build \
    -R 'bitcoinqml_(signal_waits|integration_)' -j1 --output-on-failure
```

For timing, select only integration groups with `-L integration -j1`. Compare fresh builds of the published parent and the candidate with identical compiler, build options, Qt, environment, CPU allocation, and test selection. Run at least three samples per version. Check that each sample includes all 122 cases with zero failures or skips.

Do not replace the frame/readiness checks with timers or a continuously reposted predicate to recover speed. Identify the actual notification or event boundary needed by each condition.

## Existing workspace artifacts

On the original host, the isolated checkout is:

```text
/home/codex/workspaces/bitcoin-core-app/.validation/pr1090-signal-waits/source
```

Its parent directory retains `HANDOFF.txt`, the patch and Git bundle, build trees, XML reports, logs, and `run.py`. The helper uses cached image `qml-stage-scenarios-asan:20261108`, mounts the exact Core checkout read-only, and keeps baseline and candidate build trees separate. These artifacts and scripts are local; they are not committed to the remote branch.

From that artifact directory:

```bash
python3 run.py candidate-build
python3 run.py candidate-smoke
python3 run.py candidate-benchmark
python3 run.py asan-build
python3 run.py asan-smoke
python3 run.py tidy
python3 run.py baseline-tidy
```

**Rebuild before benchmarking:** the retained candidate executable still predates the final optimization. Do timing samples after other jobs finish.

Use `baseline-normalized.json` for the correct 122-case baseline inventory. The original raw JSON also counted stale `integration-stageN.xml` files copied from the build cache. That affected report parsing, not the CTest wall times or executed selection. `benchmark.py` now excludes those stale names.

The temporary `source/bitcoin` inspection symlink was removed after sanitizer validation. The checkout is clean and its gitlink is unchanged. All experiment containers finished. Preserve unrelated validation artifacts, worktrees, and active jobs.
