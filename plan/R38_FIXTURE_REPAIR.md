# R38 first focused run and fixture repairs

Frozen6894b87b275e27fa50c0be5ba4f614acd22f8017 and its failures remain immutable.
The first candidate built and passed pinned tidy/QML static checks. Its225 Native,
37 QML and29 public-Core bodies were nonempty; their raw failures are retained.
The original71 parent retains20 Native (16 intended failures/four controls), one
QML failure and eight public-Core failures. The two Core header exceptions were
not original-oracle proof.

The coherent test-only correction preserves all effect/identity/privacy oracles:

- Six date assertions and the Core information consumer now use the already
  accepted localized short-format presentation. They still compare the exact
  original header/block timestamp and height, including stale read schedules.
- The signer settings fixture uses a second valid executable spelling (`/./`),
  rather than adding unsupported arguments to an executable pathname. The
  configuration identity changes; pending/freshness, exact scan/write counts and
  final device/effective-path assertions remain.
- The process bridge passes the QML function's actual command argument. Its
  default JavaScript parameter does not provide a zero-argument Qt meta-method.
  Real QML routing, captured endpoint, privacy anchors and shutdown checks remain;
  independent integration cases retain physical Enter/navigation.
- The physical Control/Meta+L schedule requests and verifies an active window,
  then waits for the exact single output-generation transition within the
  existing QtTest watchdog. It still holds the actual RPC, preserves the draft,
  receipt and exactly-once result, and never invokes Clear in place of the key.
- Context/banner checks retain all exact source-role and displayed-value
  assertions while allowing the asynchronous output projection to settle.
  A payload-free oracle-line diagnostic identifies any remaining mismatch.
- A small independent public-Core diagnostic reproduced `ProcessNewBlock, block
  not accepted`: mining the same coinbase script within the same second recreated
  the deliberately invalidated block hash. Changing only the replacement
  coinbase script from raw51 to raw52 completes invalidation, reconsideration and
  replacement mining. The Core GUI regression now uses this distinct block and
  adds method-specific public-RPC exception diagnostics. Its lowered-header,
  recovery and real clock assertions are unchanged. The original comparison will
  carry the same fixture correction with no production change.

No production app/Core/Qt source, animation, warning filter, timeout extension or
assertion removal is part of this repair. Subsequent actual runtime remains
required; this document is not acceptance. Diagnostic evidence is retained under
`.validation/qt6-r11-r40-20261114/R38/rpc-diagnostic-{01,02}`.
