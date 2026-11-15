# Console and log ownership

The console captures an explicit node or wallet endpoint at submission. The
unnamed wallet uses `/wallet/`; it is distinct from the node endpoint. The
existing selected-wallet settings consumer rejects submission while its captured
facade is unavailable or its capabilities are unknown. An RPC targeting an
unloaded wallet receives that endpoint's error. It never retries on the node.
`submitInput` keeps Widgets' independent node-level `stop` intent available even
when the selected wallet is unavailable.
This preserves the existing supported console routes; it does not introduce a
new wallet-scoped console workflow.

Each admitted input has a monotonic command identity and an output generation.
The worker prepares syntax and redaction once, then evaluates that transient
plan. Nested calls retain the same endpoint. Arguments covered by the maintained privacy filter remain only in transient
execution input. Request rows and the bounded recall inventory
receive the maintained filtered representation, including `(…)` placeholders.
Recalling a command uses the current explicit target and requires secret
arguments to be entered again. History does not silently restore an old wallet.

The existing `execution::OwnedCommand` cell records the terminal result on Domain
independently of the GUI. Its value distinguishes success, a known RPC error,
failure before an RPC call, and an uncertain outcome after a call was attempted.
An exceptional task transport receipt remains an exception-bearing result; it
does not assert that a dispatched RPC had no effect. Arbitrary RPCs have no
automatic retry. The facade reserves busy ingress before admission and rejects
duplicate input until completion. The independent Control route for the exact
Widgets `stop` shortcut remains available. Parent shutdown fences admission and
drains accepted packets independently of that shortcut's spelling.

Clear advances the presentation generation, removes rows and pending row batches,
resets history navigation and forgets the last wallet banner. It retains the
redacted recall inventory. An already admitted command keeps its endpoint, busy
ownership and terminal receipt. Old-generation rows are suppressed; the terminal
receipt still releases busy, including when Clear interrupts incremental GUI
projection after Native completion. Subsequent wallet output announces its
context again. Every command row exposes command, generation and endpoint roles.

Pointer hover does not select input for Enter. Up/Down explicitly select a
completion; the first navigation selects its first row. Enter submits that
selection or the exact typed draft, without first replacing the input field.
Rejected or busy submission preserves the draft. Tab and clicking a completion
only fill the field; a subsequent submit is a new explicit key intent.

The console accepts at most 65,536 UTF-16 input units and a 1,024-unit wallet name.
Recall keeps 50 filtered entries of at most 4,096 units plus a truncation notice.
Output has both a 5,000-row ceiling and a 2,097,152-unit HTML text ceiling. Physical
chunks contain at most 1,024 visible UTF-16 units; public output append also
enforces a bounded row and newest suffix. Timestamp and endpoint metadata are
bounded separately. These are component bounds, not a bound on a response
allocated by Core inside an arbitrary RPC. `MonospaceOutputView` continues to
virtualize delegates and search immutable bounded projections off GUI.

The log reader retains its configured path and logging-enabled fact. It performs
file work and reduction off GUI, keeps byte offsets and unfinished record framing,
and compares the opened handle's file identity before trusting an old offset.
An unchanged size additionally requires an unchanged modification stamp; the
existing tail anchor detects rewriting/truncation around the saved offset.
Unavailable native identity falls back to a full bounded tail. A tail read scans
at most 8 MiB, and retained/display text each has an 8 MiB budget. Existing row,
line and chunk limits, pagination, omission notices and explicit external-open
errors remain in force.

Log activation has its own revision. Deactivation detaches watchers and stops the
debounce timer, while an admitted read still settles. A result from another
activation cannot change the current rows or error. The successor resets its
projection because Native reduction may have advanced without GUI adoption.
Reactivation catches up from the owned file state and preserves wider pending
pagination. It neither closes the shared scheduler nor cancels console commands.
Watchers still use actual platform file/directory notifications; explicit refresh
and existing path/disabled-logging controls remain independent observations.
