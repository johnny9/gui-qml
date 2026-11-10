# Background ownership and lifecycle

`execution::TaskQueue` separates effect execution from domain outcome
consumption. Submission never executes either callback inline. An accepted
record keeps its count and payload budget until its outcome is consumed.
An immediate rejection returns a typed reason and starts no effect. A queued
cancellation or subsequent transport rejection produces an owned `NotStarted`
outcome and disposes effect captures on Cleanup.

The explicit bounded `BackendExecutor` constructor supplies shared Core,
Domain, Control and Cleanup transports. Migrated Core effects have one physical
worker. A fifth compatibility worker retains legacy blocking commands and GUI
completion until their callers migrate. This permits the consumer's audited
immutable wallet-name and locked wallet-catalog reads to progress while a legacy
operation waits for transferred handles to retire. It does not authorize
concurrent arbitrary wallet operations. Domain
consumes decisions and outcomes; Control and Cleanup remain available while
Core is blocked. Independent resource keys receive fair turns; each key keeps
FIFO order. Observation quotas leave room for actions, and bounded action
bursts permit observations to progress. The compatibility constructor retains
the existing serial transport and GUI completion API.

Default limits are 128 ordinary records/4 MiB, including at most 112
observations/3 MiB, plus 16 Control records/64 KiB and 16 Cleanup records/64 KiB.
These are transport budgets. Persistent backend catalog state remains owned by
its subscription and scales with the authoritative catalog.

A resource owner reserves Cleanup before acquiring backend resources. Its
private key cannot be reused within the scheduler. Activating the reservation
fences further work and waits for all same-key outcomes and counted native
leases. Final scheduler closure activates outstanding reservations. No worker
blocks waiting for another queued turn or a GUI observer to acknowledge a
publication. GUI performs the bounded native thread-stop handoff; drain waits
for every real thread join.

## Wallet load subscription

`walletlife::LoadSubscription` is the first lifecycle consumer. Its injected
registration, capture and notification ports are exercised with the production
queue in both manual and real-thread tests.

| Producer or effect | Execution and ownership | Progress and retirement |
| --- | --- | --- |
| Registration | Core; owner and Cleanup reservation already exist | Callbacks during registration enter the live ingress |
| Core load callback | Backend producer; counted lease and at most 64 pending handles | Sets one refresh/coalescing flag; overflow requires a coherent resync capability or an explicit unavailable outcome |
| Initial capture and refresh | Core; names and interface handles remain backend-owned | Same-key FIFO; queued refresh can retire through Cleanup while Core is blocked |
| Registered/changed/failure outcome | Domain; explicit lifecycle state guards publication | Produces value-only GUI messages; an absent observer cannot lose the outcome |
| GUI compatibility handoff | At most 16 entries per presentation turn | Background shadows retain every handle; separately reserved disposal owns their retirement |
| Handoff disposal | Cleanup, with its outcome consumed on Domain | Publishes the next batch continuation; parent retirement waits for its lease |
| Disconnect and final release | Cleanup after the key and leases drain | Disconnect failure is explicit; final drain also waits for physical joins |

Notification ingress captures the producing command's adoption context. A
delayed watch-only callback cannot publish a wallet whose setup failed, and an
unrelated delayed notification cannot take selection from a completed user
command. Context updates and ownership transfer perform no backend query on
GUI. Deferred or duplicate handles return through background disposal.

Initial capture and coherent resynchronization are separate injected ports.
Pinned Core calls `NotifyWalletLoaded` before `AddWallet`, so a concurrent
`getWallets` may miss a wallet whose callback already returned. This Core
composition supplies no coherent resynchronization port. Overflow therefore
invalidates wallet discovery, reports an unavailable outcome, disables dependent
catalog commands and retires the subscription. It never silently treats an
incomplete snapshot as current. Positive resync tests explicitly inject a
synthetic backend with the required committed-membership guarantee. R09 owns
the actual backend protocol and independent RPC consistency tests.

`WalletLifecycleTests` cover bounded handoffs, saturation/resync, rejection,
registration failure, command context and sixteen legal retirement schedules.
Real-thread cases check registration, metadata, notification, disconnect and
last-reference affinity, blocked registration, observer loss and join-based
drain. Synthetic traces contain only lane/task events and numeric identifiers.
`BackendExecutorTests` additionally hold Core and Domain independently and
check Control/Cleanup progress and reentrant GUI completion retirement.

This consumer retains the existing wallet facade constructors and command
methods. Their remaining GUI-side Core queries and domain transformations
still require the wallet/session extraction. Node question producers, other
feature owners and independent Core/RPC consistency guarantees retain their
own follow-up obligations. The lifecycle foundation does not establish those
consumers' full schedule or platform coverage.
