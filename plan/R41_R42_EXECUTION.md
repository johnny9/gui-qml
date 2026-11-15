# R41 cleanup and R42 final qualification

Work starts at app `f1c03c3dfccd42f4b89f0322e08d5de5d60f95a1`, stock Core
`44901d8f64cb617714707bd025ecd83f108353c2`. Evidence is retained under workspace
`.validation/qt6-r41-r42-20261115/`. R41 is focused accepted at `90abf2fdd92`;
R42 final qualification is active. R11–R41 focused acceptance remains distinct
from final acceptance. See [R41 evidence](R41_FOCUSED_ACCEPTANCE.md).

R41 audits old methods, members, callbacks and QML callers against their current
owners and original test requirements. Only fully migrated paths are removed.
Live compatibility, persistent formats and guards remain until equivalent
consumers and tests exist. The audit includes public schema/notification checks,
negative dependency checks and actual manual/physical-thread/Core consumers.
Lexical call counts and schema equality alone do not certify runtime behavior.

R42 restores the default full unit build (`-DAPP_UNIT_TEST_CLASSES=`), embedded
QML resources and the supported full CI/configuration matrix. It owns measured
native and QML/JS coverage, source/profile identity and legacy accounting,
whole-series app/Core lint, pinned Clang23/Bitcoin-plugin tidy, sanitizers,
IPC-enabled GUI lifecycle stderr/exits, compile-time wallet-disabled and runtime
disablewallet checks, static depends, native Mac and Windows. The explicit lane
inventory is `R42/QUALIFICATION_PLAN.json` in the workspace evidence directory.

Validation uses leased warm builds and content-checked compiler caches. Root
dispatches authorized source exports to johnny on Gameboy, Bazzite and the Mac
VM; concurrent jobs have disjoint build trees. Generic JSON phase specifications
retain raw commands, exits, nonempty test evidence and failed prerequisites,
and continue independent checks. A focused pass, old ELF or different compiler
cannot certify an unexecuted final-head configuration.

Existing approved limitations remain unresolved: R09 Core device locking and
cancellation, R17 effective-change-policy fee preview, R20 precise commit receipt,
R30 capability007 and the native Qt picker TSan race. Actual transaction review
remains authoritative. Do not change Core/Qt, add a custom native chooser, claim
unconditional bounded shutdown or represent these limitations as fixed.

The R42 finding/schedule overlay must preserve all 318 findings, 329 scopes and
36 schedules, including non-defect dispositions, and link real selectors and
execution artifacts. Canonical shared planning registers and unrelated working
changes remain untouched. Coverage gaps, valid failures and unavailable matrix
lanes are qualification gaps, never implicit passes.
