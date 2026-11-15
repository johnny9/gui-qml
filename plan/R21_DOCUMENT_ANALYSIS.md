# R21 document analysis: implementation and evidence in progress

Core stays at `44901d8f64cb617714707bd025ecd83f108353c2`. No subtree or Qt
changes, private wallet access, wallet RPC escape, persisted-format change or
new executor framework is authorized by this step.

`psbtdocument::Owner` owns one document generation and its coherent immutable
analysis on the existing Domain delivery lane. Native file/Wallet/Node effects
produce a result before that owner consumes it. QML receives cached properties
and bounded summary/flow windows. The facade can withdraw interest; it cannot
adopt a retired generation or run analysis synchronously. `RetainValues` reserves
background destruction before any structural result crosses into presentation.
Closing an alive facade releases its owner and structural aliases without
requiring a queued GUI notification to run. Accepted work retains its original
owner until the physical effect and Domain outcome finish.

The shared value types live in `psbtworktypes.h`; the Domain owner does not
include the Native effect API. App lint caught the initial mutual include;
separating the values removes it without changing the cycle allowlist.

The imported document and the metadata/finalization assessment are separate
immutable values. Copy/save preserve the original bytes. Inspection may use a
captured assessment to verify signatures and explain eligibility. It must not
replace the original document or combine its transaction with another version's
fee proof. The existing 100 MiB file limit and bounded single read remain.

## Imported outputs

Ownership is not recipient intent. The public interface supplies ownership but
does not establish whether an arbitrary imported output was intended as change.
Every imported output is reviewed, including self-transfer outputs. Flow rows
carry `changeKnown=false`, and QML labels the list **Outputs**, explains that
wallet-owned outputs may include change, and labels the aggregate **Outputs plus
fee**. Signing capability never changes the visible output set. Wallet-prepared
reviews keep their explicit recipient/change classification.

This avoids guessing from private-key availability, key scheme, address-book
presence, or derivation metadata. A future supported classification capability
could improve this display; it must still distinguish observed wallet ownership
from the creator's intent. That enhancement is not needed to show and bind the
exact outputs safely.

## Signature and device boundary

Core's finalized-field flag alone is insufficient evidence of valid signatures.
The main Send guard was qualified at `b31e8412645d20a4c7c746330abeb1b0983650a5`.
The R21 audit also found this weakness in standalone inspection/sign/broadcast.
The current candidate uses Core's `PSBTInputSignedAndVerified` against authenticated
prevouts before reporting completion, retaining finalized signer results or
broadcasting. Valid partial results remain possible; an invalid finalized input
does not become signing authority. A different physical device is acceptable
only when its result satisfies the original keys and immutable reviewed terms.

`SignatureState` distinguishes Unverified, Incomplete, Invalid and Verified.
The compatibility `complete` fact can still describe finalized fields when
input evidence is unavailable. `signaturesVerified` and action permissions
derive from the explicit state; unverified evidence cannot grant broadcast.
A localized empty metadata error cannot grant authority or be mislabeled as
a missing fee when the fee is actually known.

The standalone guard is implemented but remains unqualified. Full R24 held-device,
timeout, stale-publication and partial-result obligations remain open; no physical
hardware pass is claimed.

## Scope accounting

Canonical scope registers remain unchanged. These are current implementation
and test obligations, not acceptance claims.

| Assigned scope | Current treatment |
| --- | --- |
| 029 | Existing typed `feeKnown`/optional fee and `Unknown` presentation distinguish unknown from zero. Focused Domain tests cover known-positive, known-zero, missing and invalid amounts; the Core authenticated legacy-fee control and forged-fee rows passed in the final focused run. |
| 039 | Parent test reproduces lookup under the unsigned legacy txid. Lookup now uses the extracted finalized identity when available, without mutating the imported document. The independent Core legacy self-transfer/known-record/UI scenario passed in the final focused run. |
| 093 | Retain checked `psbtfee::Analyze` accumulation and authenticated prevouts. Review accumulation checks `value <= MAX_MONEY - total` before adding. Shape, fee and amount controls passed in the final focused unit/Core selections. |
| 095 | Held claim, reconciled for the current command boundary. Four controls passed on the unguarded parent: actual signature bytes/Core verification show which outputs ALL/NONE/SINGLE commit, and immutable review prevents changed-output submission even where the signature remains valid. The historical unconditional P1 remains unestablished; no repaired defect is claimed for this held row. |
| 102, 106 | Parent reproduces lost owned outputs for signable and complete watch-only self-transfers, with two passing controls. All imported outputs are now shown, with change intent explicitly unknown. Final focused unit/Core/UI cases passed. |
| 104, 252 | The Domain owner commits/clears one analysis tuple and rejects older/withdrawn structural outcomes while retaining actual scalar receipts. Existing Wallet import projection fences remain; failed-import draft preservation, coherent retry and the post-decode tuple-preservation rows passed in the final focused run. |
| 250, 280 | Original document and captured assessment/proof remain separate immutable values. Read/copy/save preserve original bytes; metadata refresh is explicit. Export-parity, stale-read and held-publication controls passed in the final focused unit/Core selections. |
| 266 | Preserve canonical satoshi values and checked accumulation. Cross-consumer amount parsing remains covered by its original send-model owner; do not confuse it with the separate rate-parser scope265. |

