# Signer transaction commitment

The signer response is untrusted. Capture the reviewed unsigned transaction
before calling the signer. Check version, locktime, ordered input outpoints and
sequences, and ordered output scripts and amounts before installing any complete
or partial response. ScriptSig and witness signing data may change. PSBT container
versions and non-transaction metadata do not define transaction identity.

`MatchesUnsignedTransaction` is the value-only comparison used by explicit signer
approval, signing during Send, and Speed Up. A missing unsigned transaction is
also rejected. Speed Up signs an isolated copy of its approved replacement and
checks that copy before commit, preserving the input conflict chosen by Core.
A rejected response never changes the reviewed transaction or marks it replaced.

On approval rejection, retain our own reviewed PSBT. Ordinary Send drafts also
retain that document so a later explicit Send must sign and validate again.
The legacy `currentTransactionCanSend` capability permits a signing attempt; it
does not itself mean device approval succeeded. Preserve that capability for a
safe retry, while the existing failure signal keeps the action out of its signed
presentation state. No rejected signer-returned document is installed.

Valid partial signatures remain exportable and can be completed later. Exact
matching complete responses retain their existing Send and replacement behavior.
Tests cover altered terms in ordinary and imported Send, direct signing during
Send, replacement signing, real partial signatures, and actual signer subprocesses.
The process cases independently check mempool validity, rejected substitution,
matching retry, original conflict state and replacement confirmation.

This is the R02 guard in the current signing path. Full signer execution/lifetime
migration to background effects remains R24; existing synchronous signer/Core
calls are not represented as migrated here. R03 owns authenticated fee metadata.
Neither broader signature validation nor the complete M05 workflow is claimed
by the unsigned-terms comparator alone.
