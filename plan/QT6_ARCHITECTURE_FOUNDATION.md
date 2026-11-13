# Foundation for the Qt 6 refactor and future features

Binding discovery rule: [discovered GUI work and model actions](.validation/issue-remediation-pr1090-20261109/execution/coordinator/discovered-gui-work-20261111/REQUIREMENT.md) must migrate to injected bounded background executors as found, with manual controlled timing unit tests and actual integration coverage. Existing legacy code is not an automatic exemption; getters use cached state, commands preserve original identities and distinguish admission from completion. Apply this to all work orders without changing original scopes, Core boundaries or acceptance gates.

R10 independent component: shared-observation retirement now joins the existing model executor before shutdown completion at `715fb5449366a9a267e6d111169f226b429947ef`. [Bounded qualification](.validation/issue-remediation-pr1090-20261109/execution/R10/drain-fix-20261111/qualification.json), [report](.validation/issue-remediation-pr1090-20261109/execution/R10/drain-fix-20261111/REPORT.md). This supporting fix is qualified; full R10 acceptance remains false. The accepted program head remains `c90cb60f222b76045d1a75d27497781f239c7f7b`, with R09 conditional. No original scope or full schedule is closed. The supported-capability decision below narrows the earlier metadata hold; full R10 qualification remains required.

R10 supported-capability decision: Multi-key wallet workflows are outside current GUI support, as clarified by the user. [Constrained decision](.validation/issue-remediation-pr1090-20261109/execution/R10/supported-capabilities-20261111/DECISION.md) scopes opening metadata to public Watch-only/external-signer facts and a generic software-wallet category, without asserting Single-key policy or transaction signing ability. Exact multi-key metadata is deferred to future wallet-feature work; it no longer blocks R10. Continue the authorized session/lifetime migration behind public interfaces. Preserve existing PSBT multisig behavior and tests, supported descriptor-import obligations, all original R10 scopes and full acceptance gates. No Core edit or internal classifier exception is authorized. R10 remains unaccepted; later orders remain unstarted pending normal dependencies.

R09 conditional acceptance: the user explicitly accepted forward progress at `c90cb60f222b76045d1a75d27497781f239c7f7b` under unchanged Core `44901d8f64cb617714707bd025ecd83f108353c2`. The public-wallet audit and tested balance consumer are accepted as the app-side prerequisite; the original full backend gate remains unsatisfied. [Decision and clear limitations](.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/NOTES.md), [five deferred contracts and consumer owners](.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/deferred-contracts.csv). Device waits may still hold Core's wallet mutex and block wallet RPC; background execution protects GUI but cannot release that lock. R10 now owns the opening/session lifetime work and its original eight scopes plus O04/O27. R00–R08 are bounded accepted; R09 is conditionally accepted; R10 is active and unaccepted. Preserve all 318 findings, 329 scopes, 43 orders, 36 schedules and existing platform/coverage gates.


Current base amendment: [base-pr1109-core44901-20261111](QT6_BASE_UPDATE.md) adopts PR #1109 head `000e49c81e60ccf6560ccdd1a93e811a5a44c475` and stock Core `44901d8f64cb617714707bd025ecd83f108353c2`, explicitly authorized by the user. R00–R08 are amended and qualified at application `e89249aa7b3074fa2a33bd02d2cccacf8e993aa6`; the [new-base gate](.validation/issue-remediation-pr1090-20261109/execution/base-update-pr1109-20261111/qualification.json) passed. Original evidence remains historical; R09 was subsequently conditionally accepted under the explicit user decision above. Current static depends qualification requires the new stock Qt6.8.4 recipe (historically6.8.3). Preserve the Core subtree unchanged at this new pin and retain the upstream-note/notify-and-wait policy.

