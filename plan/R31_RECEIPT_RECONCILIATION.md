# R31 receipt reconciliation implementation

Implementation is prepared on top of accepted combined main `9c71709c629` and
unchanged Core `44901d8f64cb617714707bd025ecd83f108353c2`. Focused runtime,
static and original-parent comparison results remain pending. This note is not
an acceptance claim or an update to the shared tracking registers.

The existing opening-scoped `walletreceipts::Reader` and Domain-only `Ledger`
remain the single receipt read/reconciliation owner. Existing bounded Core
batches, ordered replacement rechecks, native revision fences, manual clock
backoff and background retirement are retained. The request Store remains the
single durable historical-use writer; current credit is not persisted as a
substitute for fresh Core evidence.

`ReceiptReconciliationModel` now owns the bounded presentation adapter and
native poll handoff previously embedded in `WalletQmlModel`. It consumes shared
immutable snapshots, performs bounded address lookups, and returns replaced
inventories to Reader for background retirement. An incomplete port cannot
capture a snapshot without its retirement service. Value publication and its
committed notification keep the old wallet facade's reentrant publication guard
coherent. No Core read or blocking I/O runs on GUI.

Each persistent request editor owns a typed `RequestReceiptModel` bound to its
original reconciliation owner. A later opening cannot retarget that facade.
Current satoshi credit, positive-receipt status, historical use, phase, freshness
and read error are separate facts. Watched-address membership prevents an
unobserved address being presented as a confirmed zero. Native revision and
reader readiness invalidate freshness before a held GUI publication arrives.

The saved-request card binds these properties. Zero current credit returns the
status to Awaiting; the historical-use notice and sharing prohibition remain.
A positive partial payment intentionally says Payment received with its exact
amount, without claiming the requested amount was paid in full. Read failure
retains the last amount as stale and offers an explicit check command.

Activity request copying now captures a bounded request/view sharing context
and uses the existing Source/View/Grant admission protocol. Preparation is not
clipboard admission. Payment during a held handoff, a successor context or a
hidden view revokes the original unadmitted effect. Successful delivery closes
the menu after the actual result; an error remains owned and visible. The
creation draft is independent and is never replaced by an Activity copy.

Focused tests include manual receipt/projection schedules, original-opening
guards, failure backoff, large shared-inventory retirement, real workers with a
held Core read, exact Activity copy effects, typed QML states, public Core
reorg/reconsideration/replacement and wallet reload. A functional scenario uses
the existing IPC harness for real checked process restart with an open request
view. An identical universal UI/Core reorg oracle is prepared on an accepted
test-only parent: the active-receipt control and the original reorg-to-zero
case remain separate.

Primary scopes: 099-RC73, 100-RC69, 100-RC79, 101-RC14, 123-RC14, 125-RC13,
126-RC13, 167-D167, 275-RC13, 276-RC13, 279-RC60. The positive-underpayment
claims in 101/123 remain held design claims, not invented paid-in-full bugs.
The source-only rebuild-deferral candidate 167 is not promoted without an
actual reachable failure. Existing R28/R30 association, aggregate, preview and
input bounds remain part of the dependency evidence; state labels alone are
not evidence of improved hostile-chain memory complexity.

No Core/Qt or persisted-format changes. The historical Widgets-compatible
address-use bit remains authoritative for reuse protection. This is not an
atomic guarantee against a payment arriving after final sharing admission.
Exhaustive coverage, platform/configuration matrices and sanitizer validation
remain deferred to R42 under the user's recorded cadence.
