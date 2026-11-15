# R23 PSBT eligibility and revision coherence

Implementation and focused qualification pass at
82a4f0f8f9447af8c231cfad37b36b26fa525afc. Merged composition remains pending.
The R22 Save continuation, withdrawal and lifetime guards are repaired. Core remains
44901d8f64cb617714707bd025ecd83f108353c2, unchanged.

The reviewed immutable assessment, authenticated fee, verified final transaction
and displayed identity now travel together through preparation, fee refinement,
local sign-only and broadcast. Export retains the original PSBT bytes. Native
cleanup is reserved before structural capture and for raw transaction results.
GUI models project retained values and narrow commands.

Read-only public Node/Chain observations cover missing or spent chain inputs,
coinbase maturity, absolute finality, advertised minimum fee and chain limits.
These observations are advisory. Public Node RPC `testmempoolaccept` can also
provide the current consensus/policy verdict without wallet access, mempool
submission, key consumption or broadcast. The Native adapter was qualified at
7a2ee2323b31d83dae1743bcb90584dbfaf24d54 and integrated at51c40864. It checks
the exact finalized bytes and returned transaction identities, with explicit
unknown/rejected outcomes. No Core change is required for that capability. The UI states
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
After the repair, `withdrawal-08` at862ed14afcf passes78 affected bodies and
`lifetime-09` at82a4f0f8f94 passes44 Save/preparation bodies, including all21
unchanged original quiet/delete/withdrawal cases. `FINAL_FOCUSED_UNION.json`
records609 distinct passing identities across exact sources with explicit carries;
it does not claim one final-ELF full-suite execution or merged coverage profiles.

App lint03 passes at1cac3b22327. Pinned Clang23.1.6/Bitcoin-plugin static01 checks
59 handwritten affected units plus7 freshly regenerated MOC aggregates:65 pass,
one test-only unnecessary UniValue copy fails and is corrected at583d549be95;
static02 passes9/9 affected units, replacing that failure and rechecking the
changed Save guard plus7 fresh MOCs. All66 distinct static entries are qualified.
Final app lint05 passes at82a4f0f8f94 and clone/source/Core identities are rechecked.
Actual IPC GUI functional02 at82a4f0f8f94
passes native startup failure/recovery and bridge-protocol scenarios. It records
the intended failed process exit1/markerfalse and recovered CoreReady/550MiB/
markertrue/exit0, inspects GUI stderr, and has empty outer stderr in both scenarios.
`FOCUSED_QUALIFICATION.json` records exact commands, scopes, artifacts and limits.

The admission follow-up executes29 passing bodies:13 exact-public-RPC cases,
10 independent document/policy cases and6 actual Core cases, including rejected
replacement and relative locks that become admissible after a block. All11
pinned static entries and exact-head app lint pass. The source-pinned
`FINAL_ADMISSION_FOCUSED_UNION.json` records626 distinct passing identities,
replacing overlaps in the earlier609-body union. This is an explicit carry
across tested sources, not one final-ELF/full-suite result. R23's final order
acceptance still waits for the reopened R22 overwrite-retirement prerequisite.

Future optional upstream improvement: expose a typed, revisioned, read-only
finalized transaction eligibility observation to simplify the Node RPC adapter.
Submission must still validate current state. This note authorizes no Core
changes and grants no new acceptance exception. Full coverage, matrices, native
Windows and sanitizers remain pending at R42.