This is an implementation acceptance contract for the [refactoring plan](QT6_TOTAL_REFACTOR_PLAN.md). It incorporates the useful refinements from the [earlier FSM review](QT6_FSM_PROPOSAL_REVIEW.md). It describes the foundation to build and test, not an implemented framework or a claim of runtime correctness.

## Dependency boundaries

| Layer | Owns | May depend on | Must not depend on |
| --- | --- | --- | --- |
| Domain values and decisions | Typed identities, canonical amounts, validation, state/event reduction, effect intentions | Narrow value interfaces; injected values such as time/IDs | QML, GUI projections, Core handles, filesystem/process calls, executor globals |
| Domain owner and execution | Serial mailbox, task/outcome ledger, lifetime, dependency tracking, effect dispatch | Domain decisions; narrow execution/backend ports | QML objects, mutable GUI models, synchronous GUI answers |
| Backend/platform adapters | Core synchronization, versioned reads/writes, process/file effects, owned resources | Domain request/result values; Core/platform implementation | Feature pages and mutable current selection |
| GUI projections | Cached typed properties/roles, coherent publication, transport commands | Value snapshots and command transport | Core, blocking I/O, worker getters, business-state mutation |
| QML and native presentation | Bindings, focus, animation, IME, clipboard/dialog/scene handoff where required | Typed projection APIs | Core interfaces, backend state setters, domain polling or workflow synchronization |

Keep feature-specific policies in their feature owners. Share only protocols with proven consumers: execution, reader lifecycle, editor persistence, file writing, factory transport and scoped authorization. Do not introduce a universal model base containing every workflow, an untyped global event bus, or one thread per object. The composition root wires production versus test ports explicitly; features do not discover dependencies through a service locator.

Production wallet adapters also stay strictly behind `interfaces::Wallet` and `WalletLoader` lifecycle APIs under the [user-selected interface boundary](.validation/issue-remediation-pr1090-20261109/execution/R09/interface-boundary-20261111/CONTRACTS.md). Keep Core synchronization and shared RPC invariants in Core; do not use its testing accessors, internal objects, mutexes or fields as an app implementation route. Public interface value types remain allowed. Observable lifecycle/security state supports presentation and invalidation; protected commands still need backend-enforced admission, conditional release or compare-and-write where required. No additional app FSM or status getter supplies those atomic guarantees by itself.

R01 establishes deterministic decision/effect interfaces with one existing consumer. R08 adds required lifetime/scheduling facilities. New abstraction capabilities arrive with the consumer and tests that need them. A pure decision function is the default; deterministic private mutation is acceptable without expensive copying if it preserves the same observable semantics.

The [app-side wallet adapter plan](QT6_WALLET_ADAPTER_PLAN.md) is the selected implementation direction under the current Core restriction. Use narrow injected public-wallet effect ports, a retained background opening context and the existing R01/R08 scheduler/ledger. Feature owners retain their own domain state; do not introduce a universal wallet FSM or duplicate task accounting. The adapter can order app actions and own close/drain, cached observations and uncertain outcomes, but cannot make independent RPC or Core timers consult its local state. Record each stronger backend gap and later Core logic/interface improvement in the linked follow-up register, including usable app workarounds. No new guarantee is accepted merely by planning its state.

R41 adds build/include dependency checks where the repository can enforce these boundaries, plus runtime affinity and QML publication tests where static checks cannot. A string search alone cannot certify thread safety or absence of indirect GUI backend calls.

## Extension contract

Every new feature or material API change must include the following in its implementation change, using the existing task/report artifacts rather than a separate approval process:

