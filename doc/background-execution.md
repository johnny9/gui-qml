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
These are transport budgets. Persistent wallet inventory remains owned by the app subscription and scales
with the wallets retained by the model. The ready inventory is not limited to
64 wallets and does not trigger another getWallets call when it grows.

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
| Core load callback | Backend producer; counted lease and at most 64 pending handles | Sets one refresh/coalescing flag; production overload posts an owned capacity failure through independent Control |
| Initial capture and refresh | Core; names and interface handles remain backend-owned | Same-key FIFO; queued refresh can retire through Cleanup while Core is blocked |
| Registered/changed/failure outcome | Domain; explicit lifecycle state guards publication | Produces value-only GUI messages; an absent observer cannot lose the outcome |
| GUI compatibility handoff | At most 16 entries per presentation turn | Background shadows retain every handle; separately reserved disposal owns their retirement |
| Handoff disposal | Cleanup, with its outcome consumed on Domain | Publishes the next batch continuation; parent retirement waits for its lease |
| Disconnect and final release | Cleanup after the key and leases drain | Disconnect failure is explicit; final drain also waits for physical joins |

Notification ingress captures the producing command's adoption context. A
delayed watch-only callback cannot publish a wallet whose setup failed, and an
unrelated delayed notification cannot take selection from a completed user
command. Context updates and ownership transfer perform no backend query on
GUI. Deferred or duplicate handles return through background disposal. A
GUI delivery acknowledges the previous change publication, even during nested
event loops that cannot yet claim its data;
repeated backend changes retain at most one unacknowledged value-only wakeup.
A disposal outcome supplies the next batch continuation without GUI polling.

The production app subscribes before the startup capture, seeds the wallet
list from that capture, and merges notification-supplied handles by wallet name.
A pre-AddWallet notification remains represented by its retained handle even
when getWallets does not contain it yet. Discovery uses the existing Core APIs;
the subtree wallet and node modules remain unchanged.

The app subscribes before its one startup capture and preserves notification
handles. Pinned Core notifies before `AddWallet`, so a later `getWallets()` call
cannot replace those handles or recover discarded notifications. There is no
alternate-backend resynchronization port. Raw ingress is bounded to 64 pending
handles; overflow produces one owned, explicit failure through Control even
while Core work is blocked. Ready inventory grows with retained wallet models
and GUI claims remain bounded to 16 entries. Cleanup releases all unclaimed
handles off GUI. Core remains unchanged; API simplification opportunities are
recorded in the workspace upstream-improvement ledger.

`WalletLifecycleTests` cover bounded handoffs, saturation and explicit failure, rejection,
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

## Upstream task API and rebased Core

The base now includes PR1109's task IDs, explicit value/error/cancellation
outcomes and replayable `whenDrained()` observation. The stock Core pin is
44901d8f64cb617714707bd025ecd83f108353c2. Wallet-controller shutdown consumes
`whenDrained()` after the final physical join, including all five workers in a
bounded group; observation remains safe after executor-facade destruction.
Accepted task packets remain on their native worker until the GUI completion
has released its callback state. Cancellation or lost observation cannot move
discarded backend-resource destruction onto GUI.

`submitTask()` is the GUI compatibility transport. Its callbacks carry bounded
presentation values. A submission after shutdown returns a task ID and queues
Rejected rather than returning the old submit API's boolean rejection. New
feature migrations explicitly handle that outcome. Running-result discard is
for replaceable observations; mutation results stay owned and are reconciled.
Migrated domain owners continue to use the injected scheduler port: their
outcomes and decisions execute on background Domain, independently of GUI
observers. The compatibility API does not authorize domain/Core work in a GUI
completion or replace the scheduler's keyed admission and reserved cleanup.
