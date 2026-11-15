# R23 PSBT eligibility and revision coherence

Implementation is in the isolated R23 candidate. Acceptance remains pending the
R22 reentrant Save admission repair and its affected checks. Core remains
44901d8f64cb617714707bd025ecd83f108353c2, unchanged.

The reviewed immutable assessment, authenticated fee, verified final transaction
and displayed identity now travel together through preparation, fee refinement,
local sign-only and broadcast. Export retains the original PSBT bytes. Native
cleanup is reserved before structural capture and for raw transaction results.
GUI models project retained values and narrow commands.

Read-only public Node/Chain observations cover missing or spent chain inputs,
coinbase maturity, absolute finality, advertised minimum fee and chain limits.
These observations are advisory: the public interfaces cannot supply an atomic
mempool admission/conflict/replacement/relative-lock snapshot. The UI states
that submission checks current policy; the actual Node broadcast result owns
acceptance or rejection. An explicit Recheck refreshes the original review.
Rejected or successful attempts cannot silently rearm through a late fee echo.

| Scopes | Evidence and disposition |
| --- | --- |
| 096,097,105 | Actual Core controls distinguish confirmed independent spend, nonfinal locktime, below-minimum fee and mempool replacement rejection. Read-only observations do not promise mempool acceptance. |
| 108,253 | Native success clears the original source; rejection requires Recheck and preserves its actual outcome. The old assertion that success always leaves the source unchanged was contradicted by the cited assignment and is not repeated. |
| 109,254 | Manual original-session/device/action schedules and an actual Core unload/reload control retain opening identity and prevent review transfer. No demonstrated wrong-wallet send is claimed; controller selection already masked that allegation. |
| 110,112 | The same assessed revision supplies permissions and checked final extraction; raw results retain Native cleanup. Parent lifetime and repeated-metadata failure schedules reproduce defects. Actual valid pinned-Core `sign=false` byte divergence for112 was not demonstrated, so that narrower allegation remains unproven. |
| 304 | PR1056 was already merged. Preserve and test both intentional absolute and rate limits at the existing broadcast API; no fee-policy redesign. |

Artifacts are under `.validation/qt6-r11-r40-20261114/R23/`. Parent01 executes
9 intended failures; parent02 executes the independent raw-result lifetime
failure. `combined-06` at1cac3b22327 executes608 bodies:606 pass and2 original
R22 Save withdrawal rows fail, with no skipped or incomplete selections.
`opening-07` at ead12cbea44 adds1 passing actual Core unload/reopen control.
The R22 worker owns the remaining repair; its original oracle stays unchanged.

App lint03 passes at1cac3b22327. Pinned Clang23.1.6/Bitcoin-plugin static01 checks
59 handwritten affected units plus7 freshly regenerated MOC aggregates:65 pass,
one test-only unnecessary UniValue copy fails and is corrected at583d549be95;
the affected tidy rerun remains pending. Actual IPC GUI functional01 at583d549be95
passes native startup failure/recovery and bridge-protocol scenarios. It records
the intended failed process exit1/markerfalse and recovered CoreReady/550MiB/
markertrue/exit0, inspects GUI stderr, and has empty outer stderr in both scenarios.

Future upstream improvement: expose a typed, revisioned, read-only finalized
transaction eligibility observation, including mempool spender/replacement and
relative-lock facts, without mutation or key consumption. Submission must still
validate current state. This note authorizes no Core changes and grants no new
acceptance exception. Full coverage, matrices, native Windows and sanitizers
remain pending at R42.
