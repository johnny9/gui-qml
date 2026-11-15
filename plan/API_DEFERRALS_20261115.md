# User-approved R20 and R30 API deferrals

The user explicitly instructed: “For r30 and r20 mark those pieces as needs
upstream improvements and move on. Just make sure we have notes about this
decision.” This supersedes the earlier pending decisions. Core stays at
44901d8f64cb617714707bd025ecd83f108353c2; no Core or Qt changes are authorized.
Only these missing API guarantees are deferred. Other implementation and focused
acceptance gates remain required; final qualification remains R42.

## R20: submission receipt

Mark the exact disabled-broadcast-versus-rejection distinction as **needs
upstream improvement**. Wallet::commitTransaction returns void; later public
reads can establish exact wallet recording and observed local status, but an
absent mempool entry cannot identify the original reason or prove peer receipt.
The app retains a truthful ledger of recorded/local/unknown facts for the
original candidate. It preserves Core policy, never forces an additional
broadcast and never automatically resubmits an uncertain outcome.

Upstream should return an immutable typed commit receipt for recording,
policy-suppressed submission, local acceptance or rejection/reason, and any
propagation request. A queued propagation request is not proof of peer receipt.
Scopes081/307RC11 retain this explicit API-dependent remainder.

## R30: receive-type capability inventory

Mark exact initial per-OutputType filtering for arbitrary imported descriptor
wallets as **needs upstream improvement** (scope007). Aggregate canGetAddresses,
Taproot-manager presence and configured default type do not enumerate exact
support. Generating destinations for inventory would consume keys/reservations.

The app exposes proven/Unknown support separately from temporary unlock/keypool
availability. An explicit generation attempt owns its result; failure clears
any stale reservation so old address/QR/request data cannot be reused as the
new result. No private wallet access, wallet RPC or mutating capability probe.

Upstream should expose a nonmutating typed per-type capability snapshot,
separate current availability/reason, and a coherent revision/change signal for
descriptor/keypool changes. These facts must not reserve keys or write metadata.

The branch-local decision artifact is workspace
.validation/qt6-r11-r40-20261114/USER_API_DEFERRALS_20261115.json.
Canonical318finding/329scope registers remain historical and unchanged.
