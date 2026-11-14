# R17 public-interface constraints

Core is pinned at 44901d8f64cb617714707bd025ecd83f108353c2 and is unchanged.

## 048: accepted deferral requiring Core/interface changes

The public `interfaces::Wallet` needs a read-only capability along the lines of
`getTransactionChangeType(recipients, coin_control) -> OutputType`. Core can
implement this by calling its existing `TransactionChangeType` using the effective
explicit override/default and available managers, without reserving or keeping a
destination. The receiving-address default alone is insufficient: `-changetype`
can differ even when no Taproot recipient is involved. Public
`Node::getPersistentSetting` excludes command-line settings and cannot provide the
complete effective policy. The application must not clone Core's policy or read
wallet/node internals.

Unsigned `createTransaction` without an explicit dummy change destination retains
a destination in the pinned Core implementation. It is allowed in the disposable
integration oracle, but must not become a passive automatic preview that consumes
wallet keys. The existing dummy-change sizing route remains held for replacement
under the user's accepted deferral. The user chose to preserve Core automatic/configured
change policy because the authoritative review must remain correct. This finding
is unresolved and requires Core/interface changes; Core edits are not authorized.

`red-048/local-01/integration/report.json` preserves the exact A391+test-only public
Core proof: bech32 control 1410/1410 sat; Taproot preview 1530 versus actual
preparation 1650 sat; two executed bodies, one pass and one expected regression
failure, empty mempool. This is a reproduced defect, not a source-only candidate. The positive
`feePreviewLimitationKeepsCoreReviewAuthoritative` controls in local-07 pass both
rows: actual QML review displays1410 and1650 sat respectively, and public RPC
prevouts/output arithmetic independently authenticates those actual fees.
The1530-sat Taproot preview remains an explicitly accepted limitation.

## Existing public alternative: GUI-owned explicit change policy

`CCoinControl::m_change_type` can explicitly pin the policy for both dummy preview
and actual preparation. Choosing the public receiving default would allow the
existing bech32 dummy shape to match a Taproot recipient's preparation, and can
also override `-changetype=legacy`. This uses only the public interface and does
not require a Core patch. It is a product change: the GUI would choose the change
policy instead of honoring Core's effective automatic/configured default. It is
not a harmless inference of the existing policy. The user chose to preserve Core automatic/configured policy. This alternative is
not adopted in production; automatic-policy preview replacement remains deferred.

`explicitPublicChangePolicyMatchesThePreview` adds disposable public-Core controls
for bech32, Taproot, and an explicitly overridden legacy default. All three
public-Core rows pass in `core-controls-05/integration/explicit-public-change-policy`
and again in `local-05/integration/explicit-public-change-policy`: 1410/1410,
1530/1530 and 1410/1410 sat respectively, P2WPKH actual change, empty mempool.
The module passes the literal selector to `SendTestArguments` before app startup
(`test/integration/runner/test_main.cpp:422`), including `-changetype=legacy` for
the third row. These are disposable test-only preparations, not an adopted
production policy.

## Exact fee observation provenance

`CreatedTransactionResult` exposes the actual fee and `fee_reason`, but does not
expose the exact chosen nominal rate or returned estimator target. A separate
`getMinimumFee` read is an observation and cannot authenticate the nominal rate
of an earlier transaction when external policy may change. Prepared reviews use
the actual fee/reason and only expose a nominal rate when this exact native
request supplied it explicitly. An upstream addition of actual chosen nominal
rate and returned target to the transaction result would remove this limitation.

## External invalidation

The app captures draft intent, selected inputs, explicit fee policy, observed
requirements and processed-block versions; explicit refresh advances its backend
observation epoch. Public balance notifications/processed-block reads trigger
refresh even when the numeric balance did not change. There is no complete public
fee-policy/estimator revision notification. An upstream observation token covering
estimator, required/fallback policy and spendable-wallet changes would permit
stronger freshness claims across external RPC activity. Stale observations remain
visibly stale and never become current authority for a newer request.