Cross-consumer scopes016/021/229 retain R14's original credential/action lifetime;
087 retains R03 scheduling contracts. Scope107's global import route belongs to
R26 and is not accepted by a standalone helper/model pass.

## Recorded checkpoints

Artifacts are under `.validation/qt6-r11-r40-20261114/R21/` in the workspace:

- Parent01, test-only `9db4793199c` on production `0449765cf907`: five bodies,
  three intended failures and two passing controls. No skips.
- Source01 `8776cc9f9b2`: 50 focused bodies passed, including three alive-facade
  drain schedules, original held-publication cases and a physical worker check.
  This qualifies that checkpoint only, not subsequent changes or the whole step.
- Source02 `727f1e3b819` and parent03 `5601bb61d72`: new test-fixture compilation
  errors stopped the build before test execution. Both raw failures are retained;
  neither is an application reproduction or a test pass.
- Parent04 `87ae6760fac`: ten bodies executed: seven controls pass and three
  incompatible-key Inspect/Sign/Broadcast cases fail as intended. The compatible
  cases use actual Core signatures. Four signature-commitment controls pass on
  this parent. No skips; this reproduces the remaining standalone guard gap.
- Source03 `b080e44e64f`: the build rejected the new helper's `struct` forward
  declaration because the pinned Core type is a `class`. Zero bodies executed;
  the declaration is corrected in the next candidate. No application failure
  or passing test is attributed to this compile error.
- Source04 `cf1c99c6393`: 261 bodies executed, 257 passed and four older
  finalization/translation expectations failed. All eleven actual Core/UI
  bodies and all six compatible/incompatible-key cases passed. QML lint passed
  247 files in each compatibility variant. The next candidate separates
  verified signatures from finalized fields and preserves the typed metadata
  error guard. App lint also found the new include cycle, fixed as above.

## Final focused checkpoint

On the combined R21 candidate `61002115b2bf0df2392b6359648d40ac5f8e7d6f`
(app parent `7b1ab0eb2fd1492a3a6553090512b9cafeab4bb4`, Core
`44901d8f64cb617714707bd025ecd83f108353c2`), the focused R21 manifest passed
347/347 bodies. This includes all 14 selected PSBT Core/RPC bodies: the prior
11 rows and three decoded-review failure rows. An additional 45/45 R29
composition bodies passed, bringing the distinct combined focused count to
392; repeated runs are not added to that count. The R21 model class was then
requalified at 78/78 with the same-generation refresh/page race row included.

The bounded summary source retains the immutable assessment/proof and one
ownership bit per output, not a fully formatted output list. Initial and
requested formatted pages are capped at 128 rows, with a separate total count.
Manual tests cover superseded Native work, a held GUI publication, replacement
by a same-generation refresh, failed admission/retry and cleanup while the
facade remains alive. A 4,096-output document verifies bounded page payloads
and full count/content parity. The 100 MiB PSBT file cap and existing single
bounded read remain unchanged; no new cardinality cap was added.

Exact-head app lint passed with the pinned Core 449 linter. Exact-head QML lint
passed in both recorded compatibility variants (Qt 6.2 and 6.5). The pinned
Clang23/Bitcoin-plugin tidy selection, including fresh MOCs for the changed
C++ files, is the remaining focused static gate. Exhaustive coverage,
sanitizers and the full configuration/platform matrix remain deferred to R42
under the user's validation policy.
