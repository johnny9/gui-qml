# Funding fixture research

Measured locally on 2026-11-12 with the pinned Core revision and macOS environment
in [the parallel execution report](integration-parallelization.md).
These are daemon microbenchmarks, not GUI-suite speedup measurements.

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
