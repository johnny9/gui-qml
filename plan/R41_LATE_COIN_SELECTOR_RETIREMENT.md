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

Current model/QML/public-Core selection tests, static analysis and pinned native
checks are pending for this preparation commit. Do not mark the cleanup or R42
accepted before its evidence is recorded. Shipped QML inventory becomes 263;
retain every remaining file in final coverage accounting.

`draft-171-D171` concerns the old Activity getter notification. That getter is
now a plain C++ compatibility method without QML property/invokable exposure.
Production TransactionDetail binds an independent typed ActivityDetailModel,
which owns pending data and notifications. Record retirement of the source-only
trigger, without labeling a generic stop test proof of the old notification.
The live typed-view lifecycle retains independent tests and final qualification
obligations. Exact source references remain in the R42 scope-review artifacts.
