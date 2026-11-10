# PSBT fee authentication

A reviewed fee is the checked sum of authenticated input amounts minus checked
output amounts. An unknown fee is distinct from a known zero fee. Invalid money
ranges, overflowing totals, negative fees, duplicate outpoints, incompatible
locktimes, mismatched parent transaction hashes, out-of-range indexes and
contradictory UTXO representations cannot enable signing, sending or broadcast.

`psbtfee::Analyze` accepts a parent transaction only when its transaction ID and
output index match the input. Witness UTXO metadata, scripts and signatures do
not independently prove an amount. Missing parent transactions can be completed
from an independently obtained, immutable node UTXO snapshot. Metadata that
contradicts that snapshot is rejected. Core transaction construction supplies a
separate aggregate fee proof bound to the exact unsigned transaction; signer
metadata cannot replace that proof or change its approved fee.

The wallet composes a fee-only reader from its existing background scheduler,
node lookup and queued GUI publication port. The confirmed coin lookup falls
back to the public gettxout RPC with mempool inclusion for unconfirmed parents;
all RPC execution and decoding remain on the worker. The reader owns an immutable PSBT
snapshot. It performs node lookups and completion verification on the worker.
The GUI review remains unavailable for protected actions while the result is
pending. Admission state is complete before its notification. A ready result
replaces the review with one frozen value projection; it does not mutate the
published fee amount halfway through observer callbacks. Reviewed recipient,
change and label data survive edits to the underlying send form. Request identities reject publications after replacement, closure,
shutdown or observer loss. Domain task accounting completes independently of
GUI publication. Missing data, exceptions and rejected admission settle without
approving a fee. Both Send review and the standalone PSBT model consume this
reader. No new worker thread is allocated per document or model.

Protected commands compare the exact unsigned transaction and the current
approved fee against the retained proof before invoking signing, and validate
returned candidates again before installation or submission. A rejected signer
candidate leaves the original document available for a matching retry. Export
retains the original imported document, including rejected fee metadata, for
inspection and repair.

This is the R03 urgent fee-authentication repair. The existing synchronous
file parsing, wallet classification, wallet normalization, and broader PSBT
analysis/presentation paths remain the R21 M09 migration obligation. The full
scheduler and cancellation ownership migration remains R08. UTXO availability,
reorg/spend admissibility and final submission validation are separate from
this amount-provenance decision; the node still validates submitted transactions.

Regression protection includes real legacy signatures whose witness metadata
claims a one-satoshi fee while the independent UTXO/mempool oracle establishes
500,000 sats. PSBT versions 0 and 2 cover owned, mixed and foreign inputs with
matching valid controls, including witness-only unconfirmed parents. Manual schedules cover delayed effect, outcome and
publication, stale and duplicate results, rejected admission, failure, model
loss, shutdown and node replacement. Real-thread tests hold a node read while
the GUI processes an independent heartbeat.

The pre-existing positive synthetic witness fixtures used invented outpoints
without parent commitments. They now construct matching full parent transactions
before signing. Their selectors and behavioral assertions remain: valid partial
signatures survive approval and export; complete foreign multisig transactions
broadcast; original PSBT metadata survives export; incomplete multisig stays
review-only; negative fees cannot broadcast. Owned and foreign unsigned review
controls also use matching parents rather than empty-script witness placeholders.
Real witness-only documents retain positive protection through the background
reader and independent Core integration/process journeys.
