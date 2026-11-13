# Earlier FSM proposal: adoption review

Binding discovery rule: `discovered GUI work and model actions <.validation/issue-remediation-pr1090-20261109/execution/coordinator/discovered-gui-work-20261111/REQUIREMENT.md>` must migrate to injected bounded background executors as found, with manual controlled timing unit tests and actual integration coverage. Existing legacy code is not an automatic exemption; getters use cached state, commands preserve original identities and distinguish admission from completion. Apply this to all work orders without changing original scopes, Core boundaries or acceptance gates.

R10 independent component: shared-observation retirement now joins the existing model executor before shutdown completion at `715fb5449366a9a267e6d111169f226b429947ef`. `Bounded qualification <.validation/issue-remediation-pr1090-20261109/execution/R10/drain-fix-20261111/qualification.json>`, `report <.validation/issue-remediation-pr1090-20261109/execution/R10/drain-fix-20261111/REPORT.md>`. This supporting fix is qualified; full R10 acceptance remains false. The accepted program head remains `c90cb60f222b76045d1a75d27497781f239c7f7b`, with R09 conditional. No original scope or full schedule is closed. The supported-capability decision below narrows the earlier metadata hold; full R10 qualification remains required.

R10 supported-capability decision: Multi-key wallet workflows are outside current GUI support, as clarified by the user. `Constrained decision <.validation/issue-remediation-pr1090-20261109/execution/R10/supported-capabilities-20261111/DECISION.md>` scopes opening metadata to public Watch-only/external-signer facts and a generic software-wallet category, without asserting Single-key policy or transaction signing ability. Exact multi-key metadata is deferred to future wallet-feature work; it no longer blocks R10. Continue the authorized session/lifetime migration behind public interfaces. Preserve existing PSBT multisig behavior and tests, supported descriptor-import obligations, all original R10 scopes and full acceptance gates. No Core edit or internal classifier exception is authorized. R10 remains unaccepted; later orders remain unstarted pending normal dependencies.

Reviewed `GUI_QML_STATE_MACHINES.md`, including its implementation inventory and proposed worker architecture, against the current model, threading, transition and refactoring contracts. Its implementation inventory is pinned to `6e54a48ab571cb46b813eb1d98a301401089192f`; implementation work remains based on PR 1090 `02bbdb7711f8b85685e63aa18873e25968aeb848`. Older illustrative tables are not an alternative implementation specification.

The earlier proposal supports the chosen direction. The main architecture is already incorporated. Its additional value is a detailed preservation checklist and a clearer separation between transition correctness, backend consistency and eventual progress. Adopt the following refinements through existing work orders, keeping the 32 logical contracts and finding-driven acceptance unchanged.

## Ideas already adopted

| Idea in earlier proposal | Current contract | Assessment |
| --- | --- | --- |
| Worker domain owner → explicit effects → owned result events → GUI snapshot | Threading plan, R01/R08, all migrated owners | Keep. Do not move QML-connected QObject models wholesale to a worker. |
| Independent state dimensions | Model contracts: phase versus security/signatures/eligibility/selection | Keep. Avoid one enum for every combination or a machine for every formatting enum. |
| Narrow dependency versions rather than one wallet generation | Readers, draft/review/document identity, publication streams | Keep. Useful progress must survive unrelated label/tip/presentation changes. |
| GUI serialization cannot protect against RPC | R09 backend guarantees and final action checks | Keep. Actor ordering and token equality are insufficient at a protected backend action. |
| Immutable reviewed terms and structured write outcomes | M04/M05/M09/M10 and M24 | Keep. A successful signature, recorded transaction and accepted mempool transaction are different facts. |
| Shutdown while actor loops and outcome receivers remain alive | R08/R10/R36 and manual schedules | Keep. Stop admission, settle prompts and effects, release handlers/handles, then join. |

## Refinements to make explicit in worker acceptance

### 1. Use a pure reducer as the default domain seam — R01, R19

Use ordinary typed C++ state/events and a decision function equivalent to `reduce(state, event) -> {next_state, effect_intents}`. Core, I/O, clocks, random IDs and Qt publication stay outside that decision; their values/outcomes enter as explicit inputs/events. Effect execution and task accounting remain production infrastructure, exercised through the same injected interfaces in tests.

This is a design default, not a requirement to copy large state values or adopt a new generic FSM framework. Private state mutation may implement the same semantics if it remains deterministic and confined to the domain owner. Make illegal combinations difficult to construct with typed records/variants where useful. Derive capabilities from authoritative state rather than storing another mutable set of booleans.

Manual schedule tests then exercise the production transition logic directly. A Qt state-machine library is optional only if it solves a concrete problem without obscuring guards, ownership or deterministic testing. Its mere use is not a correctness guarantee.

### 2. Audit real event producers and pending-state progress — R08, R09, R42

For each event used by a migrated owner, record the actual producer/call site, producer thread, payload ownership, applicable resource/revision, delivery/coalescing policy and teardown behavior. A table with an event name is not evidence that any real code emits it. For observations not available from Core, explicitly supply a shared notification/version hook or document an eventual-refresh strategy plus final-action validation.

For every pending state, record what it waits for, who owns that obligation, and what happens on rejection, exception, timeout, close and producer loss. Progress claims must state their assumptions: executor fairness, backend completion or a safe interrupt/timeout, and cleanup capacity. A manual test that withholds a noncancelable operation forever cannot prove bounded shutdown. Unknown mutation outcomes remain visible and owned; a watchdog must not turn them into success or safe cancellation.

