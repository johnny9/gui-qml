# R40 desktop intent owner — component checkpoint

Component implementation `30fe15f7e44165661c08baca18060f1a924c4573`, unchanged Core
`44901d8f64cb617714707bd025ecd83f108353c2`. This is private component
development, not R40 acceptance or completed production ingress wiring.

The desktop intent owner is an application mailbox for external Bitcoin links,
opened files and forwarded process requests. It retains a bounded queue until
an actual registered inspection receiver is ready. Readiness automatically
inspects one request at a time by creating its Source. This does not select a
wallet, apply a payment, prepare a review, sign or send a transaction. Closing
that inspection or explicitly handing it to the captured wallet view releases
the presentation slot. The original receiver,
delivery and route remain fixed while its creation acknowledgment is unresolved.
A lost acknowledgment queries the same pre-reserved receipt instead of calling
the factory again. The receipt means Source admission, independently of parsing,
QObject survival and later wallet effects.

The Domain queue admits batches atomically, with 16 pending requests, 64 retained
receipts, 16 Ki UTF-16 characters per input and 128 KiB pending value budget.
Producer-key deduplication is limited to the retained receipt lifetime. It is
not durable or cross-restart exactly-once processing. Stop drops undispatched
requests with explicit receipts and accounts for an original in-flight handoff.
The Qt model exposes cached state and narrow commands. Its admission fence
releases Native owners independently of view destruction and emits no UI signal.

Thirty-six current ordinary bodies pass: 16 owner/facade rows, ten Native
transport rows and ten QML consumer rows. They cover bounded admission, stale
tickets, manual lost-acknowledgment schedules, argument boundaries, actual Qt
file-open events, captured paths, real Source-factory receipts, one presentation
at a time, early reentrant close, and physical drain with retained facades.
The Native admission fence is checked both by Domain and at the queued GUI
factory handoff. The new held-handoff timing row failed before the repair;
its original earlier-closure control still passed. Full focused rerun and
exact app lint pass. Logs, commands, XML and actual body counts are retained in
`.validation/qt6-r11-r40-20261114/R40/gui-focused-12`, `gui-fence-red-12` and
`gui-lint-12`. Current pinned analysis is recorded separately in `gui-static-12`.
Earlier zero-body compiler failures and actual shutdown counterexamples remain
retained with their original source revisions.

The Native transport component is qualified independently, including real-thread
controls, original-key receipt queries and physical socket retirement. Early
Qt file-open capture and declarative inspection/continuation are implemented.
Remaining acceptance work is actual startup composition before Core
configuration writes, direct/onboarding continuation, and actual application
argument/two-process ingress evidence. Startup distinguishes Primary,
Forwarded, Rejected and Unknown;
Forwarded cannot be presented as a locally initialized node. A synchronous
startup-Core callback cannot post to the same lane and wait for its own result.
Keep the listener on a stable existing executor until actual Native retirement.

R27 consumers and R36/R39 shared composition are qualified prerequisites.
R37/R38 composition remains separately tracked. Full suites, exhaustive
coverage, platform/configuration matrices,
native Windows and sanitizers remain explicitly pending at R42. No Core or Qt
source changes, finding-register rewrites, push or external submission occurred.
