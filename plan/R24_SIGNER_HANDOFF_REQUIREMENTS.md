# R24 signer handoff requirements

The user explicitly requires correctness when a device changes mid-flow.
Physical device selection is not required; HWI/Core services the descriptors.
A short fingerprint alone never authenticates a replacement.

Once the wallet descriptors or reviewed candidate exist, all signing actions
retain the original wallet opening, descriptor keys, immutable document and
reviewed transaction. A replacement with compatible keys may continue. A device
with incompatible keys, an invalid signature, or an altered returned transaction
must fail clearly without retargeting, automatic retry or publication as success.
A later discovery result cannot replace a held operation or its outcome.

The R24 focused gate must exercise replacement before signer admission, while
a signer request/result is held, and before result publication, using manual
executors and controlled process fixtures. Verify unchanged transaction inputs,
outputs and amounts against the exact reviewed candidate; verify returned
signatures through Core's supported validation, not fingerprint equality. Include
same-key replacement, incompatible keys, stale result after wallet switch/close,
and a preserved unknown outcome when physical work cannot be cancelled.

Creation/discovery swaps are already covered by R15. R19 owns immutable candidate
and review; R24 owns these signing cases. This document is a requirement, not a
claim that R24 or physical hardware tests have passed. Core/Qt remain unchanged;
report any required unsupported public-interface guarantee before accepting it.

This boundary includes standalone PSBT inspection, local/external signing and
direct broadcast, as well as wallet Send. The R21 audit found a remaining
finalized-field trust gap in those standalone paths. Its guard and real-key
regressions must be qualified before that work is accepted; the main Send pass
does not establish a pass for every signing consumer.

Apply these requirements to main Send, SignOnly and standalone PSBT sign/broadcast.
Exercise both success and error at every held boundary. An unfinished partial
signature map is not proof of verified completion; preserve recoverable bytes
without granting submission authority. See [the focused schedule review](execution/R24-device-switch.md).
