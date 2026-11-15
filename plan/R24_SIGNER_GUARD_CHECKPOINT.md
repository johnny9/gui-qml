# Signer key compatibility hardening checkpoint

The focused repair is integrated and qualified at application
`b31e8412645d20a4c7c746330abeb1b0983650a5`; Core remains unchanged at
`44901d8f64cb617714707bd025ecd83f108353c2`. This is a repair checkpoint,
not acceptance of the complete R24 extraction or physical hardware testing.

A replacement may service the original descriptor keys. Finalized signatures
must verify against the exact reviewed inputs, outputs and canonical previous
outputs. Fingerprint equality and a completion/finalization flag do not establish
that. Native code uses existing Core PSBT verification helpers. Prepared wallet
inputs come from the original public Wallet getTx/getCoins interface; imported
inputs use their authenticated proof. Public coin reads preflight known indices.
No wallet/node internals, subtree edits or new executor are used.

Verification protects SignOnly, direct PSBT sign-and-submit and direct submission
of prepared external-wallet transactions. Unsigned or incompatible candidates
cannot reach wallet commit. Signer errors and unknown outcomes cannot substitute
untrusted input metadata into a retained partial document. Compatible signatures
remain usable. The GUI projects typed failures; no retarget or automatic retry
is introduced.

The original three-row probe on ce039 production gives one compatible control
pass and two intended failures for incompatible keys/invalid final scripts. The
final affected gate passes85 WalletSend bodies and72 WalletQmlModel bodies
(157 total), including real crypto, controlled ordering, real worker and close/
drain controls. The actual Core/HWI substitution scenario passes, refusing a
changed payment and accepting an explicit exact matching retry. Pinned Clang23/
Bitcoin-plugin analysis passes3 changed C++ units plus7 fresh MOC aggregates;
app lint passes. R19's separate248-body/17-entry merged receipt is carried with
its exact source identity, not counted again as a full b31 run.

Existing mocks now provide consistent actual previous outputs/ownership and
valid unlocking scripts; real-key controls use the shared Core-signed fixture.
The preparation convenience wait honors the existing fee-policy admission guard.
Original no-commit, exact-transaction and lifetime assertions remain. Failed
numbered runs, initial fixture/launcher setup failures and the parent comparison
are retained separately; setup failures are not application reproductions.

Evidence: `.validation/qt6-r11-r40-20261114/integration-12/` —
`SIGNER_REPAIR_ACCEPTANCE-03.json`, `manifest-03.json`, `runtime-03/actual/`,
`static-03/`, `lint-03/`, `functional-03/`, and `BINARIES-03.json`.
Original proof: `R24/key-compatibility-probe-01/runtime-02/`.

The [R24 handoff requirements](R24_SIGNER_HANDOFF_REQUIREMENTS.md) still require
held-process device/config changes, timeout/close, stale publication and complete
partial-result lifecycle qualification. Actual physical devices are untested.
R42 owns exhaustive coverage, platform/configuration suites and sanitizers.
A future Core bounds-safe output query would remove the getTx preflight; no Core
API change is needed for this signature verification repair.
