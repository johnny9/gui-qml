# Experiments after prepared-wallet fixtures

These experiments start from `710ed3e` and use the same macOS host, pinned Core
revision, Qt version, Release build, and headless settings documented in
[integration parallelization](integration-parallelization.md). The changes tested
here were initially experiments. The four-job cap and reduced watch-only
funding have since been adopted after the coverage review below.

## Four concurrent jobs

Three samples per setting ran in orders `(3, 4)`, `(4, 3)`, and `(3, 4)`.
Every sample used identical saved CTest scheduling hints. No build or other test
job overlapped a timed sample, and the timing history was restored afterward.

| Jobs | Sample 1 | Sample 2 | Sample 3 | Median |
| --- | ---: | ---: | ---: | ---: |
| 3 | 80.177 s | 79.695 s | 80.070 s | 80.070 s |
| 4 | 71.694 s | 66.582 s | 71.714 s | 71.694 s |

Four jobs reduced the median by **8.376 seconds (10.46%)**. All six runs passed
identical inventories of 122 runtime cases with no failures or skips: 732 passing
runtime results. The ranges do not overlap in these samples. Some individual
groups slowed under contention, but running another group concurrently reduced
full-suite elapsed time. This supersedes the earlier result that four jobs did
not help before prepared-wallet setup shortened the sending group.

Four jobs is now a useful setting on this host. A CI cap of `min(nproc, 4)` is a
reasonable next change, with Linux CI validation still needed. More splitting at
three jobs has limited potential because that setting already keeps the three
slots occupied for nearly the entire run.

`prepared_concurrency.py`, `results/prepared-concurrency.json`, and the
`results/prepared-concurrency-j{3,4}-{1,2,3}/` directories retain the driver,
executable fingerprint, summaries, logs, reports, and inventories under the local
`work/signal-waits-macos` experiment directory.


## Reduced funding in the watch-only case

The prototype changes only the setup of
`createFromXpubAndPrepareUnsignedTransaction()`. It mines one reward to the new
watch-only wallet and 100 rewards to an unspendable `raw(6a)` output. Height 101
still provides one mature 50 BTC input. Explicit balance and spendable-coin checks
replace the general funding helper's positive-balance check.

Source wallet creation, extraction of its xpub, watch-only creation through the
UI, live funding notifications, fee configuration, unsigned transaction review,
PSBT export, absence of signatures, and refusal to sign/broadcast are retained.
The prototype reduces immature rewards and wallet transaction history; these are
not assertions of this case. The invalid-xpub case is unchanged. No snapshots or
shared state are introduced here, and the general `FundWallet()` helper is unchanged.

Three samples per implementation ran the complete two-case signer group,
alternating orders `(control, prototype)`, `(prototype, control)`, and
`(control, prototype)`. Separate executables with recorded SHA-256 fingerprints
avoid changing the binary during a comparison. Every sample passed both cases
with the same discovered inventory and no failures or skips.

| Implementation | Sample 1 | Sample 2 | Sample 3 | Median |
| --- | ---: | ---: | ---: | ---: |
| Current funding | 10.310 s | 10.765 s | 10.493 s | 10.493 s |
| One wallet reward | 4.403 s | 4.006 s | 3.965 s | 4.006 s |

The signer-group median decreased by **6.487 seconds (61.82%)**. These are isolated
group measurements, not an estimate of an equally large full-suite gain: its
runtime overlaps other groups during parallel execution.

The implementation is retained in `watchonly-funding-prototype.patch` in the local
experiment directory. `watchonly_funding_experiment.py` and
`results/watchonly-funding.json` retain the driver and measurements; each
`results/watchonly-funding-{control,prototype}-{1,2,3}/` directory has the XML
report and process log.


A complete-suite prototype run at four jobs passed the exact 122-case baseline
inventory without skips in **69.439 seconds**, including prepared-wallet seed
creation. This falls within the earlier four-job range, so this single validation
run does not establish an additional full-suite speedup from the watch-only change.
The repeatable benefit is lower signer-group setup time. Its raw results are in
`results/watchonly-prototype-full-j4/`.

After validation, the original watch-only source was restored and rebuilt.
The committed CI cap and test fixtures are unchanged by these experiments.
Both changes are good candidates to adopt: raise the cap to four (bounded by
CPU count), and reduce the unused watch-only rewards. Linux timing remains to be
verified; no claims are made for other runners or higher job counts.


## Adoption and coverage review

Both changes are adopted. CI now uses `min(nproc, 4)` without changing test
selection. The adopted watch-only change keeps two wallet rewards: one mature and one immature
at height 101. The remaining 99 blocks pay an unspendable output. This preserves
the original presence of immature funds while removing repeated setup transactions,
not any
case or behavior assertion. Its original watch-only creation, key restrictions,
review click/focus behavior, PSBT export and unsigned-input checks, empty mempool,
and refusal to sign/broadcast remain intact. It now additionally checks the exact
50 BTC available balance, 50 BTC immature balance, and one spendable coin. The original case did not
assert the size of the immature balance; retaining one immature reward also
preserves that original setup condition. The earlier timing experiment used
one wallet reward; the adopted two-reward variant is validated separately. Mining and maturity remain live;
no case shares writable state. All 122 runtime cases were preserved and passed
in the full prototype validation; the isolated signer comparison passed both
cases in all six samples.


The final two-reward variant passed all original 122 runtime cases at four jobs
with no failures or skips in **68.017 seconds**. The signer group took 4.79 seconds
within that concurrent run; this is a validation result, not a new isolated
benchmark. Reports and the exact case inventory are in
`results/adopted-coverage-final/`. The shard-selection and prepared-wallet
setup-failure regressions passed. The workflow YAML and integration shell block
were parsed, and the CPU cap was checked at 1, 2, 3, 4, and 8 reported CPUs.
