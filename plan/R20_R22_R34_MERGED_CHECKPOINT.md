# R20, R22 and R34 merged focused checkpoint

Tested app `644abba531304b3608ad4cfc9cc686f15d145c26`, Core
`44901d8f64cb617714707bd025ecd83f108353c2` unchanged.

The combined shared focused runner executed 387 distinct behavioral bodies:
387 passed, zero failed/skipped. Pinned Clang23.1.6 with the Bitcoin plugin
passed all 24 changed handwritten C++ units and seven fresh app MOC aggregates.
App lint passed in an isolated clone. QML lint passed both 251-file compatibility
variants with zero warnings/errors and all ten negative probes.

The actual IPC-enabled GUI passed native startup failure/recovery and bridge
startup/shutdown. The recovery retained the invalid proxy intent after exit1,
then reached Core ready with the corrected saved setup, exact550MiB and exit0.
The scenario checks GUI stderr and exit status; both outer stderr files are empty.
R34 also passed its three original X11 geometry/restart scenarios remotely.

Exact commands, binary hashes, actual XML counts, numbered failed attempts and
worker source carries are retained in
`.validation/qt6-r11-r40-20261114/integration-16/ACCEPTANCE.json` under the workspace.
The first attempted dispatch was denied Docker access. The next invocation built
and passed QML lint but ran zero bodies because the coordinator supplied an empty
selector key. The runner correctly rejected it. Removing that key selected the
intended whole class; the final 387-body run passed without source changes.

R20 is conditional on the [approved upstream receipt limitation](API_DEFERRALS_20261115.md).
R22 and R34 meet the focused cadence. Full R24, R30 and R35 work remains active.
Exhaustive coverage, complete configuration/platform suites, native Windows and
sanitizers remain required at R42. This checkpoint does not claim final qualification.
