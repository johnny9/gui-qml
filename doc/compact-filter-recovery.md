# Compact-filter wallet history recovery

Wallet history recovery starts automatically for a loaded descriptor wallet when the node is configured with both pruning and the basic block filter index. There is no separate recovery-enable option. For example, a node started with `-prune=2048 -blockfilterindex=1` uses this flow after its chain and filter index are ready.

The filter index must cover the historical blocks being searched. Enabling a new index after its source blocks have already been pruned still requires obtaining the data needed to build that index. Configure the index before pruning the history you want it to retain.

## User interface

A progress banner appears below the wallet navigation tabs. It remains available on Activity, Send, Receive, and wallet settings. It shows waiting, search, download, and wallet rescan progress.

- Cancel stops recovery after the current bounded operation. History remains marked incomplete until Retry completes it.
- Retry starts discovery again. Existing wallet transaction insertion is idempotent.
- An encrypted wallet offers Unlock. An incorrect password leaves recovery incomplete. A wallet unlocked for this operation is relocked when that operation ends.
- Missing peers, unreadable filters, failed downloads, and chain changes produce an incomplete state with an error and Retry action.
- The wallet badge and selector hide numeric balances while history is incomplete. The application blocks send, fee-bump, PSBT signing, and broadcast actions during that state.
- Completion is shown briefly. Loading the wallet in a later application session starts another verification/recovery pass; no persistent recovery checkpoint is trusted to skip discovery.

## Recovery path

`WalletHistoryRecoveryController` runs `wallet_history::Recover` through a dedicated `BackendExecutor`. It publishes thread-safe progress snapshots to QML. Node RPCs, wallet script snapshots, filter reads, block retrieval, and wallet rescans run off the GUI thread.

The backend:

1. Checks pruning, index availability, descriptor-wallet support, initial sync, background validation, and wallet unlock state.
2. Reads cached scriptPubKeys from every script manager under the wallet lock. This includes hardened derivation paths from imported descriptors and migrated HD wallets, without exporting private keys.
3. Searches basic filters from genesis using the cached scripts. It uses `LookupFilterRange` directly and requires every requested filter, so a read failure cannot silently count as a successful scan.
4. Downloads unavailable candidate blocks from connected peers advertising full historical block service. It waits for actual block availability and bounds retries.
5. Calls the existing `rescanblockchain(height, height)` wallet RPC for candidate blocks in ascending height order. The wallet handles ownership, spending transactions, persistence, and normal transaction notifications. Re-fetched blocks do not need undo data for this path.
6. Repeats discovery from genesis when rescans expand the wallet's cached script set. A bounded expansion limit leaves the job incomplete instead of claiming success prematurely.
7. Catches up incrementally when the chain grows during recovery. This covers spends that arrived before the wallet knew their historical funding transactions.
8. Performs a final open-ended rescan from the current tip to replay current mempool transactions, then checks cached scripts and the chain again before completion.

The controller checks the chain anchor throughout recovery. A reorg stops the job with an explicit incomplete state. Cancellation is checked while collecting scripts and between filter batches, block operations, and download polls. On unload or shutdown, wallet retirement waits for the recovery operation on a worker; the GUI thread does not wait for it.

Core wallet source and the Bitcoin submodule revision are unchanged. There is no dependency on `txoscriptindex`, `txospenderindex`, or a new proof-import path.

## Scope

Recovery covers the descriptors and derivation ranges known to the loaded wallet, with expansion driven by the existing wallet keypool behavior. It does not discover arbitrary accounts, script types, or address use beyond an unknown derivation gap. Basic filters omit empty and OP_RETURN scripts; unsupported descriptors fail explicitly.

This is a recovery flow for a loaded descriptor wallet. It does not bypass Core's existing refusal to load an old wallet backup whose required initial rescan data has been pruned. Labels, notes, and transactions that never confirmed still require wallet metadata or current mempool data.

The implementation has regtest and GUI coverage. Mainnet recovery duration, download volume, and large-wallet memory use have not been benchmarked. Discovery currently starts from genesis on each load or manual retry.

## Tests

Build with `-DBUILD_APP_TESTS=ON -DENABLE_TEST_AUTOMATION=ON -DBUILD_DAEMON=ON`.

- `WalletHistoryRecoveryBackendTests`: readiness, checked filter reads, no-change spending order, peer failures, cancellation, script expansion after block and mempool rescans, reorgs, new-block catch-up, and final mempool replay.
- `WalletHistoryRecoveryControllerTests`: progress state, retry, GUI responsiveness, cancellation, unlock handoff, destruction, and shutdown completion.
- `WalletListModelTests`: incomplete balances remain hidden across model refreshes.
- `WalletQmlModelTests` and QML tests cover blocked external-signer approval during recovery. QML tests also cover the banner, controls, wallet badge, and selector.
- `test/functional/qml_test_wallet_history_recovery.py` runs real application flows: pruned receive/no-change-spend recovery (including a hardened-wildcard wallet and keypool expansion), Activity rows, fee and balance, retry, restart, disabled configurations, encrypted unlock/relock, unload, and shutdown.

The functional test is included in the GUI functional-test workflow.
