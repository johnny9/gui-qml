# R32 Activity filtering and CSV ownership

Focused runtime is pending. Core remains pinned and unchanged. This slice builds
on accepted main0224 (including R31 and R24's Activity-only delegate reuse
workaround), and preserves its original opening, source and retirement fences.

Activity snapshots use structurally shared value indexes and immutable full-row
deltas. Source projection reuses rows only when their actual Core facts and
original request association inputs agree. Filter jobs reuse an exact predecessor
and policy; stale predecessors and changed filters use bounded full evaluation.
Pending/count/available-amount aggregates belong to the same indexed generation.
Native work/allocation counters and manual clocks are the test oracle, rather
than wall-clock-only speed claims. A bulk public Core history capture remains a
bulk allocation; this slice does not claim to bound Core's internal work.

Amount controls bind committed bounds and keep a range envelope when available
values change. The amount maximum includes unpaid requests because that filter
also governs those rows; requests remain excluded from wallet pending balance.
Date filters refresh across day rollover, including a command arriving before
the next wakeup. Inclusive local civil dates handle skipped days. Week start and
clock formatting follow locale, with bounded declarative presentation for the
other date surfaces recorded in original052.

CSV export captures the original immutable frame and unit. It reuses M24's
existing native destination policy for suffix normalization, exact final path,
version consent, staging, installation and known/unknown outcomes. One resource
key serializes each default policy owner, including retirement. Bounded terminal
receipts survive a reentrant observer deletion and a successor command. Existing
AppFileDialog remains the chooser; cancellation never means file success.

No Core/Qt changes or persistent namespace/format migration. Existing shared UI
preferences already resolve original299; their live-view and persisted-key
controls remain in the focused batch. Exhaustive coverage, full matrices and
sanitizers remain pending R42. Original source findings and all preflight failures
are retained under `.validation/qt6-r11-r40-20261114/R32/`.

The native chooser now starts from a model-owned capture token. It retains the original immutable source and unit before path selection, without reserving a mutation scope during the chooser wait. Source retirement cancels only its exact token; stale completions cannot consume a successor, and an existing root admission fence still refuses a new write. Four deterministic public-command schedules cover filter/unit changes, source replacement, root closure, and reentrant source cancellation.
