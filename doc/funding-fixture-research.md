# Funding fixture research

Measured locally on 2026-11-12 with the pinned Core revision and macOS environment
in [the parallel execution report](integration-parallelization.md).
The initial probes are daemon microbenchmarks; later sections record GUI
experiments and implementation validation.

## What the sending cases require

`FundWallet()` currently mines 101 rewards to a newly generated wallet address.
At that point one 50 BTC reward is spendable. `prepareSweep()` mines two more
blocks, requires exactly three spendable coins, and locks one of them. Tests
check 50 BTC selected amounts, 100 BTC sweeps, and preservation of the locked
coin. `preparedReviewAndExportRemainStableWhenMoreFundsArrive()` subsequently
mines another block while checking that the prepared review stays fixed as
available funds increase. Preserve these transitions, including live wallet
notifications. The hardened-descriptor test also needs its existing private-key
and change-keypool behavior.

The phase profile attributed 42.445 of 79.212 seconds to `generatetoaddress`.
That is RPC wall time, not a measurement of proof-of-work computation. Wallet
processing can account for time inside that call.

## Probes

Each alternative ran three times, sequentially, using the same built Core daemon.
The cache was generated and cleanly shut down before copying. Each sample used
its own directory and wallet. No GUI ran during these probes.

| Setup | Observed time |
| --- | ---: |
| Generate 101 blocks without a loaded wallet (one seed build) | 0.026 s |
| Generate 101 rewards to a live wallet | 2.800–3.229 s |
| Copy cached blocks and chainstate (17.85 MB logical size) | 0.015–0.033 s |
| Import a deterministic private descriptor and scan the cache | 1.666–1.898 s |
| Mine 4 rewards to the wallet, then 97 to an unowned address | 0.217–0.260 s |

Normal wallet synchronization after mining added 0.027–0.035 s; the four-reward
alternative added 0.006 s. Startup, wallet creation, and copy/import costs are
recorded separately in the raw JSON. The cached wallet was blank and imported a
single funding descriptor; this is not yet an equivalent sending wallet with
normal receiving/change descriptors. Do not interpret total setup times as an
end-to-end GUI comparison.

Both cache and four-reward probes verified one spendable 50 BTC coin at height
101 and three at height 103. The four-reward probe also verified 200 BTC at
height 104. These checks establish the relevant funding shape. A separate GUI prototype
then exercised all 12 sending cases with their assertions unchanged.

## Recommendation

Prefer the four-reward approach in sending tests over adding a cache. It keeps
fresh nodes and fresh wallet keys and needs no shared directory, cache version,
copy lifecycle, or fixture-generating test. It reduces wallet-owned transaction
processing while retaining the blocks that make the first rewards spendable.
Limit the change to a sending-specific helper initially: other callers of
`FundWallet()` may rely on the larger immature balance or later rewards.

This changes the number of immature wallet rewards and the overall wallet
transaction history. The prototype preserved assertions and passed all sending cases; repeat
measurements and full-suite validation are appropriate before adopting it. Keep tests of initial funding notifications and later confirmation
live. Do not reduce coinbase maturity or disable durable database writes.

A chain cache remains possible, but block files alone are insufficient: a usable
snapshot needs chainstate, and new random wallet keys cannot spend its rewards.
Importing deterministic keys incurs scanning costs; copying a funded wallet
would avoid those costs but bypass wallet creation and funding transitions.
A cached fixture must be generated independently of test cases, copied privately,
and never mutated in place. Core's own Python functional framework demonstrates
this pattern in `_initialize_chain()`.

## Reproduction artifacts

The local experiment directory `work/signal-waits-macos` contains
`funding_cache_probe.py`, `funding_live_probe.py`, and `funding_sparse_probe.py`.
Their raw results are `results/funding-{cache,live,sparse}-probe.json`.
The scripts use fresh directory names and a fixed local RPC port; they must run
sequentially and existing probe directories must not be reused as fresh samples.
`send-profile-instrumentation.patch`, `send-profile.log`, and
`results/send-profile.xml` retain the GUI phase experiment. Profiling code is
not part of the test implementation.

## GUI prototype

The saved `funding-sparse-prototype.patch` adds a sending-only funding helper.
It mines four wallet rewards, then 97 rewards to `raw(6a)` (an unspendable
OP_RETURN output). The daemon probe used an unowned spendable address instead;
both avoid adding those 97 rewards to the test wallet, but they differ in
chainstate contents. All subsequent test operations and assertions are unchanged.

The prototype passed all 12 runtime cases (14 Qt results including setup/cleanup)
without skips in 35.366 seconds. The discovered case inventory exactly matched
the original profile. This is a single sequential group run, not a full-suite
or CI speedup measurement. The experimental helper was removed afterward and
the original fixture rebuilt; the patch and XML/log remain available.

A fresh uninstrumented control then passed the same 12 cases in **76.894 s**.
The prototype took **35.366 s**, a **54.0%** reduction in this one paired
comparison (prototype first, control second). Repetition and Linux CI measurements
are still needed. `results/funding-gui-comparison.json` records the comparison;
`results/funding-{sparse,control}-gui.xml` and matching top-level logs retain
the individual results.

## Prepared wallet plus chain snapshot

