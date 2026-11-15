# R40 desktop intent owner — component checkpoint

Implementation `1741e20b4c366a82bec6b6f5da7fc8c547c5becc`, unchanged Core
`44901d8f64cb617714707bd025ecd83f108353c2`. This is private component
development, not R40 acceptance or completed production ingress wiring.

The desktop intent owner is an application mailbox for external Bitcoin links,
opened files and forwarded process requests. It retains a bounded queue until
an actual registered inspection receiver is ready. Inspect creates a Source;
it does not apply a payment, sign or send a transaction. The original receiver,
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

Nine current ordinary Qt bodies pass: bounded admission and stale tickets,
manual lost-acknowledgment schedules, argument/option boundaries, captured paths,
real Source-factory receipts, GUI readiness and physical drain with retained
facades. Pinned Clang23/Bitcoin-plugin analysis passes three affected C++ units
and seven fresh MOC aggregates, with four unchanged C++ units explicitly carried
from the earlier static run. Exact app lint passes. The source-qualified logs,
commands, XML, failed runs and counts are in
`.validation/qt6-r11-r40-20261114/R40/COMPONENT_CHECKPOINT.json` and its linked
`domain-focused-06`, `static-06`, `lint-06` directories. The actual one-body
shutdown counterexample remains in `drain-red-05`; zero-body compiler failures
and the corrected direct-include lint failure remain separately recorded.

Remaining work includes Native local transport, early file-open capture, typed
startup continuation before Core configuration writes, declarative desktop
inspection/continuation, real-thread controls and actual process/platform ingress
evidence. Startup must distinguish Primary, Forwarded, Rejected and Unknown;
Forwarded cannot be presented as a locally initialized node. A synchronous
startup-Core callback cannot post to the same lane and wait for its own result.
Keep the listener on a stable existing executor until actual Native retirement.

R27 consumer qualification and R36/R39 shared composition are independent
prerequisites. Full suites, exhaustive coverage, platform/configuration matrices,
native Windows and sanitizers remain explicitly pending at R42. No Core or Qt
source changes, finding-register rewrites, push or external submission occurred.
