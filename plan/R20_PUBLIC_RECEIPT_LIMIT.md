# R20 public submission receipt limitation

Decision pending. Core remains `44901d8f64cb617714707bd025ecd83f108353c2`.
No Core/Qt changes, wallet RPC escape or forced rebroadcast are authorized.

The pinned interfaces::Wallet::commitTransaction returns void. Public
getTx/getWalletTxDetails reads can establish the exact transaction's observed
wallet presence and current local mempool/confirmation/conflict status. They
cannot recover whether a missing mempool entry resulted from disabled wallet
broadcast, a rejection, another intervening event, or prove peer receipt.
An absent entry is not a rejection receipt. Core's internal log text is not a
supported application outcome API.

The implementable application ledger keeps independent facts: exact original
wallet/candidate identity; recorded, observed absent or unknown wallet state;
local mempool, confirmed, conflicted, abandoned, not-in-mempool or unknown status;
and relay unconfirmed. Accepted work and its actual result survive lost GUI
publication. Unknown outcomes require reconciliation, never automatic resubmit.
Exclusive confirmation, full-wallet consent, one mutation and preservation of a
successor draft remain mandatory. No unsupported reason is inferred from absence.

R20 scopes draft-081-RC11 and draft-307-RC11 explicitly ask for disabled versus
rejected outcomes. Their reason distinction cannot be accepted as implemented
through this interface. The user must choose a conditional focused app checkpoint
with that distinction explicitly unresolved, or keep R20 acceptance on hold.
The ordinary truthful-ledger implementation and independent stages can continue.
This is separate from R17's already accepted fee-preview limitation and R19's
source-refuted preparation-time cluster-policy allegation.

For upstream, return a typed immutable public commit receipt identifying wallet
recording, policy-suppressed submission, local acceptance or rejection with its
reason, and whether propagation was requested/queued. A propagation request must
not be described as proof that peers received the transaction. This would expose
the result already known at the Core effect boundary and remove later inference.
No Core change is made as part of this program.
