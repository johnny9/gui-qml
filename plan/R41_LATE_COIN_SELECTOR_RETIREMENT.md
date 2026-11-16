# Retired coin selector follow-up

Final source review found one remaining R41 cleanup: shipped
`pages/wallet/CoinSelection.qml` has no type or URL producer. Live Send
instantiates `SendCoinSelection`; its shared `CoinsList` presents typed selected
amount and target data. Translation source locations are historical references.

Remove the unused page/resource, its only `CoinsListModel.changeAmount` and
`overRequiredAmount` readers, and their mock aliases. This removes the unchecked
signed subtraction/absolute-value expression in `draft-266-RC30`. The separate
BitcoinAmount minimum-integer formatter repair does not prove the original
malformed-PSBT workflow and remains separate evidence. Do not invent malformed
private state or an obsolete workflow to increase counts.

Focused qualification passes 242 distinct bodies with zero failures/skips:
47 native coin-model, 13 coin QML, 180 SendCreate and two actual public-Core
selection journeys. Both 263-file QML variants, 13 pinned C++/MOC tidy entries
and app lint pass. Source and raw Core compiler-command identity receipts are
in workspace `R42/coin-selector-qualification/`. The current selector's source
is unchanged. Final combined suites and coverage remain R42 obligations.

`draft-171-D171` concerns the old Activity getter notification. That getter is
now a plain C++ compatibility method without QML property/invokable exposure.
Production TransactionDetail binds an independent typed ActivityDetailModel,
which owns pending data and notifications. Record retirement of the source-only
trigger, without labeling a generic stop test proof of the old notification.
The live typed-view lifecycle retains independent tests and final qualification
obligations. Exact source references remain in the R42 scope-review artifacts.
