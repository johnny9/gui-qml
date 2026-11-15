# R15 fingerprint and discovery semantics

User clarification, 2026-11-15: explicit device selection is not a requirement.
Core/HWI handles the device specifics. A signer must be able to service the
wallet's descriptors; the GUI must present an accurate selection/discovery
process. Physical-device binding is outside this program's current scope.
Preserve the pinned Core version's sole-connected-signer constraint; this does
not promise automatic selection among multiple connected signers.

Mid-flow device changes must preserve the original operation. Once wallet
descriptors or a reviewed transaction exist, a replacement must service those
same keys; incompatible keys must fail clearly without retargeting another
wallet, descriptor set, document or review. Same-key replacement is permitted.
R15 tests stale discovery/configuration and creation/Core-HWI handoffs; R19
keeps the candidate/review immutable; R24 covers the signing handoff. Use
manual executors and controlled process fixtures. Notify the user before
accepting a required guarantee that public Core interfaces cannot establish.

Mid-flow acceptance must cover replacement before creation, while Core requests
descriptors, after the wallet has stored descriptors, and during signing. A
failed descriptor handoff must not publish a usable wallet or silently retry.
If Core has already created a storage artifact, retain the uncertain outcome
and recovery guidance rather than declaring that nothing happened. A later
discovery result must not replace the operation's result or selected wallet.
Signing must preserve the exact reviewed transaction and authenticate the
returned signatures against its keys; a matching short fingerprint is not
sufficient. Creation/process controls belong to R15, immutable review to R19,
and signing/device-change controls to R24. These are required focused gates,
not a claim that physical hardware or all signing cases are already tested.

The external-signer protocol's fingerprint identifies a master key origin. It
is a short matching hint, not a physical-device identifier, a unique wallet ID
or a cryptographic proof of identity. Devices holding the same seed share it.
Core persists the actual wallet descriptors/public keys and key-origin data;
transaction signatures must validate against the relevant public keys.

The current GUI supports the sole-connected-signer flow and offers no explicit
physical-device or fingerprint chooser. Preserve Core's existing enumeration,
creation and descriptor persistence policy. Do not introduce physical-device
binding as an R15 acceptance requirement or treat its absence as a new blocker.
No passive address allocation or synthetic PSBT operation is needed to inspect
an origin for this flow.

M23's stable discovery identity means stable logical rows/results within their
effective configuration and discovery generation. Keep the protocol fingerprint,
display name, count, effective configuration and result provenance distinct.
Do not infer physical-device count or uniqueness from fingerprints. Generation
fences reject known stale app requests; they do not claim to freeze external
hardware or Core state.

If a future GUI offers explicit selection of signing keys, an expected-origin
creation argument and read-only origin metadata could improve the public Core
interface. A short fingerprint alone would remain a matching hint. This is an
upstream capability note, not a current R15 exception. Core remains unchanged.
