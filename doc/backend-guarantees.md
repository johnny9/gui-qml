# Wallet adapter guarantees

Production wallet adapters use `interfaces::Wallet` and the public wallet loader
lifecycle APIs. They do not use the testing `wallet()` or `context()` accessors,
wallet implementation fields, Core mutexes or implementation-service shortcuts.
The application does not modify the pinned Core subtree.

## Displayed trusted balance

`walletbalance::WalletRead` calls `tryGetBalances` on the background Core lane.
The returned trusted balance and wallet-observed block hash belong to that one
declared public read group. A busy wallet returns a typed busy observation; it
does not return a successful zero balance. This group does not include available
coins, a node tip, transaction details, authorization or a future mutation.

`walletbalance::Reader` uses the existing keyed, bounded observation scheduler.
The controller injects its shared R08 scheduler at each wallet-model creation;
the display reader creates no executor or thread. Standalone model constructors
retain their compatibility transport for existing direct users and tests.
Accepted effects finish on a worker and their outcomes are consumed on Domain.
Refreshes coalesce before admission; a notice during a read preserves another
refresh while completed observations continue to publish. This parameter-free
read stays tied to its original opening: another poll is interest, not a new
wallet identity or backend revision. Continuous refresh must not require a
quiet window before showing a balance. Domain retains the last good value
alongside later busy/error status, even if GUI has not consumed it yet.
A single-slot GUI mailbox
coalesces publications. GUI getters read cached values only. Initial absence,
busy, unavailable, failure and transport rejection remain distinguishable from
a known zero. A busy or failed refresh preserves an already displayed value.
Backend content invalidation uses `walletContentsChanged`, separately from the
cached balance's presentation notification. Publishing a balance must not start
legacy coin/address reads or invalidate send fee estimates. Delayed presentation
callbacks also guard retired QML roots and detached overlays during close.
Opening metadata still publishes at its original lifecycle point, with an
explicit unknown-balance flag. Later balance publications use that cached
name/key metadata and do not query Core. Legacy available-balance bindings use
content invalidation, independently of the displayed trusted balance.
The cached value is an observation, not a promise that independent RPC writers
have not changed Core since that observation.

The read closure stores a weak interface. Only an actual worker read retains a
strong handle, and releases it on that worker. An application close or observed
unload fences this reader at ingress and suppresses its pending publications.
Already admitted reads remain accounted through the scheduler's physical drain.
The GUI never waits for them. The reader has no separate task/outcome ledger,
wallet thread or persistent recovery journal.

The periodic presentation refresh and wallet notices enqueue interest only;
getters do not start work. A rejected refresh keeps interest for the next notice
or timer tick without spinning against a saturated executor or busy Core lock.
Tests independently control effects, Domain consumption and GUI publication;
real-worker tests verify affinity, held-read responsiveness and final release.

## Limits retained for the owning orders

This migration covers the displayed trusted balance. The QML `balanceSatoshi`
property reads `displayedBalanceSatoshi()`, while the existing native
`balanceSatoshi()` method retains its send-validation behavior for that owner.
It must not be used for QML or wallet-list presentation. Available-to-send
balance has distinct semantics and remains a separate consumer migration.
Other legacy wallet methods and handler registration are not made background-safe by adding
this port. Catalog/opening identity and a shared session borrower/closing
protocol remain owned by the wallet session and catalog orders.

Application closing is not an earlier backend unload fence. Local sequencing
does not serialize independent RPC or Core scheduled tasks. Scoped authorization
release, conditional persistent writes, structured wallet recording/broadcast
outcomes and device calls that hold internal locks retain their separately
documented Core interface/logic gaps. No local status flag closes those gaps.

After an uncertain mutation or restart, query authoritative state where the
interface permits it. In-process deduplication does not establish restart
idempotency, and the application must not automatically replay signing, writes
or broadcasts. This read-only adapter introduces no mutation/replay protocol.

Any later adapter workaround must record its actual guarantee, remaining race
or blocking limit, evidence, consumer owner and the Core logic/public contract
that would remove the workaround. An optional upstream simplification does not
block supported app work; a missing required backend guarantee remains open.
