# R40 focused acceptance

R40 and the requested R11–R40 batch satisfy the approved focused acceptance
gate. R41 has not started. R42 final qualification remains pending.

Application behavior was tested at
`491d8cdbbfe14d7ef3e20f67f0ecbdc82488868c`; the final reset lifecycle script is
`461516a35388e0acf83b6c06a3ad5b06afcd73ff`. Integration commit
`67a823ba1c8de31202c82103a6c7a54c858ecdf2` has identical application/build/QML
sources to the tested application; only the checked lifecycle script differs.
Bitcoin Core remains clean at `44901d8f64cb617714707bd025ecd83f108353c2`.

## Resulting behavior

The application captures actual Qt file-open events before its first startup
event loop and parses command-line requests on Native. Qt retains its synthetic
argument list. A bounded Domain mailbox retains original producer, delivery and
receiver identities. Actual readiness inspects one Source at a time. Wallet
selection, applying a payment, review, signing and submission remain explicit
user actions. Unknown acceptance queries the original receipt without replay.

An existing-instance-only Native probe precedes onboarding. It forwards to a
confirmed same-user peer; Missing leaves the queue and arguments untouched and
grants no Primary authority. Normal admission uses the final profile after any
chooser. Rejected or uncertain transfers cannot fall through to local Core
initialization. Genuine Primary onboarding/reset behavior is preserved.

Native sockets, configuration reads and startup work use the stable injected
executor. Native admission is rechecked at the actual queued GUI factory
handoff. Presentation release, mailbox stop and Source close reserve Cleanup
before effects, so ordinary queue exhaustion cannot strand shutdown. GUI
facades can remain alive without retaining physical Native ownership.

## Executed evidence

All paths below are relative to
`.validation/qt6-r11-r40-20261114/R40/`. Counts are separate executions/source
qualifications and overlap; they must not be summed as one final full suite.

| Gate | Actual result | Evidence |
| --- | --- | --- |
| Final Bazzite owner/transport/Source/QML | 52/52 bodies; seven behavioral process cases pass | `bazzite-03/artifacts/green/RESULT.json`, `startup.stdout` |
| Final shared Gameboy integration | 67/67:14 Native,50 QML,3 public Core; no skips/empty selections | `gameboy/shared-composition-01/SHARED_ACCEPTANCE.json` |
| Shared pinned static | 11/11 entries; complete261-file QML lint per variant, zero warnings/errors | Same shared packet; all370 Core compile commands unchanged |
| Reset lifecycle completion | One existing scenario, two actual GUI children; both normal exits0 and strict stderr checks pass | `bazzite-05/artifacts/startup.stdout`, `startup.stderr` |
| Existing-peer Native component | 19/19 bodies;9 pinned entries; app lint; normal/fatal IPC process controls exit0/1 | `PROBE_COMPONENT_ACCEPTANCE.json` |
| Startup caller/profile | 70/70:21 new role/profile,45 Flow,4 profile/fence;10 pinned entries/app/QML lint | `probe-caller/QUALIFIED_CALLER_01.json` |
| Preparation owner | 48 unchanged preparation bodies retained from the earlier97-body gate | `startup-slice/QUALIFIED_COMPONENT_03.json` |
| Source lifecycle | 59/59 owner bodies plus2 public Core imports;9 static entries and IPC controls | `SOURCE_CLOSE_ACCEPTANCE.json` |
| Darwin supplement | 31/31 at explicit earlier cb3 source:28 owner/transport,3 QML | `macos-01/artifacts/`; no final whole-platform claim |
| Exact-source app lint | Pinned app entry point passes; final script lint recorded separately | `final-lint-17/RESULT.json`, lifecycle repair receipt |

Per-selection commands, actual XML identities, raw output, compiler database,
binary hashes, source manifests and cache statistics remain in those packets.
The existing `test/run_focused.py`, unit class targeting and functional scenario
selector were used. No new feature-specific test runner was introduced.

## Primary scope and schedule mapping

`draft-245-D245` is established by the independent retained-parent process in
`parent-argv-02/results/RESULT.json`: MainWindow, wallet shell and public RPC are
ready, yet the valid argument URI has no mailbox or visible request. Normal GUI
quit exits0 with checked stderr. The new primary-arguments process demonstrates
the repaired visible delivery. Actual Qt event, argument, duplicate, readiness,
shutdown and Native transport regressions supplement that process oracle.

O30/O36 acceptance ownership uses the original Source/receipt, lost-ACK query,
atomic batch/no accepted prefix, held factory/native-fence, full ordinary budget,
physical retirement and unknown/no-fallback rows in DesktopIntentTests,
DesktopIntentTransportTests and PaymentImportTests. R26/R36 outcome evidence is
retained; this does not imply automatic Apply or transaction replay.

## Retained failures and limits

The first parent probe assumed a parse rejection; it did not establish that
claim and remains explicitly inconclusive. Bazzite01/02 retain both actual
secondary30s timeouts: reset onboarding ran before role discovery. The Native
probe/caller fixes those failures without extending the watchdog. The first
reset lifecycle audit exposed uninspected child pipes; checked rerun04 then
correctly rejected fixture SIGTERM exit-15. Rerun05 uses normal GUI quit and
checks both actual exits and stderr without new allowances.

Earlier compiler attempts with zero bodies, failed timing rows, rejected stop
and presentation release, Source close failures, and their exact source hashes
remain retained. No empty or failed selection is reported as a pass.

Installed OS associations/packaging, full configurations including a fresh
wallet-OFF build, native Windows, exhaustive line/branch coverage, default full
suites and sanitizers remain R42 obligations. The known native Qt picker TSan
finding, existing two WalletOpening failures and earlier broad PSBT timeout are
not declared fixed by this gate. Core R09/R17/R20/R30 limitations retain their
approved upstream decisions. Configuration inspection/reread is not atomic;
see [the adapter and upstream requirement](R40_STARTUP_PREPARATION_ADAPTER.md).
No Core/Qt edits, push, external messages or canonical register overwrite occurred.
