# Upstream improvement notes for the Qt 6 refactor

Standing user policy `native-qt-defer-20261113`: [document and defer defects in native Qt components](QT6_TOTAL_REFACTOR_PLAN.md#native-qt-component-defects-document-and-defer); keep the component, do not implement our own replacement or change Qt. Record attribution, versions, reproducer, impact, affected unresolved checks and revisit condition. Continue independent application work.

R10 final Qt picker decision (2026-11-13): user explicitly selected “Document and defer; keep the affected TSan result explicitly unresolved.” This supersedes the earlier app-owned chooser implementation direction and pending Qt-patch decision. Keep Qt/Core unchanged and retain the existing picker. Do not implement or integrate a replacement chooser for this dependency defect. Preserve the Qt-only race reproducer and original failing TSan journey, with the affected result explicitly unresolved; this is accepted deferral of a dependency risk, not a sanitizer pass or blanket R10 acceptance. Continue independent application, integration, coverage and platform checks. No evidence currently establishes application wallet-logic failure or application data corruption from this race; absence of that evidence does not establish harmlessness.

The isolated picker proposal has zero model/UI edits, no integration, builds or tests. Retain it as preparation only. All original318 findings/329 scopes and remaining acceptance obligations remain tracked. R10 remains unaccepted pending the independent gates; no R11 work.


R09 conditional acceptance: the user explicitly accepted forward progress at `c90cb60f222b76045d1a75d27497781f239c7f7b` under unchanged Core `44901d8f64cb617714707bd025ecd83f108353c2`. The public-wallet audit and tested balance consumer are accepted as the app-side prerequisite; the original full backend gate remains unsatisfied. `Decision and clear limitations <.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/NOTES.md>`, `five deferred contracts and consumer owners <.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/deferred-contracts.csv>`. Device waits may still hold Core's wallet mutex and block wallet RPC; background execution protects GUI but cannot release that lock. R10 now owns the opening/session lifetime work and its original eight scopes plus O04/O27. R00–R08 are bounded accepted; R09 is conditionally accepted; R10 is active and unaccepted. Preserve all 318 findings, 329 scopes, 43 orders, 36 schedules and existing platform/coverage gates.


The user-directed [app-side wallet adapter plan](QT6_WALLET_ADAPTER_PLAN.md), amendment `wallet-adapter-plan-20261111`, selects app-owned state behind the existing public wallet interface while Core internals/pin remain unchanged. The `follow-up register <.validation/issue-remediation-pr1090-20261109/execution/R09/adapter-plan-20261111/core-improvement-followups.csv>` maps UPSTREAM-001–010 to current adapter treatment, residual guarantees and later Core logic/interface work. It distinguishes existing public solutions, optional simplifications and required stronger contracts still open. No adapter implementation, Core improvement or finding closure is claimed by this planning amendment.

For every similar limitation discovered during any later order, record the app workaround/state and its actual guarantee, the remaining race/blocking/uncertainty, the more appropriate Core logic/interface change, exact evidence, compatibility/tests, owner and workaround retirement condition. Keep UI drafts/workflows and local lifetime in the app. Shared authority needed by RPC or scheduled Core work belongs in the future backend contract. Deferring that work does not make app-local counters authoritative against other clients.

The accepted [base-pr1109-core44901-20261111](QT6_BASE_UPDATE.md) uses stock Core `44901d8f64cb617714707bd025ecd83f108353c2`. Existing notes retain their original revision/evidence. The R09 `Core/RPC audit <.validation/issue-remediation-pr1090-20261109/execution/R09/core44901-audit-20261111/REPORT.md>` adds independent evidence and UPSTREAM-004–008 below. The pin upgrade does not mark any upstream proposal implemented or authorize local subtree fixes.

User decision `wallet-interface-boundary-20261111`: production code stays strictly behind `interfaces::Wallet` and `WalletLoader` lifecycle APIs. The `contract analysis <.validation/issue-remediation-pr1090-20261109/execution/R09/interface-boundary-20261111/CONTRACTS.md>` resolves the audit's architecture question. Internal-access prototypes remain diagnostic tests only; their successful observations do not authorize production use. Missing atomic commands/lifecycle/outcome contracts remain upstream requests, with affected acceptance gates open. No Core edit is authorized.

These local notes retain changes that could simplify the application or remove a backend limitation. The [refactoring plan's Core subtree policy](QT6_TOTAL_REFACTOR_PLAN.md#core-subtree-boundary-and-upstream-follow-ups) is binding. A note does not authorize editing the Bitcoin Core subtree, moving its pin or submitting an upstream issue or patch. Existing consumers' semantics must be considered before recommending an API change.

Add a note whenever a discovered Core interface limitation, ordering hazard or missing guarantee complicates the app solution, including when an app-only workaround succeeds. Use a stable local note ID and record:

- Affected work orders and existing scope IDs, exact app/Core revisions and source locations.
- Confirmed behavior, the required guarantee, assumptions still needing verification, and reproduction/regression evidence.
- The app-only workaround, its lifecycle, performance or maintenance costs, and remaining limitations.
- The upstream change or contract clarification that would simplify the solution, compatibility implications and required upstream tests.
- Whether the app fix can proceed or a required issue is awaiting user input, plus any eventual user decision and upstream reference.

If an issue cannot be solved correctly without subtree changes, notify the user with the evidence and wait for input before continuing the affected fix or dependent work. Keep its acceptance gate open. Continue independent checks. An improvement that merely simplifies an adequate app workaround does not require a pause. These note IDs supplement the plan; they do not renumber or close any of the 318 findings, 329 regression scopes or 36 schedules.

## UPSTREAM-001

**Wallet load notifications and loaded-wallet snapshots**

**Status:** App-only discovery workaround available; upstream improvement remains a local proposal. No Core change has been implemented or authorized, and no user-input hold is required for this discovery workaround. R08's other open gates remain open.

**Affected work:** R08 lifecycle/discovery foundation; R09 backend-interface audit; R10/R11 lifetime and catalog consumers. This is an infrastructure contract note, with no new finding or regression-scope closure.

**Revisions:** Bitcoin Core `dc282ff31d1cc97507530a541d9cec8a8f6a6ef4`. App candidate `485620b1452100525083571060ad7ded9a98df2e` demonstrates the artificial inventory-limit failure; isolated app candidate `1730bc280509f8e5eb3d3b4fac74bb78451a7d74` preserves the announced wallet without recapture. These are candidate evidence, not an R08 acceptance claim.

**Observed contract:** In pinned Core, ``NotifyWalletLoaded` <.validation/issue-remediation-pr1090-20261109/source/bitcoin/src/wallet/wallet.cpp>` at line 231 invokes callbacks while holding `wallets_mutex`. The load path at line 301 and create path at line 444 notify before calling `AddWallet`, with a lock-release interval between the calls. ``GetWallets` <.validation/issue-remediation-pr1090-20261109/source/bitcoin/src/wallet/wallet.cpp>` at line 203 copies the currently registered wallets under that mutex. A concurrent getter can therefore omit a wallet whose load callback has already returned. This is a confirmed ordering property; it alone does not establish a Core bug or an atomic cross-call guarantee.

**App workaround:** Subscribe before the startup getter; seed the app-owned list with startup handles; merge and deduplicate handles transferred by notifications. Retain a notified handle through the pre-`AddWallet` interval. Run capture, metadata work and resource retirement off GUI, with bounded presentation batches. Treat retained wallet inventory separately from bounded transient task/event queues. Do not discard a handle and assume a subsequent getter can reconstruct it. Unload/reopen identity, burst handling and close/drain retain their own obligations.

**Cost:** The app must own and merge startup/notification discoveries, retain resource lifetimes through presentation, handle duplicate discovery, and distinguish inventory size from queue pressure. The current interfaces do not supply a documented atomic versioned snapshot/subscription protocol; the workaround does not claim one.

**Upstream improvement to consider:** Document callback ordering relative to catalog membership, callback threads/locks and handle ownership. Consider a separate committed-membership change subscription with a defined registration/snapshot protocol; if needed for reliable resynchronization, associate snapshots and changes with an in-process membership revision. Preserve existing load callbacks' behavior and audit current consumers before introducing a new contract. The desired simplification is reliable coalesced membership refresh through a declared backend guarantee. This is an upstream design proposal, not a selected or authorized subtree implementation.

**Evidence:** `Producer audit and decision history <.validation/issue-remediation-pr1090-20261109/execution/R08/BACKEND_CONTRACT_DECISION.md>`; `stronger app regression failing against candidate5 <.validation/issue-remediation-pr1090-20261109/execution/R08/handle-inventory-red.json>`; `app-only candidate and source hashes <.validation/issue-remediation-pr1090-20261109/execution/R08/candidate6-source.json>`; `172 focused after-cases <.validation/issue-remediation-pr1090-20261109/execution/R08/handle-inventory-work/focused-counts.json>`; `regression requirement mapping <.validation/issue-remediation-pr1090-20261109/execution/R08/handle-inventory-work/REGRESSION_MAPPING.md>`. The producer audit is source evidence and the ordering regression is app-level evidence; neither is being presented as an independently executed upstream Core regression.

**Tests for an upstream proposal:** Registration/snapshot overlap; notification before and after membership changes; all actual load/create/startup/restore/migration producers; concurrent add/remove and independent RPC writers; duplicate or out-of-order delivery under the declared contract; callback reentrancy, disconnect and unload lifetime; and backward compatibility for existing consumers. Verify that a claimed resynchronization protocol cannot lose a required change. Keep this validation separate from the app workaround's test results.

**Next action:** Keep the local note current as the app solution evolves. Any required subtree change must first be reported to the user and await their input. External submission requires separate authorization.

## UPSTREAM-002

**Wallet-specific callback entry and disconnection synchronization**

**Status:** Existing pinned implementation supplies the needed synchronization; app-only work can proceed. This is a contract-documentation opportunity, not an established Core defect or a request to change its implementation. No user-input hold or external submission is pending.

**Affected work and revisions:** R08 handler ingress/physical drain; R09 backend contract audit; later subscriptions in R10/R11/R36. App candidate `e701902ad5baebd82176565db2f4d50f21f7639c`; Core `dc282ff31d1cc97507530a541d9cec8a8f6a6ef4`. No finding or regression scope is closed by this note.

**Verified behavior:** Wallet `HandleLoadWallet` inserts its callback under `WalletContext::wallets_mutex` and returns a cleanup handler whose disconnect acquires that same mutex before erasing it. `NotifyWalletLoaded` holds it across callback invocation and argument destruction. This specific disconnect therefore waits for an executing wallet-load callback. The generic `interfaces::Handler` comment says disconnect prevents future notifications, without documenting a universal join guarantee. The wallet path does not use the separate generic `btcsignals` wrapper; its behavior must be audited from its actual producer.

**App solution and cost:** Execute registration, capture and disconnect off GUI; never hold the app mailbox mutex across backend disconnect; retain the callback owner and acquired leases through handle destruction and keep Core context alive through physical drain. Preserve the distinction between this verified producer and alternative handlers. The cost is producer-specific source auditing and synchronization tests in addition to the generic lifecycle contract tests.

**Upstream simplification:** Document wallet-load callback threads, held locks, disconnect completion guarantees, argument lifetime and prohibited reentrancy at the interface boundary. Preserve existing semantics and audit consumers before changing them. Upstream contract tests should hold a callback while another thread disconnects, verify disconnection cannot complete early and cannot admit a later callback, and cover inline registration plus startup/load/create producers.

**Evidence:** `Pinned source hashes and audit <.validation/issue-remediation-pr1090-20261109/execution/R08/wallet-notification-disconnect-audit/AUDIT.md>`. This source audit is not being reported as a separately executed upstream Core regression. The app's independent callback/last-reference tests and full Core/RPC lifecycle suites retain their own evidence and limitations.

## UPSTREAM-003

**Successful migration with no primary wallet handle**

**Status:** The app already guards a missing primary handle; an app regression covers the permitted result. Interface clarification could simplify outcome modeling. No Core change or user-input hold is required for that guard. The full empty-wallet migration journey remains for the migration owner; no independent upstream process reproduction is claimed by this source audit.

**Affected work and revisions:** R08 shared-handle compatibility and outcome ownership; R09 backend outcome audit; R14 migration behavior. App production candidate `ae0fbb1393888185349118394c28c5b2604abdb9`; Core `dc282ff31d1cc97507530a541d9cec8a8f6a6ef4`. This is a contract note with no finding or regression-scope closure.

**Source-permitted behavior:** `MigrateLegacyToDescriptor` at `wallet/wallet.cpp:4421` removes an empty local wallet. The later loop at 4440–4461 sets the result's primary wallet only if a remaining migrated wallet is loaded. The public interface calls it with the default `load_wallet=true`, then converts that potentially absent primary wallet with `MakeWallet`, which returns null for a null source. Thus a success result alone does not establish a non-null primary handle. Preserve the distinction between permitted source behavior and an independently executed empty-wallet Core reproduction.

**App treatment and cost:** Complete the existing successful operation without fabricating or dereferencing a wallet model when the returned handle is absent. Preserve the initialized catalog and explicit busy/error state. A local test exercises this nullable success alongside the separate success-with-wallet test. Later migration UX must qualify the actual empty-wallet journey and its result presentation.

**Upstream simplification:** Document whether each successful migration outcome loads a primary wallet, removes an empty wallet, or leaves no loaded primary handle. Consider an explicit additive outcome discriminator if it would help clients, preserving current empty-wallet semantics and existing consumers. Upstream tests should distinguish empty, spendable, watch-only and solvable migrations, load failure/rollback, and loaded versus non-loaded results.

**Evidence:** `Source audit and acceptance requirement <.validation/issue-remediation-pr1090-20261109/execution/R08/null-migration-review/REVIEW.md>`; app selector `WalletQmlControllerTests::successfulEmptyMigrationDoesNotFabricateAWallet`. The selector's executed result belongs to the eventual candidate qualification, not to this note itself.

## Re-audit after the authorized Core44901 base update

The `base-pr1109-core44901-20261111` source audit checks stock Core
`44901d8f64cb617714707bd025ecd83f108353c2`. Original notes/evidence above remain
historical; `the new source hashes and call sites <.validation/issue-remediation-pr1090-20261109/execution/base-update-pr1109-20261111/upstream-reaudit.json>`
record the refreshed contracts. No subtree source was modified.

UPSTREAM-001 remains applicable: wallet.cpp305/393 and wallet/load.cpp157
notify before AddWallet; the startup getter at wallet.cpp206 only copies
committed membership. Keep announced handles and one startup getter.
UPSTREAM-002 remains applicable: callback registration/removal at
wallet.cpp228–232 and invocation at235–240 share wallets_mutex, so this wallet
subscription's disconnect still synchronizes with active callbacks. Keep
retirement off GUI and outside the application mutex.

UPSTREAM-003 remains source-permitted: wallet.cpp4343–4348 removes an empty
local wallet, and4350–4380 only populates the primary interface when a remaining
wallet is loaded. The new interface takes an explicit load_wallet argument;
our caller retains true. A successful null result remains distinct from a
failed migration. This re-audit is source evidence, not a claim of a new
independent empty-wallet reproduction or completion of the later migration
owner's regression journey.

The richer passphrase errors and explicit transaction metadata in this Core
revision require outer-app API adaptations. They do not, by themselves,
establish shared authorization leases, conditional-write or unload-admission
guarantees. R09 continues auditing those guarantees at the new pin.

### UPSTREAM-001/002: optional opening identity and observed callback ordering

The R10 O04 composed-source batch adds application-owned original-adoption tokens, transient budgeted close escrow, and a shared saturating event clock. Each public load callback is stamped at ingress before metadata; each original Session stamps its public unload callback before GUI publication. A load observed after that unload may adopt a fresh per-entry origin while delayed original command results remain tied to their original facade/Session. Origin lifetime/pruning order is distinct from the Native callback event order. These are application observations, not a Core backend identity or an atomic catalogue guarantee.

An optional public per-opening identity, command/load correlation and documented load/unload registration ordering would simplify this adapter. In particular, an unload occurring before the application registers that opening's unload handler is not observed by the app clock; the adapter does not claim to recover that missing event or infer identity from a reused name. Such stronger guarantees remain upstream design considerations. No Core subtree changes, private wallet access, new exception or additional user decision is required for the selected app-side solution.

`Batch17 source design and exact composed selectors <.validation/issue-remediation-pr1090-20261109/execution/gui-threading-implementation-20261112/O04_SOURCE_BATCH17.md>` record both old-result/notification orders, unknown-name late entries, fresh same-name loads after observed unload, independent selections, prompt identity and physical retirement obligations. This note records implementation intent and public-interface limitations; it does not claim that the newly written regressions have run or that O04/R10 is accepted.

## UPSTREAM-004

**Current acceptance status:** Explicitly deferred by the user's R09 conditional acceptance; The current app may implement its own local workflow and observations; the stronger RPC-shared Core guarantee remains open. `Decision and limits <.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/NOTES.md>`, `consumer owners and closure tests <.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/deferred-contracts.csv>`. No upstream fix or Core edit is claimed.

**Scoped authorization coordinated with RPC and scheduled relocking**

**Status/revisions:** Public-interface gap reproduced at app `e89249aa7b3074fa2a33bd02d2cccacf8e993aa6`, stock Core `44901d8f64cb617714707bd025ecd83f108353c2`. A narrow in-process lock/deadline prototype succeeds. The user rejected production access to Core internals and selected the strict wallet interface boundary. A complete shared lease/conditional-release interface contract is not implemented or accepted.

**Observed behavior:** The production `WalletRelockGuard` (`qml/models/walletunlock.cpp:38–45`) unconditionally calls `interfaces::Wallet::lock()`. In a controlled test, a later independent HTTP `walletpassphrase` unlock succeeds and can sign, then the older guard locks the wallet and signing fails with -13. Core's RPC (`wallet/rpc/encrypt.cpp:44–109`) coordinates scheduled deadlines with `nRelockTime`; `CWallet::Lock` (`wallet/wallet.cpp:3231–3245`) does not itself own or compare an app lease. An app-local saved `was_locked` boolean cannot preserve a later RPC unlock.

**App route/cost:** A prototype takes Core's actual `m_relock_mutex` then `cs_wallet` and preserves a live RPC deadline. This uses the internal wallet accessor, not a public lease API. It establishes only the tested later-RPC case. A production owner still needs exact action/opening identity, overlapping app lease accounting, scheduled and explicit relock/lock/passphrase-change behavior, callback lifetime and close/outcome ownership. Do not infer the complete protocol from the prototype.

**Upstream simplification:** An additive scoped authorization/conditional release API, explicitly coordinated with RPC and scheduler epochs, would avoid pin-specific lock/deadline coupling. Specify whether a lease permits admission or keeps key material alive, how later authorization supersedes cleanup, and how explicit locks revoke operations. Preserve existing RPC timeout semantics.

**Owners/tests:** R09/M03, R14 and protected consumers. Existing draft-139-RC46/draft-153-RC46 consumer obligations are not closed by this mechanism-level audit. Require independent RPC unlock/lock/timeout/passphrase-change schedules, overlapping app leases, stale cleanup, unload, device waits and restart limits. `Normal evidence <.validation/issue-remediation-pr1090-20261109/execution/R09/core44901-audit-20261111/observations.json>`, `sanitized evidence <.validation/issue-remediation-pr1090-20261109/execution/R09/core44901-audit-20261111/asan/observations.json>`.

## UPSTREAM-005

**Current acceptance status:** Explicitly deferred by the user's R09 conditional acceptance; The current app may implement its own local workflow and observations; the stronger RPC-shared Core guarantee remains open. `Decision and limits <.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/NOTES.md>`, `consumer owners and closure tests <.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/deferred-contracts.csv>`. No upstream fix or Core edit is claimed.

**Conditional wallet metadata and request persistence**

**Status/revisions:** At the same R09 app/Core revisions, separate `getAddress` and `setAddressBook` calls permit a newer independent RPC label to be overwritten. Holding the existing in-process `cs_wallet` across a value comparison and write rejects the tested conflict and succeeds for an unchanged value. No production change or general versioned persistence guarantee is claimed.

**Source/route/cost:** `wallet/interfaces.cpp:189–214` exposes separate address operations; `wallet/wallet.cpp:2336–2340` performs the write. The test-only adapter accesses `CWallet::cs_wallet` through the internal accessor and uses the actual existing write implementation. This diagnostic route avoids Core source edits but couples the adapter to lock order and internal representation; the user has excluded it from production. A supported conditional interface command is required for that guarantee. A value comparison does not establish a mutation generation, cross-record atomicity, conditional historical-use/bind protection or settings-file coordination.

**Upstream simplification:** Add narrow conditional commands for declared consistency groups, with an expected base/version, conflict/not-found/known-failure outcomes and explicit transaction boundaries. A request command should specify its use/bind predicates and persistent record scope. Keep labels and public invoice fields separate; do not introduce one undocumented global revision.

**Owners/tests:** R09/M12, R14/R30/R35/R36 and their persistent editors. Require independent RPC writes between capture and save, unchanged/success/conflict/deleted bases, unrelated edits, successor edits during save, actual database failures, use/bind races and restart reconciliation. The R09 label prototype is not acceptance of those later scopes. Evidence is in the linked R09 report and both observation files.

## UPSTREAM-006

**Current acceptance status:** Explicitly deferred by the user's R09 conditional acceptance; The current app may implement its own local workflow and observations; the stronger RPC-shared Core guarantee remains open. `Decision and limits <.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/NOTES.md>`, `consumer owners and closure tests <.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/deferred-contracts.csv>`. No upstream fix or Core edit is claimed.

**Early wallet closing and protected mutation admission**

**Status/revisions:** A real Core unload interval is reproduced at the same R09 app/Core revisions. The current public interface supplies no declared shared closing/admission token. No complete app-only solution is established, and no proof that every possible app-only implementation is impossible is claimed. R09 and dependent acceptance remain open; no Core edit is authorized.

**Observed behavior:** `RemoveWallet` (`wallet/wallet.cpp:175–197`) writes wallet state, disconnects chain notifications, removes catalog membership and then emits `NotifyUnload`. A test-owned wrapper delegates the real handler disconnect and holds the following interval: the wallet is still listed, no unload notice has fired, and an interface metadata write succeeds. After the notice and catalog removal, a retained interface handle can still write; reload verifies persistence. Core's lifetime retention intentionally permits access until handles are released. Neither the late notice nor membership alone is the required admission fence.

**App options/cost:** App opening tokens and ingress flags still fence observed app closure and preserve admitted outcomes. They do not establish an earlier backend closing boundary. Core's public mutable chain-handler field is not a synchronized closing flag; the test wrapper is not a production proposal. RPC unload's rescan reservation is not a declared general mutation-admission protocol and must not silently be repurposed. A new in-process adapter boundary still needs an independently tested closing protocol. If the required guarantee cannot be supplied correctly through existing interfaces, notify the user and wait for input before any affected fix or dependent work.

**Upstream simplification:** A narrow operation-admission/retirement contract, shared by unload and protected operations, with a documented closing linearization point, owning lease and drain/rejection outcomes. Notify early enough to close app admission, but retain already admitted effects and cleanup; avoid locks across UI/device waits. Audit startup, RPC unload, migration and shutdown producers before changing notification ordering.

**Owners/tests:** R09, R10/R11 and protected consumers. Require unload before/after admission, the demonstrated pre-notification interval, existing admitted writes/outcomes, failed unload/rescan, independent RPC writers, handle release and reopen identity. `Audit method and limits <.validation/issue-remediation-pr1090-20261109/execution/R09/core44901-audit-20261111/REPORT.md>`.

## UPSTREAM-007

**Current acceptance status:** Explicitly deferred by the user's R09 conditional acceptance; The current app may implement its own local workflow and observations; the stronger RPC-shared Core guarantee remains open. `Decision and limits <.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/NOTES.md>`, `consumer owners and closure tests <.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/deferred-contracts.csv>`. No upstream fix or Core edit is claimed.

**Structured wallet recording and transaction broadcast outcomes**

**Status/revisions:** At the same R09 app/Core revisions, an independent RPC spends a prepared candidate's input. `interfaces::Node::broadcastTransaction` returns a rejection and leaves that candidate uncommitted. Calling the wallet's void `commitTransaction` afterwards returns normally and records it despite rejected broadcast. No claim is made that Core promises void commit means broadcast success.

**Source/route/cost:** `wallet/interfaces.cpp:280–284`, `wallet/wallet.cpp:2138–2182` and `node/interfaces.cpp:375–378`. A test-only route uses actual node admission results and separates wallet recording; a final input-policy check under the existing wallet mutex rejects a later RPC coin lock, then submits the exact candidate after unlock. RPC independently verifies the actual input, transaction ID, recipient and amount. This requires an app adapter to own separate validation, admission and persistence outcomes. An accepted/already-known transaction is not proof of a newly performed network effect.

**Upstream simplification:** Add structured recording/submission results without promising global database/mempool/network atomicity. State whether the wallet record exists, admission was accepted/already known/rejected, broadcast was requested and a known error occurred. Preserve fee limits and replacement/input policy. Return enough authority to distinguish retryable pre-effect rejection from partial or unknown outcomes.

**Owners/tests:** R09/M04, R19–R22 and imported/replacement attempt consumers. Test independent input spends/locks, replacement changes, exact reviewed terms, fee policy, persistence failure after admission, duplicate submission, notification/result order, owner loss and restart query-only reconciliation. The prototype does not implement a production attempt owner or close any finding.

## UPSTREAM-008

**Current acceptance status:** Explicitly deferred by the user's R09 conditional acceptance; For device work, current wallet calls may keep cs_wallet held during signer/user waits. Worker execution protects GUI, while wallet RPC can remain blocked. Capture/execute/guarded-apply needs a future public Core contract. `Decision and limits <.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/NOTES.md>`, `consumer owners and closure tests <.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/deferred-contracts.csv>`. No upstream fix or Core edit is claimed.

**Capture signer data separately from device/process waits**

**Status/revisions:** At the same R09 app/Core revisions, `WalletImpl::displayAddress` (`wallet/interfaces.cpp:247–250`) holds `cs_wallet` through `CWallet::DisplayAddress` (`wallet/wallet.cpp:2536–2554`) and signer enumeration/device I/O. While a real stock signer response is held, an independent HTTP wallet query enters but cannot complete. The PSBT path likewise holds the wallet lock in `CWallet::FillPSBT` (`wallet/wallet.cpp:2009–2045`) through external signer work.

**App route/cost:** A prototype captures the public descriptor, invokes the existing `ExternalSigner::DisplayAddress` on a separate worker without the wallet lock, then verifies the echoed address. While its response is held, actual wallet RPC and GUI handoff both progress. This establishes diagnostic split feasibility. Under the strict wallet boundary, production needs an appropriate capture/execute/apply interface contract rather than a direct signer-library bypass. Full signing still requires exact wallet/opening/device/configuration/PSBT provenance, protected capture, response verification, final validation and process ownership. No cancellation or universal shutdown deadline is supplied by the prototype.

**Upstream simplification:** Separate immutable signer request capture from process execution and result application, with documented locks, device identity and cancellation/retention semantics. Keep the existing synchronous API compatible; a future narrow API should avoid requiring clients to reconstruct hidden capture rules.

**Owners/tests:** R09/M05, R15 and R23. Require independent wallet RPC during held enumeration/signing/display, close/late response, device/configuration changes, malformed/substituted results, process failure/hang, bounded execution, cleanup and physical affinity. The existing R02 exact-signer regressions remain required. The audit establishes current lock ownership and a narrow split feasibility result, not the complete feature migration.

## UPSTREAM-009

**Exact public fee and maximum-send preparation**

**Status/revisions:** Public-equivalence audit/planned migration only at app `e89249aa7b3074fa2a33bd02d2cccacf8e993aa6`, Core `44901d8f64cb617714707bd025ecd83f108353c2`. No replacement or new Core requirement is claimed proven by this note. R09/R17/R19 own the interface audit and consumer migration.

**Current evidence:** The `six-site legacy access inventory <.validation/issue-remediation-pr1090-20261109/execution/R09/interface-boundary-20261111/legacy-internal-access.json>` identifies `MinimumCustomFeeRatePerKvB` at walletqmlmodel.cpp175, sweep detection265, `CreateSendAllTransaction`288 and insufficient-selected-input estimation366. They access the internal wallet, coin availability and fee/transaction rules. `interfaces::Wallet::createTransaction` supports ordinary preparation, but equivalence for exact remaining-amount intent and richer unsuccessful-preview output must be checked rather than assumed.

**App adapter treatment:** Own immutable fixed/maximum intent, selected input policy, reader revisions and observed results; run preparation/read work on the background port and publish truthful unavailable/unknown/error data. Migrate using existing public methods only where they preserve the current behavior and regressions. Do not copy implementation algorithms into the adapter, approximate amounts/fees or invoke a signing/broadcasting RPC while previewing an edit. Preserve existing functionality while the actual gap is assigned.

**Later Core improvement:** If no existing equivalent public route exists, expose a narrow read-only preparation/preview request with explicit fixed/remaining amount and input/fee policy, plus structured estimated/insufficient-input/failure facts. Keep the calculation next to Core's own transaction rules so GUI and RPC behavior cannot drift. Avoid introducing a duplicate formula or an ambiguous global wallet status flag.

**Required tests/removal condition:** Exact send-all/input sets, fee/rate caps and fallback policy, insufficient selected inputs, invalid/spent/foreign inputs, version/TRUC conditions, independent Core arithmetic/decoded transaction oracles and absence of signer/broadcast effects during preview. Retire legacy implementation access only after an existing public equivalent or the new public contract passes these regressions. This creates no new finding/scope ID or acceptance.

## UPSTREAM-010

Qt Widgets source cross-check at Core `44901d8f64cb617714707bd025ecd83f108353c2`: the `reviewed creation/status/send/PSBT paths <.validation/issue-remediation-pr1090-20261109/execution/R10/qt-reference-20261111/REPORT.md>` use public capability facts and transaction-specific completion, without requiring exact Single-key/Multi-key wallet classification. Preserve external-signer precedence and existing multisig PSBT checks. Our generic software-wallet label remains app policy, and our reads still run on background executors with cached GUI properties. This is source evidence only; no Widgets runtime pass is claimed.

**Current R10 scope decision:** R10 supported-capability decision: Multi-key wallet workflows are outside current GUI support, as clarified by the user. `Constrained decision <.validation/issue-remediation-pr1090-20261109/execution/R10/supported-capabilities-20261111/DECISION.md>` scopes opening metadata to public Watch-only/external-signer facts and a generic software-wallet category, without asserting Single-key policy or transaction signing ability. Exact multi-key metadata is deferred to future wallet-feature work; it no longer blocks R10. Continue the authorized session/lifetime migration behind public interfaces. Preserve existing PSBT multisig behavior and tests, supported descriptor-import obligations, all original R10 scopes and full acceptance gates. No Core edit or internal classifier exception is authorized. R10 remains unaccepted; later orders remain unstarted pending normal dependencies.

**Retained historical evidence:** The `real Core counterexample <.validation/issue-remediation-pr1090-20261109/execution/R10/capability-api-audit-20261111/results.json>` proves the audited existing public capability values do not distinguish the constructed Single-key/Multi-key pair. The `earlier strict-boundary choice <.validation/issue-remediation-pr1090-20261109/execution/R10/capability-api-audit-20261111/USER_DECISION.md>` and `qualified drain component <.validation/issue-remediation-pr1090-20261109/execution/R10/drain-fix-20261111/REPORT.md>` remain historical evidence. The product-scope clarification supersedes the requirement for exact wallet-level multi-key metadata in current R10; it does not supply the missing API or authorize an implementation bypass.

**Typed wallet capabilities and supported descriptor import**

**Status/revisions:** Existing-interface migration and capability audit planned at the same app/Core revisions. This is not a request to add a duplicate import API. R09/R10/R12/R15 retain consumer ownership.

**Current evidence:** `WalletQmlModel::keySchemeForWallet` at walletqmlmodel.cpp1117 reads internal wallet flags and active descriptor managers; the watch-only creation continuation at walletqmlcontroller.cpp629 imports through raw `CWallet`. The public interface already supplies `hasExternalSigner`, `privateKeysDisabled` and `importDescriptors` (`interfaces/wallet.h:215–216` for import). These must be used/audited before proposing Core extensions.

**App adapter treatment:** Cache public capability observations, preserve per-opening identity and typed unknown/unavailable outcomes, and migrate imports using public request/result types with full per-item/partial handling. Keep feature policy in the app. No direct ScriptPubKeyManager or wallet flag inspection is permitted as the production workaround.

**Later Core improvement:** Add only capability/descriptor information that is actually absent, such as a supported typed multi-key classification if existing public data cannot represent it correctly. Document capability/read consistency and existing import outcomes. Preserve supported watch-only/external-signer behavior and existing descriptor/persistence formats. Exact multi-key policy presentation becomes required with a future wallet-level multisig feature; current software-wallet presentation must not assert an unobserved Single-key policy.

**Required tests/removal condition:** Real wallets of each supported kind, classification during load/reopen, import failure/partial results, actual persisted descriptors and reload/backward compatibility, with cached GUI bindings. Retire legacy access only after qualified public replacements preserve supported behavior and test the intentional generic software-wallet presentation. Public descriptor import, per-item outcomes and persistence checks remain required independently of the deferred multi-key metadata. No finding or scope is closed by recording this note.

## Implemented first balance consumer — R09

The current `WalletRead` uses the existing public `tryGetBalances` consistency group; cached trusted-balance display needs no new Core API. App-owned coalescing, known/error state and background publication are implemented and tested in the linked consumer report. The observed wallet block hash does not version all wallet/RPC changes. Local close/weak-handle observation does not implement earlier Core closing or shared admission. UPSTREAM-004–008 retain their exact required contracts; UPSTREAM-001/002 retain callback/lifecycle documentation improvements. A later callback or snapshot API may simplify app refresh only if its actual ordering/ownership contract is declared; no new guarantee or subtree change is inferred. Six existing testing-accessor sites remain inventoried migration debt under the strict-boundary audit and their owning orders.

## User-accepted condition for continued app work

The explicit `r09-conditional-acceptance-20261111 <.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/NOTES.md>` permits supported app-only orders to proceed with UPSTREAM-004–008 retained as deferred backend contracts. Their original source evidence and upstream proposals remain unchanged. In particular, device calls through the current interface may hold `cs_wallet` during external signer/user waits and block independent wallet RPC; our worker only protects GUI responsiveness. Future Core work should provide protected immutable capture, device execution outside the wallet mutex, then guarded result validation/application. Test GUI progress and wallet RPC progress separately, retain running effects through physical completion, and keep cancellation limits explicit. Do not bypass `interfaces::Wallet`. The `deferred register <.validation/issue-remediation-pr1090-20261109/execution/R09/conditional-acceptance-20261111/deferred-contracts.csv>` records exact owning consumers and closure tests. No upstream fix or original finding closure is claimed.

## R10 public settings API audit

The `R10 settings adapter audit <.validation/issue-remediation-pr1090-20261109/execution/R10/session-implementation-20261111/SETTINGS_ADAPTER.md>` identifies an existing public solution: inject `interfaces::Chain::updateRwSetting` for an atomic wallet-specific preference update and a real disk-write result. `Node::updateRwSetting` is void and readback alone cannot prove persistence. The adapter preserves a failed persistence outcome when Core keeps the new in-memory value, including `-nosettings`; manual and actual Core integration evidence is linked in that audit. No Core edit is needed. A future optional Node/Chain API convergence could simplify composition, preserving memory/disk outcomes. This does not close UPSTREAM-005's separate wallet-database contract or accept R10.

## UPSTREAM-011

**Incremental public wallet-history observations**

**Status/revisions:** Optional public-interface improvement, recorded during R10 at application HEAD `715fb5449366a9a267e6d111169f226b429947ef` with the dirty candidate identified in the `receipt audit <.validation/issue-remediation-pr1090-20261109/execution/R10/receipts-20261111/UPSTREAM_BULK_READ_AUDIT.json>`, Core `44901d8f64cb617714707bd025ecd83f108353c2`. Current app-only extraction can proceed. No Core edit, stronger guarantee, new acceptance waiver or finding closure is authorized.

**Confirmed source contract:** `WalletImpl::getWalletTxs` at `wallet/interfaces.cpp:332` holds `cs_wallet` while constructing a set for the complete wallet history. The public interface exposes no cursor, page size or cancellation parameter. `getWalletTxDetails` at360 captures transaction/status/mempool facts under that call's wallet lock; a sequence of those calls is not a versioned wallet-wide snapshot. The transaction callback at528 forwards an ID and change kind. A receiver may lack the sender's replacement marker on the original transaction. The audit retains exact source hashes; this lock/capture description is source evidence, not a measured Core latency or an independently executed upstream race reproduction.

**App treatment and costs:** The receipt owner performs the initial public bulk capture on Core, then yields between batches of128 status reads and Domain reductions. It bounds/coalesces256 notices and rechecks all prior unconfirmed receipts once per notification batch: matching destinations alone misses a replaced original when output addresses change. It withholds new snapshots until those rechecks finish, preserving the last reconciled amount and permanent observed-address locks. `Follow-up source/test audit <.validation/issue-remediation-pr1090-20261109/execution/R10/receipt-publication-20261112/UPSTREAM_AUDIT.json>` records both controlled counterexamples. This conservative read cost could be reduced by documented replacement/removal notifications carrying affected old transaction IDs. Immutable publications retain the original opening. Controlled tests prove same-opening peer progress between batches, close cancelling the remainder, and rejection/rebuild without double-counting. Real-thread/TSan and existing actual Core replacement/confirmation regressions provide separate evidence. The bulk capture and final snapshot still scale with inventory; this does not establish constant allocation, hard cancellation, wallet-RPC progress during the bulk call or atomicity against independent RPC writers. GUI request persistence/projection and other discovered model work remain required R10 migrations.

**Upstream simplification and compatibility:** Consider an additive read-only cursor/batch API with documented capture/revision/subscription ordering, coherent per-item status facts and cancellation/retention semantics. Keep wallet selection/status rules in Core and preserve the existing synchronous API. Do not silently truncate inventory or treat an app counter as Core authority. Require concurrent additions/deletions, replacement without an original notification, conflicts/reorgs, unload, slow consumers, bounded pages and snapshot retirement tests. R10 and R28–R32 retain their existing scope/resource obligations. If an affected acceptance requirement cannot be met through the existing interface, notify the user and wait before any dependent fix; this optional note does not grant conditional acceptance.

**R10 app adapter update (indexed observations/label batches):** At app HEAD `715fb5449366a9a267e6d111169f226b429947ef`, clean Core `44901d8f64cb617714707bd025ecd83f108353c2`, the `source-v7 component report <.validation/issue-remediation-pr1090-20261109/execution/R10/incremental-labels-20261112/REPORT.md>` documents borrowed complete receipt views,128-unit indexed reduction and32-write/256KiB label preparation. Native historical-use reads may precede the app receipt view; a sparse app repair marker keeps affected rows/presentation pending until the already-known fact is reconciled. Controlled complete/older-empty-view tests verify that workaround without Core private state. The new actual Core case verifies65 changed records plus20 unrelated records and physical off-GUI encoding/preparation; it does not establish independent-RPC atomicity. Reader's final snapshot copying and payload retirement remain app work, while the existing bulk capture remains this upstream API limitation.

The streamed label command writes the address book once and then request records in separate batches. UPSTREAM-005's expected-version/consistency-group interface would make cross-writer conflict handling clearer; app serialization does not make those effects atomic with independent RPC writers. No additional cross-RPC counterexample was run in this packet, and no new Core defect or guarantee is claimed. Keep existing synchronous callers compatible if an additive cursor/version/effect-receipt contract is later added. Current local migrations remain possible, so this update creates no required Core blocker or new scope/acceptance. Notify and wait before any dependent fix that genuinely requires a subtree change.


### R10 outgoing-note adapter update (2026-11-12)

`Current implementation/evidence <.validation/issue-remediation-pr1090-20261109/execution/R10/outgoing-notes-20261112/REPORT.md>` moves outgoing-note address-book persistence off GUI through the same ordered public Wallet owner as receive commands. It preserves the original opening/transaction/map, does not modify invoice records and distinguishes committed transaction success from failed or uncertain notes. Existing addresses pass nullopt for purpose; a missing address requests SEND. A real commit/mempool/public-interface case verifies receiving purpose and byte-identical invoice data.

UPSTREAM-005 still applies: independent RPC can edit between separate getAddress/setAddressBook calls; a false or exceptional write is not a rollback guarantee, and confirmed prefix counts do not establish cross-record atomicity. A public conditional label-only command with explicit persistence/uncertainty and purpose-preservation semantics would simplify this adapter. No new cross-RPC failure reproduction, Core guarantee, API exception or acceptance is claimed by this component. It needs no Core change; actual Send execution and other app-side work remain possible. Preserve the existing R09 conditional limits and notify/wait before any genuinely Core-dependent fix.


### R10 Native status-contention adapter update (2026-11-13)

The in-progress app batch at base `715fb5449366a9a267e6d111169f226b429947ef`, stock Core `44901d8f64cb617714707bd025ecd83f108353c2`, uses the public blocking `getWalletTxDetails` only when the cheap `tryGetTxStatus` fails. Core wallet/interfaces.cpp360-377 takes its wallet lock and returns the transaction/status/current-height facts together; an empty transaction identifies a missing target. On Native this can wait for contention to clear without a GUI wait or the old one-second retry delay. The app preserves the cached projection while the original read is held; actual missing targets remain explicitly unknown/retryable. No full-history getter, private wallet object or subtree change is introduced. Source/tests are being implemented; no new-candidate pass is claimed.

An optional coherent blocking status-only public method, or a documented readiness notification, could avoid constructing transaction/message/payment-request metadata for this fallback. Such an addition would simplify the app but is not required for the current public-interface workaround. A blocking public call remains uncancellable inside Core; app admission, bounded queued demand, stale-publication fencing and retirement account for its original read rather than pretending to interrupt it. This note creates no new conditional acceptance or Core-change permission. See the current GUI-threading evidence for controlled fallback/GUI-progress tests when qualified.


## UPSTREAM-012

**Structured wallet commit and submission receipt**

**Status/revisions:** R10 app715fb5449366a9a267e6d111169f226b429947ef with clean Core44901d8f64cb617714707bd025ecd83f108353c2. The new app Send owner retains a typed committed/uncertain result, independently of GUI delivery; `current public-interface workaround/evidence <.validation/issue-remediation-pr1090-20261109/execution/R10/send-submission-20261112/REPORT.md>`. This is an optional upstream simplification, not a required Core change, approval request or acceptance.

**Source/actual distinction:** Public interfaces::Wallet::commitTransaction returns void (interfaces/wallet.h); wallet/interfaces.cpp delegates to CWallet::CommitTransaction. At this pin wallet/wallet.cpp:2138 commits to wallet storage, returns normally when fBroadcastTransactions is false, and logs a failed SubmitTxMemoryPoolAndRelay without returning that failure to its caller. Consequently a normally returning commit alone cannot support the completion page's unconditional broadcast claim. The current actual Core/QML cases verify accepted, walletbroadcast=0 and independently RPC-conflicted/rejected transactions. Independent wallet storage/mempool checks and a compatible red mutation prove why a normal return cannot stand in for acceptance. This does not close original R10 scopes or establish peer acknowledgement.

**Implemented app route:** Public getWalletTxDetails now supplies an off-GUI observation after normal commit. Missing/foreign/failed reads keep the confirmed commit and yield Unknown submission. The original sampled outcome is frozen for the completion page; it does not claim live state or peer receipt. Manual/model/QML cases and real disabled/rejected variants are qualified in the linked packet. The interface does not expose the exact causal rejection/relay result; a structured receipt and narrower status-only read would simplify this adapter. Existing private-wallet fee/send-all helpers are separate app migration work; no private accessor is needed for this status observation.

**Upstream simplification:** Return a structured receipt that separates wallet persistence, mempool acceptance, attempted relay, known rejection and uncertain effects, with the original transaction ID and actionable errors. State the temporal meaning of each field; neither a later status read nor a returned relay attempt proves peer receipt or confirmation. Do not combine this with an undocumented global wallet generation or claim RPC atomicity. Preserve UPSTREAM-005/006 and the R09 conditional limits.

**Boundary:** Continue the public-interface app solution. If a later precise requirement genuinely cannot be met without Core changes, record that evidence and notify/wait for input before dependent work; no such new mandatory blocker is established here.

### R10 public transaction ownership documentation (2026-11-13)

The public wallet commit interface retains the supplied shared transaction owner in wallet history. An app-owned cleanup alias passed into that interface created an app retirement cycle; the `app-only correction and source evidence <.validation/issue-remediation-pr1090-20261109/execution/gui-threading-implementation-20261112/CORE_TRANSACTION_OWNERSHIP.md>` gives Core an independent immutable Native transaction before commit. This is an app lifetime error, not an established Core defect or missing required interface capability. Optional upstream documentation identifying retained owners would make adapter lifetime reviews easier. No new API guarantee, backend repair, finding closure or Core edit is claimed. Preserve the notify-and-wait policy for any future issue that genuinely cannot be fixed behind the existing public interfaces.


## QT-DEPENDENCY-001 — native picker worker starts before connections

R10 frozen candidate `a69f18652926a2f145a6586bf3e26508c604974d`, unchanged Core `44901d8f64cb617714707bd025ecd83f108353c2`. This is a Qt dependency issue, not a wallet/Node interface gap. Stock instrumented Qt6.10.2 reproduces a TSan race during native QFileDialog construction in the actual Activity journey and an independent public QApplication/QFileDialog-only program (exit66,0.264s). The worker starts in QFileInfoGatherer's constructor before QFileSystemModel installs its internal signal connections. `Exact evidence and decision <.validation/issue-remediation-pr1090-20261109/execution/gui-threading-implementation-20261112/frontend-tsan/QT_PICKER_DECISION_V31.md>` records the relaxed connection-vector publication/read and separately reloaded count for upstream review.

No app workaround is adopted. Moving QWidget work off GUI is invalid; serializing app requests does not prevent Qt's own constructor/worker overlap. A proposed two-private-file dependency patch delays the worker start until connections exist; it is unapplied/unqualified and does not claim to fix all QObject connection races. A supported upstream fix/backport and shipping/CI provenance would allow retirement of a local patch. Required checks include an instrumented Qt-only reproducer, independent race negative control, actual picker journeys, native lifecycle and stderr/exit inspection on affected supported configurations. The user decision between pinned Qt fix, app-owned picker and retaining a blocked gate remains pending. Independent app fixes continue; no suppression or Core modification is authorized.

## QT-DEPENDENCY-002 — Qt6.4 pair-key hash integer instrumentation

Same R10 app/Core revisions. The pinned ASan/integer profile reports invalid-shift-base in Qt6.4's inline qHashCombine for QPair<qint64,int>, called by DebugLogModel::Reduce's stable overlap index. `Qt-only evidence <.validation/issue-remediation-pr1090-20261109/execution/gui-threading-implementation-20261112/asan-qhash-dependency-v31/summary.json>` reproduces exit1 using a SIZE_MAX seed and the exact profile; scalar control exits0. No suppression, sanitizer setting or global hash seed changes are used.

The isolated unvalidated draft replaces this Domain-local index with an ordered std::map keyed by the exact physical offset/chunk pair. insert_or_assign preserves the prior last-occurrence lookup rule and display overlap identity. A manual-executor regression preserves persistent indexes across identical multi-chunk records and checks exact remove/insert ranges, data, no reset and physical retirement. Full DebugLogModelTests and actual debug-log integration must be qualified on the next candidate. Upstream Qt should express intended unsigned hash wraparound compatibly with integer instrumentation and retain pair/hash semantic tests. The app index can remain ordered independently of any upstream resolution; no Core capability change is needed.
