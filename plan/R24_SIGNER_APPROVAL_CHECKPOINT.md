# R24 signer approval checkpoint

The immutable approval owner is integrated at
`0224c4c0f2fa919990a21f723da589a6bef87803`, with unchanged Core
`44901d8f64cb617714707bd025ecd83f108353c2`. Exact composed test source is
`e3373af66b2aa05430bdf162e36ce96994dd6c4d`; the worker candidate is
`c8e3d5fff4ad64c549e716b6ed4abb6eae499dee`. R22 is now restored. Final R24
acceptance awaits the bounded contributed-partial-signature audit below.
This checkpoint does not close R42.

`walletsigning::ApprovalOwner` owns Domain transitions and the original opening,
review, view and immutable request. Native signing authenticates reviewed input
keys and verifies returned signatures and exact transaction terms. Partial data
is retained without complete-signing authority. Obsolete returns drain without
installing into a successor review. GUI code projects retained snapshots and
issues narrow approval commands. An ownership check after reentrant Domain
admission protects facade deletion; its existing manual schedule now includes
`accepted-Domain-delete`.

The Activity list disables Qt delegate reuse after actual process diagnostics
showed a filtered row retaining the wrong transaction identity. Existing role
bindings remain intact. This is an app workaround; no Qt component was replaced.

Artifacts: `.validation/qt6-r11-r40-20261114/R24/integration/`.
`FOCUSED_INTEGRATION.json` and its branch copy in `evidence/` record exact
commands, revisions, raw paths and limits.

| Check | Actual executed result |
| --- | --- |
| `focused-03`, WalletSend | 111/111 bodies pass |
| Same gate, wallet facade signer schedules | 15/15 pass |
| Same gate, Activity QML consumer | 58/58 pass |
| Same gate, watch-only/signer Core integration | 12/12 pass |
| Pinned Clang23/Bitcoin tidy, `final-01` | 4 changed C++ plus7 generated MOC entries pass |
| App lint and full app QML lint | Exact composed source passes |
| Actual GUI/Core signer substitution | Send and Speed Up pass; independently valid wrong responses rejected; exact retries accepted |
| Actual normal/fatal process lifecycle | Normal bridge exits0; real Core block-write failure exits1 |

All196 bodies have zero failures/skips; the four actual process cases inspect
GUI stderr and exit status. Both outer stderr files are empty. Earlier worker
failures and the first composed manifest's107-vs111 count mismatch remain
archived; changing the count did not alter any behavior oracle.

Scope map:089/122 precise error/terminal handling;154 retained valid partial
signatures;161/286 original action/session/review lifetime;249 guarded phases and
single commit;255 cryptographic compatibility of reviewed keys;cross088 rejects
synthetic unauthorized signing results. Existing reviewed rate and absolute
fee limits remain enforced.

The R09 public interface cannot cancel/deadline an in-flight HWI call or release
Core's wallet lock across it. Background execution and stale-result fencing do
not claim those stronger guarantees. Physical devices were not tested. These
inherited limits, exhaustive coverage, all configurations/platforms and
sanitizers remain explicit pending obligations under the approved cadence.

Source review found that `VerifyFinalizedInputs(false)` skips unfinalized
`partial_sigs` and Taproot signature maps. The existing tests cover valid
partial outcomes and invalid finalized scripts, but do not establish that
every newly contributed unfinalized signature is authenticated. The signing
worker is checking an independent counterexample and pinned public Core/value
helpers. Keep this as an open acceptance audit, preserve supported multisig
PSBT retention, and do not infer a Core API requirement before that check.
