# R14 security/storage checkpoint

Private implementation at `3b102b287a09a411193991ed4da0890d4ff50b75` uses unchanged Core `44901d8f64cb617714707bd025ecd83f108353c2`. Security observations are separate from captured credential prompts and retained mutation outcomes. The password QML consumer binds typed operations. Backup uses original Session admission and the shared guarded native chooser; PSBT import adopts the same original-context handoff. The chooser rejects overlap, reentry and obsolete callbacks while continuing to use Qt's component.

A separate [versioned settings migration](R14_SETTINGS_V2.md) isolates encoded canonical identities, imports legacy values, maintains compatibility mirrors and defines interrupted/malformed recovery. Original reservation and alias deletion have independent before/after oracles. Source-only candidates and already-refuted claims are not relabeled as fixed bugs.

Focused validation passed310 distinct ordinary bodies across the documented snapshots:254 native,41 QML and15 independent Core/RPC. This includes GUI-held RPC unlock/relock, credential errors, backup refusal with byte-for-byte sentinel retention, restoration/key coverage and encryption. The source03 view fixture correction changes only test QML; production and other tests match source02. All19 changed C++ units plus7 app MOCs pass pinned Clang23/Bitcoin-plugin checks; production QML lint passes. Final app lint passes at47efac5091778711895340aea9682111e3fe44f9; the focused checkpoint is complete. Final qualification remains R42.

Commands, XML, stderr, exit statuses, negative controls and exact source carries are under `.validation/qt6-r11-r40-20261114/R14/`; `SCOPE_ORACLE_MAP.json` records all14 primary and2 cross-consumer dispositions plus O13/O33 boundaries. Shared finding/schedule registers remain unchanged.

R09 Core-wide synchronization limitations remain. Native Qt picker diagnostics and macOS keyboard/disabled-Open cases retain their separate unresolved status; this does not claim those Qt defects fixed. R21/R22/R30/R31 own remaining PSBT/export/sharing consumers. Exhaustive coverage, platform/configuration suites, native Windows and sanitizers remain R42.
