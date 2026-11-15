# Signing-result follow-up

The R24 audit reproduced three actual public-wallet return counterexamples:
normal, error and exception responses can retain a corrupted unfinished ECDSA
contribution when only finalized inputs are checked. Parent `bbbb27f092b` keeps
main6151996 production unchanged; all three focused bodies fail. The isolated
R24 repair has 177 distinct positive bodies across recorded source carries,
actual HWI/IPC process checks and pinned static/lint evidence in
`plan/evidence/R24_PARTIAL_VERIFICATION.json`; integration/prerequisite acceptance
remains separate.

A second test-only source, `3d6efacd810`, exercises local SignOnly and standalone
PSBT Sign against the same contribution categories. Its 18 actual bodies include
six passing valid controls and 12 failures for invalid or unverifiable returned
contributions on normal/error/exception paths. No submission occurs. Raw evidence:
`.validation/qt6-r11-r40-20261114/R24/contribution-red-01/actual/report.json`.

Reopen the affected R21/R22 signing-result components. Propagate the shared
public-value verification and typed validity result; unknown context retains
exact recovery bytes separately and cannot acquire verified merge or submission
authority. Preserve the approved review, previously held signatures, independent
export ownership and original operation identity. This is ordinary correctness
work, not an R42 sanitizer/coverage deferral. Prior file/admission receipts stay
historical and are not overwritten. No Core/Qt change or private interface
exception is authorized. Record any missing Wallet-interface capability and
adapter in the upstream notes under the mandatory adapter-note amendment.
