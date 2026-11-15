# R18 public wallet interface and Core notes

Core `44901d8f64cb617714707bd025ecd83f108353c2` remains unchanged.

A real Core test reproduces temporary-to-persistent lock upgrade followed by
unlock/reload restoring the lock. CWallet::LoadLockedCoin uses emplace; upgrading
an existing temporary lock can write its database row without updating the
in-memory persistence flag. Unlock then omits database erasure. Raw failure is
retained under `R18/followup/core-remaining-01/` in the execution artifacts.

The application uses only interfaces::Wallet: it refuses a persistence request
for an already locked input, returns a typed AlreadyLocked item outcome, retains
its mark and tells the user to unlock explicitly first. It does not silently
unlock/relock another actor's lock. The real Core guard test verifies zero
changed items and no returned lock after explicit unlock/reload. This avoids the
observed sequential GUI operation but does not repair Core's mechanism.

For upstream, repair the in-memory persistence update and database cleanup
contract, and expose lock persistence/status and an atomic conditional lock
operation with a typed result. Separate public isLockedCoin/getCoins and mutation
calls cannot exclude an arbitrary concurrent RPC update between them. Existing
app action serialization is not a global Core compare-and-swap guarantee.
Catalog refresh observes current locks; the app does not claim a missing public
lock-change notification or globally atomic selected-input preparation.

A required guarantee that cannot be supplied through the public wallet interface
must be documented and brought to the user before acceptance. No wallet/node
subtree changes, private wallet access or Qt replacement are authorized.
