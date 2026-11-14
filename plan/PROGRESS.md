# Implementation progress

The [approved cadence](QT6_VALIDATION_CADENCE_20261114.md) separates implementation
progress from final qualification. The copied CSV registers in `tracking/`
retain their historical provenance; this file records later branch-local
progress without changing shared finding or schedule registers.

| Orders | Current progress |
| --- | --- |
| R00–R08 | Previously accepted on the amended base; original evidence retained |
| R09 | User-accepted conditional app-side prerequisite; upstream limitations retained |
| R10 | Implementation/focused ordinary checkpoint complete; final qualification pending R42 |
| R11–R41 | Later implementation orders remain pending; R11 is next |
| R42 | Final coverage, full suites/matrices, native Windows and sanitizers pending |

The [R10 checkpoint](R10_FOCUSED_CHECKPOINT_20261114.md) records exact tested
app/Core identities, nonempty counts, commands, artifacts and unresolved issues.
The program remains 318 findings, 329 scopes, 43 orders and 36 schedules.
