# Execution seam

`BackendExecutor::scheduler()` supplies a composition-time port for owned domain
commands. Admission is thread-safe and returns a typed task ID or a closed or
transport rejection. Work and outcome consumption run as separate queued turns
on the existing serial background worker. Neither callback may access a GUI
object, and outcome consumers must not throw. GUI publication is a separate,
queued adapter carrying owned result values and a guarded receiver.

The ledger retains admitted work through queued, executing, outcome-ready and
delivering phases. Closing admission rejects new work while admitted outcomes
and their resource destruction finish on the background lane. Domain completion
and ledger drain do not depend on a live GUI observer. Transport implementations
must enqueue without executing inline and remain available for admitted outcomes.
The Qt adapter keeps its worker alive until this ledger drains; the manual
adapter uses the same ledger and deterministic decisions.

Fee reading is the first consumer. Its input captures recipient values, coin
control and custom-fee text. The production effect owns wallet access only during
its background execution. Typed revision events guard stale completion and close.
The facade caches presentation values and publishes change notifications. A
rejected read settles the pending presentation immediately. The injected presentation transport separates GUI publication from domain drain.
The injected timer
controls the existing 250 ms debounce; the manual environment supplies fake read
effects rather than running Core work on the test/GUI thread.

Tests distinguish virtual deadline expiry, effect execution, domain outcome and
GUI publication. Old success and old failure, observer loss, admission closure,
owned result destruction and physical thread affinity have independent oracles.
Real-thread tests withhold GUI event processing while awaiting domain completion.
Timeouts only bound a failed test; they do not establish the competing event order.

This is the R01 seam. The legacy GUI-completion API is retained for consumers
that have not migrated. Existing synchronous wallet getters and other fee-policy
entry points remain owned by the later wallet migrations. Bounded keyed lanes,
full cancellation/lifetime facilities and the full fee-reader contract remain
with R08 and R17 respectively.