1. **Owner and lifetime:** parent, exact resource identity, admission/retirement rules, retained outcome owner, and optional independent view lifetime. A new wallet opening always creates new objects.
2. **Public interaction:** typed properties, notifications, commands and disabled reasons. Specify request acceptance separately from eventual result. Add the public command to the action-routing catalog.
3. **Decision and dependencies:** states only for genuine workflow phases; typed independent facts for other dimensions. Declare the affected versions and which presentation changes do not invalidate work.
4. **Producer and effects:** real event source/call site/thread, payload ownership, effect lane/key, bounded admission and coalescing policy. Record what each pending state waits for and its rejection/close/timeout path.
5. **Backend authority:** consistency group, final write preconditions, concurrent RPC policy, idempotency scope and recovery from unknown outcome. Do not infer backend guarantees from actor counters. Record limitations and proposed upstream simplifications in [upstream improvement notes](QT6_UPSTREAM_IMPROVEMENTS.md), including when an app-only workaround exists. Under the [Core subtree policy](QT6_TOTAL_REFACTOR_PLAN.md#core-subtree-boundary-and-upstream-follow-ups), a required guarantee unavailable without subtree changes must be reported to the user, with the affected fix and dependent work held until their input; this contract does not authorize editing Core.
6. **Presentation:** late/recreated view behavior, local focus/animation/IME state, active-interest policy, and callbacks that can outlive the view.
7. **Acceptance:** independent unit oracles, allowed/rejected transition coverage, manual schedules, appropriate real integration and physical-thread tests. Record actual selectors and results.

A small local selection/value change need not acquire an asynchronous operation lifecycle. Conversely, an operation with independent failure, cancellation, outcome or resource ownership must not be hidden in a property setter. Reuse an existing protocol only if its invariants fit; document a feature-specific extension rather than weakening the common invariant.

## Event, effect and progress records

For each migrated owner, keep a compact record in its code/tests or adjacent repository documentation:

| Record | Required fields |
| --- | --- |
| Event producer | Typed event; actual producer/call site; thread; original lifetime; payload ownership; relevant revision; ordering guarantee; duplicate/coalescing policy; detach behavior |
| Effect | Typed request/result; operation token; lane and resource keys; admission rejection; safe cancellation boundary; actual-write boundary; result/cleanup owner; known versus unknown failure |
| Pending phase | Outstanding tokens; expected producer; completion assumptions; deadline/clock if meaningful; query-only recovery; parent-close path; drain/transfer condition |
| Observation | Consistency group; backend version or other evidence; capture/retry policy; stale/partial presentation; final protected-action check |

R08 establishes the format with a real owner; every migration fills it for its actual producer paths. R42 checks that all pending phases and consumed events have corresponding records/tests. These records supplement the generated state/event matrix; no table count proves producer coverage or progress.

Backpressure is part of the contract: specify queue/task/byte limits and overflow behavior. Coalesce only replaceable observations, never unaccounted writes or required outcomes. Fair dispatch and reserved control/cleanup capacity must be tested under saturation. If a backend call cannot safely be interrupted, state that limitation; do not promise a universal shutdown deadline.

## Identity, compatibility and recovery

Use distinct internal C++ types for operation, review, document, request and revision identities where practical. QML's opaque strings are a transport representation, not permission to interchange token types. Carry immutable value payloads, never raw GUI pointers or QModelIndex across workers. Private opening tokens are lifetime-safe; wallet names are not opening identities.

In-process command deduplication lasts for the defined owner/ledger lifetime. It does not imply exactly-once effects across crashes or restarts. For each persistent operation, identify what can be authoritatively reconciled after restart and what cannot. If reliable recovery requires a durable journal or backend token, introduce it as a separate validated persistence change; until then expose uncertainty without replaying the mutation. Do not serialize transient authorization, plaintext secrets, QObject identities or internal enum ordinals as durable state.

Persistent format changes require versioned readers, migration fixtures, interrupted-migration recovery and the relevant backward/Qt Widgets compatibility policy. Keep model extraction separate from format migration. A changing public QML API keeps a narrow forwarding adapter until all consumers and equivalent tests migrate; remove the adapter with an explicit caller audit.

Errors are typed by validation, unavailable capability/lifetime, conflict, executor rejection, backend known failure and unknown outcome. Retry permission is derived from ownership/outcome, never from a generic error flag. Optional logs record IDs, phase/event kind and timing/queue information only through an allowlist. Do not dump event payloads, credentials, RPC input, wallet paths, addresses or transaction documents into traces. Deterministic replay uses sanitized synthetic test fixtures; a production trace is not automatically a replayable or safe fixture.

## V8: request sharing and QR effect protocol

This closes the interface refinement identified in the earlier review. It is a composed protocol under the existing request/reservation/file-write owners, not a 33rd global actor. QML receives a view-scoped `RequestSharingModel`; each copy action creates a separately owned `ShareOperationModel`. A QR export uses the existing M24 file-write owner with an additional revocable sharing source guard.

`RequestSharingModel` has independent interest states Active, Inactive and Closed. Active does not itself mean shareable. `canCopyUri`, `canCopyQr`, `canExportQr` and `disabledReason` derive from the exact saved request, original wallet opening, payload revision, historical-use status and current presentation interest. The factory starts Inactive; `setActive(true)` expresses view interest, not backend authority. Hide/deactivation advances the presentation token and revokes pre-admission sharing. Reactivation requires fresh guards. Parent retirement is terminal Closed.

| Share operation phase | Event | Guard / effect | Next phase |
| --- | --- | --- | --- |
| Checking | Validate | Exact request/opening/interest and supported kind; sharing currently permitted | Preparing for QR; Authorizing for URI |
| Checking | Rejected | Known validation/admission failure; no effect | Failed |
| Preparing | ImageReady | Owned encoded image/value for exact payload and operation; no GUI object crosses boundary | Authorizing |
| Preparing | PreparationFailed | Exact job failed/rejected; resource accounting complete | Failed |
| Authorizing | DeliveryAdmitted | Recheck request/payload/use/interest; atomically reserve one effect token | Delivering |
| Authorizing | AuthorizationRejected | Source or presentation changed; no external effect admitted | Failed |
| Delivering | DeliveryResult | Exact adapter effect; known successful/failed result | Completed or Failed |
| Delivering | ResultUnknown | Effect may have happened; absence of result cannot establish failure | OutcomeUnknown |
| Checking/Preparing/Authorizing | Cancel or invalidation | Revoke delivery permission; account any preparation | Cancelling |
| Cancelling | DrainComplete | No outstanding preparation/admitted effect | Cancelled |
| Delivering | Cancel/owner close | Effect already admitted; retain accounting and suppress obsolete presentation | Delivering |
| OutcomeUnknown | OutcomeEstablished | Original adapter/ledger can establish result without replay | Completed or Failed |

Completed, Failed and Cancelled are terminal per-operation states. OutcomeUnknown is retained as an explicit outcome; query only when the adapter can resolve it, otherwise report that limitation and prohibit automatic replay. Duplicate/stale result events perform ledger cleanup only; invalid commands reject. Parent close for pre-admission phases follows Cancelling; for Delivering/OutcomeUnknown it retains or transfers the outcome owner. Cancellation never erases a clipboard or file effect already performed. Repeating a user action after a terminal result creates a new operation, not a reset old token.

URI copy can deliver a captured string through a native adapter. QR generation should prefer rendering from owned payload data off-GUI; if exact scene capture is required, use a narrow GUI capture adapter and convert its result to an owned safe value before background encoding/writing. The final native adapter rejects stale presentation tokens before performing a delayed effect. This closes the queued hide-versus-delivery gap without querying Core on GUI.

QR export captures exact request/payload/image provenance. M24's path validation and overwrite policy still apply. Its write-admission guard additionally checks the original sharing authorization; a retired request or used address must not be treated like an unrestricted standalone PSBT export. Parent sharing invalidation cancels only before write admission; afterward M24 owns the actual result. Cross-owner admission uses a token/acknowledgment, never a synchronous GUI/actor wait. Relevant use/retirement ingress revokes authorization before publication; no claim is made that an external payment cannot occur after the final check.

Regression rows belong to R30/R31 and O16/O32/O33: payment during capture/chooser, edits/deletion, switch/unload, hide/reactivate, duplicate clicks, stale image, executor/adapter rejection, write admitted before invalidation and lost result. Retain immutable historical-use semantics when current credited totals fall.

## Future-change rehearsal gates

Use small test fixtures in the existing protocol suites, not speculative shipped features:

| Evolution pressure | Required demonstration | Owner |
| --- | --- | --- |
| Another consumer of the reader protocol | Own work keys, error and lifecycle; refresh during Applying retained; no new executor framework | R17/R28 |
| A different transaction-attempt adapter | Replacement/import reuse lifecycle while preserving distinct validation and exact approval; no enum cross-product | R25/R26 |
| Two simultaneous views of one wallet | Shared authoritative domain state, independent presentation/sharing tokens; hiding one does not cancel the other's admitted work | R10/R30/R37 |
| Backend capability unavailable | Typed unavailable state and command rejection; no synchronous fallback, no permanently busy projection | R09/R15/R23 |
| New event/effect variant | Exhaustive decision handling or explicit rejection; producer, guard, cleanup and scheduling tests accompany it | R01/R08/R42 |
| Recreated view after completion | Retained outcome/prompt renders without signal replay; old facade cannot target a reopened wallet | R11/R20/R37 |
| Persisted schema evolves | Dedicated version/migration and interrupted-recovery tests; no in-memory operation-token replay | R13/R35/R42 |

The foundation is acceptable when these seams have real consumers, dependency/affinity checks and measured coverage. Do not build a plugin system, generic workflow DSL, universal event sourcing or durable operation journal merely for hypothetical future use. The architecture must make those choices possible without requiring them now.

## Core-retained value ownership (R10 discovery, 2026-11-13)

Values supplied through a public Core interface must have an ownership lifetime compatible with Core retaining them. A Core-retained shared pointer must not carry an app cleanup lease whose retirement waits for that Core owner to disappear. Construct an independent immutable value on Native before the original consent/effect boundary; preserve the reviewed content and original outcome. App presentation may wrap its own result after the public effect, without changing the ownership already retained by Core. The [committed transaction cycle and controlled regression](.validation/issue-remediation-pr1090-20261109/execution/gui-threading-implementation-20261112/CORE_TRANSACTION_OWNERSHIP.md) records the concrete case. This requires no subtree change or private-wallet exception.

## R10 per-view and opening-publication refinement (2026-11-13)

Activity views share the original opening/domain snapshots and executor, while each detail page owns typed selection, active interest and result publication. The immutable published snapshot carries its display unit and independent Native-owned detail source; never pair a newer mutable source frame with older rendered rows. Keep cached Domain previews/read-only prepared entry documents available while full public Core reads are pending, with original identity/generation fences. A projection may not retain a publishing-job cleanup lease through its own snapshot. See [view correction](.validation/issue-remediation-pr1090-20261109/execution/gui-threading-implementation-20261112/ACTIVITY_VIEW_INTEREST.md).

GUI interest contexts are not Core causal or backend-instance IDs. Original load results/notifications retain a bounded adoption origin bound to the original facade/session; close fences that token before reentrant Qt publication. A fresh observed load after an original Native unload uses a fresh per-entry origin, without rewriting old bindings. Shared app event stamps distinguish observed Native ordering; origin-retention/pruning counters are separate. Neither stamp is Core state authority, and an unload missed before handler registration is not repaired by app counters. Preserve explicit uncertainty and upstream notes. [Controller port and composed regressions](.validation/issue-remediation-pr1090-20261109/execution/gui-threading-implementation-20261112/O04_SOURCE_BATCH17.md) describe the narrow Scheduler/Post/asynchronous-drain injection and retained metadata budgets. Source additions remain unqualified until their recorded candidate passes.