A follow-up copied a cleanly closed, normally created and funded wallet together
with its matching `blocks` and `chainstate`. This preserves all 101 original
wallet rewards rather than importing keys or reducing the reward history. Three
independent copies each loaded with 50 BTC spendable and reached three 50 BTC
spendable coins after mining two private blocks. No wallet rescan was requested.

| Cost | Three-run range |
| --- | ---: |
| Copy wallet and chain | 0.013–0.039 s |
| Start daemon | 0.182–0.263 s |
| Load wallet | 0.191–0.214 s |
| Combined | 0.396–0.515 s |

These daemon-only samples exclude the once-per-run cost of generating the seed
and GUI/model initialization. They demonstrate that a prepared wallet avoids the
1.7–1.9 s import/rescan cost of a chain-only cache. The generator and results are
`funding_prepared_probe.py` and `results/funding-prepared-probe.json` alongside
the earlier artifacts.

This makes a prepared snapshot worth testing for cases whose precondition is an
existing funded wallet: fee policy, coin selection/locking, and PSBT import,
signing, broadcasting, and replacement. Sweep tests additionally require their
three-coin arrangement; retain their two live blocks initially. The frozen-review
case requires a funded second wallet and still needs its incoming payment and
confirmation performed live. Keep hardened-keypool setup explicit initially.
Wallet lifecycle cases and receive/activity cases that inspect creation, funding,
notifications, or precise history require individual review before opting in.

Use explicit opt-in, not a global replacement for `CreateWallet` or `FundWallet`.
Build one small seed with the current binaries before dependent cases, close it
cleanly, and copy it into each child's private profile before application startup.
Copy the matching wallet and chain together, load through normal application
interfaces, and assert the fixture's expected balance/coin shape. Do not copy GUI
settings or reuse a live wallet directory. All subsequent actions remain local
to that child. A setup generator may precede cases, but no case produces another
case's state. Generating once per test invocation avoids persistent-cache
invalidation and committed database artifacts. Keep the fixture set small rather
than making a separate snapshot for every test.

The original recommendation to try sparse funding remains the smaller code
change. Prepared wallets offer better preservation of original funding history
and potentially lower repeated setup cost, at the price of a fixture lifecycle
and integration into the subprocess launcher. At that research stage, prepared-wallet GUI coverage and end-to-end timing
had not yet been measured. The implementation results follow below.


## Implemented opt-in fixture

The integration runner now generates a funded seed lazily with its own executable,
waits for clean application shutdown, and copies the matching wallet, blocks, and
chainstate into each selected child's private directory. It generates at most one
seed per runner invocation (each CTest partition has its own). No daemon executable,
persistent cache, fixture-generating test case, or dependency between CTest entries
is required. Setup time is included in elapsed suite time. A single selected case
also generates its own seed, so the saving is intended for repeated setup across
several cases, not necessarily a faster isolated case.

Ten sending rows and five PSBT rows opt in explicitly. The two-wallet incoming
payment scenario and hardened-keypool scenario retain their original setup, as
do all other modules. The original 101 wallet rewards are preserved; the sparse
four-reward prototype is not part of this implementation. Sending still mines its
two subsequent blocks live and performs the original coin-selection, locking,
review, signing, and broadcast assertions. PSBT cases retain live recipient/reviewer
wallet creation and their import/sign/broadcast/replacement assertions.

Each load checks a 50 BTC balance, height 101, one spendable coin, no locked inputs,
and an empty mempool. Copying uses separate files, with no hard links. GUI settings
and node settings are not copied. Failed seed generation fails dependent cases
and retains diagnostics; successful seeds are removed at parent exit. A regression
check verifies that discovery generates no seed and a forced generator startup
failure fails the selected case with retained logs. The shard-selection regression
also continues to pass.

An initial full three-job correctness run passed the exact baseline inventory of
122 runtime cases with no failures or skips in 83.873 seconds. The stricter height
and unlocked-input preconditions were then added before the controlled comparison.
The comparison uses the previous committed implementation (including sending
partitions) as the control, with matching build options and Core revision.


| Implementation | Sample 1 | Sample 2 | Median |
| --- | ---: | ---: | ---: |
| Previous implementation with sending partitions | 108.637 s | 108.246 s | 108.441 s |
| Prepared wallet copies | 81.020 s | 81.699 s | 81.359 s |

At three jobs, the prepared-wallet median is **24.97% lower**, saving 27.082
seconds per full-suite run on this host. Samples ran in control/prepared/prepared/
control order, restoring identical CTest scheduling hints before each sample.
Every sample passed the exact same 122-case inventory with no failures or skips.
Including the initial correctness run, that is 610 passing runtime results.
The seed-generation cost is included; no build or other test jobs overlapped
these timed samples. Normal host activity was not controlled. Linux CI and Qt 6.2
have not been run locally, so these timings apply to the documented macOS host.

`results/prepared-benchmark.json` records the samples, medians, and executable
SHA-256 fingerprints. Sample directories retain CTest logs, ten XML reports,
and complete inventories. `prepared_control_build.py` builds the control from
`8d7680e` and restores/rebuilds the implementation; `prepared_benchmark.py` drove
the comparison. An initial comparison attempt restored old source timestamps
and reused the control executable; those samples are marked `INVALID.txt` and
excluded. The corrected comparison verifies the prepared-mode argument and
records distinct executable fingerprints.
