# R15 fingerprint and discovery semantics

User clarification, 2026-11-15: explicit device selection is not a requirement.
Core/HWI handles the device specifics. A signer must be able to service the
wallet's descriptors; the GUI must present an accurate selection/discovery
process. Physical-device binding is outside this program's current scope.
Preserve the pinned Core version's sole-connected-signer constraint; this does
not promise automatic selection among multiple connected signers.

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