R42 checks these producer/progress records alongside transition coverage. Existing O01–O36 schedules remain the schedule framework; add concrete rows/selectors as each producer is implemented.

### 3. Treat snapshot coherence as a backend contract — R09, R28

A series of individually locked reads can span different backend revisions. Copying their results into one GUI snapshot does not make them coherent. For fields used together in guards or presentation, specify their consistency group and capture it through a suitable existing combined read, a narrow backend snapshot, or a version/check/retry protocol. Label eventual observations as such. Do not introduce a giant wallet/chain/mempool lock.

Test a mutation between constituent reads and ensure the owner retries, reconciles or publishes explicitly stale data. Retain GUI stream/delta sequencing separately; a publication sequence is not a backend mutation version.

### 4. Preserve sharing authorization separately from captured export bytes — R30, R31

The earlier request-sharing table identifies a missing explicit acceptance detail. The pinned `PaymentRequestCard <.validation/issue-remediation-pr1090-20261109/source/qml/components/PaymentRequestCard.qml:163>` captures request ID and exact payload, checks them again on QR completion, and closes a save dialog when its target changes. Its current file-write path also needs migration to the planned background writer.

An immutable payload alone is insufficient for request sharing: saved/unused status, request revision, original opening, and presentation ownership may have changed. Preserve the distinction from wallet-free PSBT snapshot export. R30 must specify and implement typed sharing capability/reason and token-bound copy/QR/export commands before removing these QML guards. Business validation belongs in the domain owner; clipboard/native presentation and scene capture stay in a narrow UI adapter where the platform requires them. Encoding/file writing run off-GUI. Do not transfer QML objects or scene-owned results to worker code; use an appropriate owned value/image representation.

Required regression rows: payment-use notification during capture/chooser; request edit/delete; wallet switch/unload; hidden owner; delayed old capture; and actual file-write outcome after admission. Recheck permission before admitting the export effect. If a write already executed, account for it truthfully rather than claiming that closing the view erased it. This is an application-observed sharing policy, not a guarantee that no payment can arrive concurrently after the last check. Historically used addresses remain used even if credited totals later decrease.

This refinement is now specified by the [V8 protocol](QT6_ARCHITECTURE_FOUNDATION.md#v8-request-sharing-and-qr-effect-protocol), public sharing APIs and action routing. R30 implements these contracts and transition tests with the consumer; it must not leave the policy in imperative QML.

### 5. Preserve projection semantics outside the main transaction workflows — R28, R36, R38

The earlier inventory distinguishes sync completion from an estimated percentage, transaction status from a monotonic lifecycle, and OS reachability from peer connectivity. Preserve these facts while changing ownership.

The pinned `NodeModel completion logic <.validation/issue-remediation-pr1090-20261109/source/qml/models/nodemodel.cpp:535>` already has a completion latch and catch-up hysteresis. R36 must retain tests that partial snapshots or a rounded estimate cannot announce readiness, ordinary header-first updates do not flicker completion, and resumed catch-up waits for the appropriate complete state. The threshold is existing behavior to characterize, not a new design constant to invent.

R28 must characterize the separate `Transaction` and Activity status predicate precedence before consolidating it, including reorg, orphaned generated transaction, maturity, abandonment and conflict cases. A new reducer must not assume Confirmed is terminal.

R38 and the node consumers must preserve per-reader activation policy: hiding traffic presentation may stop derived rendering while raw history continues; deactivating the log reader may stop watching. View-hidden, reader-inactive, operation-canceled and owner-retired are distinct events. Exercise delayed activation results and reactivation with the manual scheduler/clock. A global “hidden means stop everything” rule is incorrect.

### 6. Preserve presentation lifecycles without moving business state back into QML — R37

Popup Opening/Visible/Closing/Closed, focus, animations and feedback timers remain presentation state. Wallet hiding does not necessarily close an overlay/native dialog automatically; retain explicit presentation cleanup and independent token validation on any late callback. An exit animation must not determine whether a write succeeded or when backend resources may be released.

Use component tests for hide/show while a popup exits, late native acceptance, recreated views with existing prompts/results, and stale deferred navigation. Model-owned operation state survives these transitions. This complements declarative QML; it does not require putting focus or animation state in a backend actor.

## Ideas not to carry over literally

- The older combined catalog machine's Opening/Creating/Migrating states are illustrative. Keep separate catalog/registry/selection and per-operation owners.
- The older Send example compresses checking, authorization and submission. Keep the newer explicit confirmation, SignOnly/ConfirmAndSubmit continuations and reconciliation paths.
- The inventory's “restore prior lock state” description is historical behavior, not the proposed cross-RPC authorization guarantee. Conditional cleanup must preserve later RPC/scheduler intent.
- Old QML flags and field-synchronization handlers describe current implementation, not desired ownership. Preserve behavior while replacing business state with typed bindings/commands.
- Older test paths precede PR 1090's reorganization. Discover actual selectors in R00; do not copy paths blindly or restore superseded detailed Python suites.

## Handoff impact

Keep the current architecture and work-order sequence. Strengthen the named orders with these acceptance obligations rather than adding more global machines or another executor framework. The earlier inventory becomes a preservation checklist during R00 and R41/R42; it is not additional evidence that the historical findings reproduce.

No application code changed and no runtime tests were executed for this review. The benefit must be demonstrated through the existing finding regressions, producer/progress audit and real integration tests as implementation proceeds.
