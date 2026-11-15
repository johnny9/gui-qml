# R24 device changes during an operation

The user requires device changes during a flow to preserve correctness. This is
an acceptance requirement, not a physical-device identity requirement. A wallet
fingerprint is an origin hint; it is not proof that a signer can service the
original descriptors.

The original wallet opening, reviewed input/output terms, authenticated prevouts,
fee and PSBT revision remain fixed after admission. Replacing a device must not
retarget those values or trigger an automatic retry. A replacement with compatible
keys may sign them. An incompatible replacement, changed terms or invalid
finalized signatures must fail without submitting a transaction or replacing the
review with returned untrusted data.

Qualified evidence already exists for the main Send guard at b31e8412645d20a4c7c746330abeb1b0983650a5
and for standalone Inspect/Sign/Broadcast in R21 final61002115b2bf0df2392b6359648d40ac5f8e7d6f.
The latter has six actual compatible/incompatible-key rows within its focused
runtime selection. These results do not complete R24.

Remaining focused R24 schedules must hold signer admission, the process result,
Domain delivery and GUI publication independently, then change configuration,
wallet/view or document. Test both old success and old error. Assert the original
operation identity, exact effect counts, no publication into a successor, no
automatic replay, preserved actual/unknown outcomes and final drain. Include
same-key replacement and incompatible keys under the original descriptors.

Partial outputs need their own treatment: finalized-input verification does not
authenticate every unfinished partial-signature map. Review error/exception
branches as well as normal completion; retaining recoverable bytes must not
claim verified completion or grant submission authority. Preserve draft255's
baseline and reproduce any claimed user-visible defect independently.

The public wallet call may block inside Core during device execution. The app
must keep GUI and Domain delivery responsive and must not claim physical
cancellation or lock release that the public interface cannot provide. The
already conditionally accepted R09 upstream limitation remains explicit. Core
and Qt stay unchanged. No physical hardware qualification is claimed.

R24 prerequisites remain R15, R19, R22 and the conditional R09 boundary. R22 and
R20 are not yet accepted; independent audit/preparation does not waive them.
