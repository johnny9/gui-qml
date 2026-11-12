# Parallel integration execution

This change starts from PR #1090's published head,
`1137c33186d24cabdd33813e94d039bedbf51b0c`. It does not include the signal-wait
experiment. That work and its macOS validation are saved separately in local
commit `04a105b` on `experiment/pr1090-signal-waits`, with a Git bundle alongside
the validation artifacts.

## Scope and isolation

Use CTest's existing group-level parallelism. Each of the nine groups has a
separate report, and each runtime case retains its own application process,
regtest node, temporary data directory, and GUI settings. Cases within a group
remain serial. Groups have no ordering dependencies, and no test consumes
another test's output. Assertions, test selection, timeouts, fixtures, and
failure diagnostics are unchanged.

The source-build CI workflow runs non-integration tests with its existing serial
scheduling, then integration tests with up to three jobs, capped at the CPU
count reported by `nproc`. The integration step runs even if non-integration
tests fail, provided the build succeeded and the job was not cancelled. Reports
remain separate and are uploaded on failure. The depends-build workflow already
uses parallel CTest execution and is unchanged.

For a local run:

```sh
ctest --test-dir build -L '^integration$' --parallel 3 --output-on-failure
```

Use two jobs on a memory-constrained machine, or one job for serial comparison.
The CPU cap is a concurrency bound, not a memory guarantee.

## Results

All eight runs passed the same 122 runtime cases with no failures or skips:
three samples each at two and three jobs, one fresh serial control, and one
three-job run without scheduling history. That is 976 passing runtime results.

| Jobs | Sample 1 | Sample 2 | Sample 3 | Median |
| --- | ---: | ---: | ---: | ---: |
| 2 | 136.329 s | 145.941 s | 142.526 s | **142.526 s** |
| 3 | 110.326 s | 124.254 s | 120.597 s | **120.597 s** |

The fresh serial control took **254.219 seconds**. Against that control, two
jobs reduced elapsed time by **43.94%**, and three jobs reduced it by **52.56%**
(2.11 times the throughput for this complete suite). The earlier three serial
samples had a 249.926-second median, consistent with the fresh control.

The three-job run without scheduling history took **132.055 seconds** and
passed all cases. CTest started foundation, node setup, and node tools first;
the repeated measurements started the historically expensive sending group
immediately. No fixed duration estimates or custom scheduling are introduced.

Three jobs is a bounded starting point for CI, not a universal optimum. Several
individual groups become slower under contention even though the suite finishes
sooner. These measurements establish a local wall-time improvement, not lower
total CPU usage or a guarantee for every CI runner.

## Measurement conditions

Measured on 2026-11-12: macOS 26.6.2 arm64, eight logical CPUs, Qt 6.11.2,
Apple Clang 21.0.0.21000334, Ninja Release. The same existing baseline binary
was used for every job count. Core is pinned to
`dc282ff31d1cc97507530a541d9cec8a8f6a6ef4`; IPC and test automation are enabled.
Both differ from the source-build Linux CI environment, so Linux timing and
Qt 6.2 compatibility of concurrent execution remain to be established by CI.

The platform is `minimal`, the Quick backend is `software`, and QML disk caching
is disabled. Tests run outside the execution sandbox because its restrictions
prevent filesystem notifications used by the live-log tests. The build uses
`HAVE_DECL_PIPE2=0` to avoid the installed SDK exposing a macOS 27-only function
on this macOS 26 host. No source workaround is included in this change.

Two-job and three-job samples alternate, with no overlapping build or other
validation jobs. Normal host activity is not controlled. The repeated samples
use CTest's existing timing history. A separate run removes that history to
check fresh-checkout scheduling; the history file is restored afterward.

## Artifacts

Scripts and raw logs are retained under:

```text
/Users/johnny/workspaces/bitcoin-core-app-macos/work/signal-waits-macos
```

`parallel.py` runs three samples at each job count followed by a fresh serial
control. `parallel_cold.py` checks scheduling without prior timing data.
`validate.py` requires all 122 runtime cases to pass without skips. Both drivers
compare the complete case inventory against the saved baseline. Each sample's
directory in `results/` contains the CTest log, nine JUnit XML reports, and a
per-case JSON summary.

The workflow YAML was parsed, and the CTest label selections were checked to be
disjoint and exhaustive. The Linux workflow itself has not been run locally.
