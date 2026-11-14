# Qt 6 refactoring plan and notes

The [approved validation cadence](QT6_VALIDATION_CADENCE_20261114.md) applies to
every work order. Keep focused ordinary correctness, manual timing/real-thread
checks, relevant integration and changed-scope lint/tidy during implementation.
R42 owns exhaustive coverage proof, full suites and configuration/platform
matrices, native Windows and sanitizers. Final qualification remains pending.

R10's [focused checkpoint](R10_FOCUSED_CHECKPOINT_20261114.md) is complete;
final qualification remains pending R42. Start with [current progress](PROGRESS.md)
and that checkpoint for source identities, validation and unresolved issues.
[CHECKPOINT.md](CHECKPOINT.md) and the [earlier repair pass](R10_REPAIR_20261113.md)
retain their historical qualifications.

- [Total refactoring plan](QT6_TOTAL_REFACTOR_PLAN.md)
- [Handoff and resume instructions](QT6_REFACTOR_HANDOFF.md)
- [Architecture requirements](QT6_ARCHITECTURE_FOUNDATION.md)
- [FSM proposal review](QT6_FSM_PROPOSAL_REVIEW.md)
- [Upstream improvement notes and deferred Qt issues](QT6_UPSTREAM_IMPROVEMENTS.md)
- [Public wallet adapter plan](QT6_WALLET_ADAPTER_PLAN.md)
- [Base revisions](QT6_BASE_UPDATE.md)
- [R00 validation requirements](QT6_R00_REQUIREMENTS.md)
- [Work orders and scope/schedule tracking](tracking/)
- [Retained validation summaries and source identities](evidence/)

These files were saved from the workspace plan. Historical `.validation/`
paths are inline code references to retained local workspace artifacts, not
bundled files or hyperlinks. Original shared registers remain
unchanged; published CSV copies normalize line endings only, with parsed records
verified unchanged and provenance in `evidence/tracking-copy-provenance.json`.
Moving the historical documents into `plan/` did not certify the pushed revision.
Later progress is recorded separately in `PROGRESS.md`. Shared registers remain
unchanged; no final qualification is implied by document relocation.
