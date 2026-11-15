# R24 finalized signer results after an error

A controlled public Wallet return reproduced four cases where incompatible-key
or invalid finalized signature bytes were retained after a signer error or
exception; two compatible-key controls passed. This is a defensive contract
repair. No physical-device failure or wrong-send defect is established, and no
finding is promoted from a source candidate.

The app now verifies every finalized input against the captured authenticated
prevouts before retaining an error/exception result. Matching valid partial work
remains recoverable. Unknown results remain Unknown and cannot automatically
retry. Unfinished partial-signature maps do not acquire verified completion or
submission authority from this check.

Test-only parent `3be493eac6da1dbbdaf9f44ff4e2451e23f9a820` uses production
`524379868c4b84fad7fdc5637eaa36be6556b582`. Candidate
`d28027473953cfd234a260fae1627342a9d3a412` with unchanged Core
`44901d8f64cb617714707bd025ecd83f108353c2` passes 33 focused QtTest bodies.
Six new manual schedules switch compatible/incompatible responses before Native
execution, before Domain delivery and before GUI publication; duplicate notices
cannot repeat the result or submit a transaction. The existing actual Core/HWI
substitution-send process scenario also passes with two signer effects; its
harness checks GUI exit status and stderr after shutdown.

Pinned Clang23/Bitcoin-plugin tidy passes three changed C++ units and seven fresh
MOC aggregates. App lint passes. QML source is unchanged. Exact commands, input
receipts, raw failures, counts and source hashes are under workspace
`.validation/qt6-r11-r40-20261114/R24/ERROR_RETENTION_ACCEPTANCE.json` and its
numbered directories. Initial parent01 could not access the Docker socket and
executed zero bodies; parent02 is the actual comparison.

This focused repair does not accept R24. R22, the complete Domain approval
extraction, held wallet/configuration changes, timeout/unknown and partial-result
lifecycles remain open. R09's accepted public Core wait/lock limitation and R42's
final coverage/platform/configuration/sanitizer qualification remain explicit.
