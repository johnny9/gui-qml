# R25 replacement attempt checkpoint

Implementation is isolated and not yet accepted or integrated. The source uses
Core `44901d8f64cb617714707bd025ecd83f108353c2` unchanged. Its latest production
candidate is `5179333f4ac`; validation is in progress.

Replacement preparation and confirmation use the shared M04 attempt owner with
an immutable original transaction, exact candidate and approved input scope.
Added inputs are rechecked before and after signing; known policy rejection
requires explicit preparation instead of an automatic rebuilt submission.
The original public wallet owns execution, result delivery and cleanup even
when GUI delivery is held or the view closes. A published review retains its
Native cleanup receipt until retirement; close/reset releases every projection.

A controlled public-wallet test found that unchanged unsigned terms alone allowed
an invalid external signature result to reach commit. The repair reuses the
existing public-wallet prevout and Core script verifier. The valid control and
all original substitution/no-replay/lock-restoration assertions remain required.

Evidence is retained under
`.validation/qt6-r11-r40-20261114/R14/r25-*` and
`.validation/qt6-r11-r40-20261114/R25/`. Failed setup, stale generated-code,
fixture timing and actual ownership/signature failures remain in their original
numbered runs. The focused-11 model gate is 51/51; earlier focused-10 separately
passed 11 Domain, 3 wallet-consumer, 1 QML and 2 real Core/RPC bodies. The new
signature guard requires fresh focused qualification, lint/tidy and actual
process checks; these earlier passes are not final acceptance.

[Upstream notes](QT6_UPSTREAM_IMPROVEMENTS.md#r25-replacement-signing-and-commit-observations)
record the missing typed verification/atomic replacement receipts, the app
adapter's actual guarantees and remaining observation races. Wallet persistence
is not a claim of mempool acceptance, relay or confirmation. No required Core
change has been made. Full matrices, coverage and sanitizers remain for R42.
