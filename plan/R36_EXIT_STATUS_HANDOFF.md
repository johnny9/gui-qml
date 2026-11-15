# Independent original071 final-status repair

The GUI now retains the public `interfaces::Node::getExitStatus()` result on Native immediately after `appShutdown()`. The receipt survives missing GUI delivery. A real Core nonzero status takes priority over normal event-loop success; existing startup/settings/diagnostic failure ownership remains separate. Core and Qt are unchanged.

This slice is based on accepted main `9c71709c629b4b07029c7afd2aabe6f2eec5b0c0`, Core `44901d8f64cb617714707bd025ecd83f108353c2`. Tested implementation: `bc10ec6ac9b9a1817e38cfd90504fef29a6cf22f`. It has no R35 implementation dependency and grants no wallet-directory boundary exception. Full R35/R36 acceptance remains held.

The original public-Core storage-write oracle fails on parent `98b463b9def6c22fb9015b0db5025680709a230d`: visible `Failed to write block`, rejected mining RPC, process exit0. The fixed application exits1; normal bridge startup/shutdown exits0. Disposable permissions are restored before teardown. Both process stderr streams and exit statuses were inspected.

Focused ordinary validation: 37/37 Native bodies, zero skips (5 new receipt/lifetime cases plus32 existing lifecycle/drain controls), plus2 process cases. The first run retained five strict-fixture failures for the newly observed public getter. The repaired fixtures assert exact reads, after-shutdown ordering and the original Native thread; unexpected-call checking stays enabled.

Pinned Clang23/Bitcoin-plugin checks pass for5 handwritten units plus all7 fresh MOC aggregates; the final fixture amendment repeats1 unit plus7 MOCs. QML static checks pass259 files per supported generated-import variant. Official app lint passes. Exact commands, source hashes and raw artifact paths are in `R36_EXIT_STATUS_SCOPE.json` and `.validation/qt6-r11-r40-20261114/R36/exit-status/`.

R42 retains exhaustive coverage, full suites, platform/configuration matrices, sanitizer and complete IPC functional qualification. This is the focused original071 repair, not full R36 acceptance.
