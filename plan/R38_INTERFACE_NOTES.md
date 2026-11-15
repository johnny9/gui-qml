# R38 interface and validation boundaries

Focused implementation and composition gates passed at application
56812dd840ad34a5451539f7b5fd47142029bd90 on accepted R27/R36/R37/R39 code.
Core44901d8f64cb617714707bd025ecd83f108353c2 is unchanged. Exhaustive coverage,
sanitizers and the full platform/configuration matrix remain R42 obligations.

The console owns an immutable accepted command identity, named RPC endpoint and
terminal receipt independently of its output generation or a view. Clear removes
presentation, resets navigation and reannounces a wallet context on the next
command; it neither cancels nor forgets an admitted effect. A refused input keeps
its draft. Pointer hover is separate from keyboard completion consent. Prepared
syntax and original sensitive arguments stay on the worker. Output, recall and
original-input conversion diagnostics follow the existing redaction policy.
Arbitrary RPC replies are not claimed to be free of secrets.

The log reader owns offsets, partial framing, bounded tail/reduction and an
activation revision. Opened-handle identity, modification stamp and saved tail
anchor detect ordinary rotation/rewrite before reusing offsets. This is not an
atomic file snapshot or detection of adversarial edits preserving all sampled
metadata/content. An inactive admitted read settles without painting another
activation. Native cleanup and unrelated console admission remain independent.
Native Windows/macOS identity branches require the final R42 platform matrix.

The ordinary focused profile may select RpcConsoleModelTests,
RpcCommandExecutorTests, DebugLogModelTests and NodeModelTests through qualified
APP_UNIT_TEST_CLASSES. Actual source-scope JSON and binary discovery are required;
this does not certify the default full suite. Exhaustive coverage, matrices and
sanitizers remain R42 obligations.

## Upstream opportunities and retirement conditions

- Public RPC argument conversion reports rejected input inside an unstructured
  exception. The app replaces conversion diagnostics for its nine sensitive
  commands with a generic error, preserving ordinary diagnostics and valid
  parameters. Direct/nested malformed-sensitive and ordinary controls plus actual
  GUI/Core privacy journeys qualify that adapter. A structured public conversion
  error with argument/type metadata and a payload-free diagnostic would avoid
  app-specific filtering. Retire this adapter only when that interface exists and
  the same controls pass. Core's useful CLI diagnostics remain unchanged.
- Public Node RPC execution captures a named endpoint, including unnamed
  `/wallet/`. This fulfils the current console contract. A known-unavailable GUI
  facade cannot become an implicit node request; a missing endpoint fails without
  node retry. Independent unload/reload under the same name is not a stable
  wallet-instance guarantee. If that product contract is introduced, a public
  instance-bound RPC capability would be needed. Retire the app availability and
  target adapter only with an equivalent capability and default/named/missing/
  unavailable endpoint regressions. No wrong-wallet effect or new wallet-scoped
  console requirement is inferred from the old draft.
- Node header/active-chain/time getters are separate observations. The app uses
  its existing background read and chain-notification revision to reject older
  height/time publication, preserving independent callback sync/progress facts.
  A public coherent versioned chain snapshot and invalidated-header notification
  would simplify it. Current invalidation/reconsider/new-block and held-read,
  presync and reentrant-publication controls must qualify this implementation;
  original impact is not inferred from a state catalog. Retire the merge adapter
  only after that observation API preserves the controls. Existing conditional
  Core-lock acceptance is unchanged.
