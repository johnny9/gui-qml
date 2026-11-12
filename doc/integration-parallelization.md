# Parallel integration execution

This change starts from PR #1090's published head,
`1137c33186d24cabdd33813e94d039bedbf51b0c`. It does not include the signal-wait
experiment. That work and its macOS validation are saved separately in local
commit `04a105b` on `experiment/pr1090-signal-waits`, with a Git bundle alongside
the validation artifacts.

## Scope and isolation

Use CTest's group-level parallelism. The nine feature groups now have ten
CTest entries: sending is split into two partitions with separate reports.
Each runtime case retains its own application process,
regtest node, temporary data directory, and GUI settings. Cases within each CTest entry
remain serial. Groups have no ordering dependencies, and no test consumes
another test's output. Assertions, test selection, and timeouts are unchanged. Selected sending and
PSBT cases now use private copies of a prepared funded wallet; see the funding
fixture research report for scope and validation.

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

## Higher-concurrency follow-up

A second comparison tested three, four, and six jobs on the same eight-CPU,
12 GiB host and unchanged binary. Each setting ran three times. Orders rotated
as `(3, 4, 6)`, `(4, 6, 3)`, and `(6, 3, 4)`. Before every sample, the same saved
CTest timing hints were restored, avoiding scheduling estimates learned under
one job count affecting the next. Original timing history was restored afterward.

| Jobs | Sample 1 | Sample 2 | Sample 3 | Median |
| --- | ---: | ---: | ---: | ---: |
| 3 | 111.018 s | 105.925 s | 106.118 s | **106.118 s** |
| 4 | 103.851 s | 107.790 s | 105.104 s | **105.104 s** |
| 6 | 104.895 s | 109.442 s | 114.758 s | **109.442 s** |

All nine runs passed identical inventories of 122 runtime cases, with zero
failures or skips: 1,098 passing runtime results. Four jobs reduced the median
by only 1.014 seconds (0.96%) relative to the fresh three-job control. Six jobs
increased it by 3.323 seconds (3.13%). The observed ranges overlap; these samples
do not establish a useful improvement from increasing the cap. Sending finished
last in every four- and six-job sample, while other groups experienced contention.

Keep the CI cap at three. The earlier three-job median and this fresh control
differ, reinforcing the need for contemporary controls instead of comparing
higher job counts only against older timings. These results do not establish
the best setting on other hardware or Linux runners.

`parallel_higher.py` reproduces this comparison. `results/higher.json` contains
the aggregate results, `results/higher-environment.json` records CPU and memory,
and each `results/higher-j{3,4,6}-{1,2,3}/` directory retains its CTest log,
nine XML reports, and case summary. The follow-up changes no CI or test code.

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

## Sending partitions and funding research

A temporary exclusive-time profile of all 12 sending cases attributed 42.445
of 79.212 seconds to mining RPCs. Alternating discovered rows partitions this
profile into 38.494 and 40.682 seconds, six cases each. The runner now supports
`--shard=INDEX/COUNT`; CTest uses `0/2` and `1/2` for sending only. Filtering
happens before partitioning, and discovery failures remain visible. Each child
still starts a fresh application and node. No shared runtime fixtures are added.

The selection regression test checks exhaustive, disjoint partitions including
data rows and rejects invalid arguments. A full three-job run passed the exact
baseline inventory of 122 runtime cases without skips in 119.919 seconds.
Existing timing history had no entries for the new partitions, so they started
late. This is a correctness result, not a demonstrated speedup; repeated
contemporary scheduling comparisons are still needed.

See [funding fixture research](funding-fixture-research.md) for the subsequent
comparison of chain caching and reducing wallet-owned coinbase rewards.


## Prepared-wallet implementation

Selected sending and PSBT cases now use independent copies of a funded wallet
and its matching chain, generated once per runner invocation. The controlled
three-job comparison against the preceding partitioned implementation reduced
the median from **108.441 s to 81.359 s (24.97%)**. All four measured runs passed
the same 122 cases. Setup-specific cases retain their existing fixtures. See the
[funding fixture report](funding-fixture-research.md#implemented-opt-in-fixture)
for scope, measurement details, and artifacts.
