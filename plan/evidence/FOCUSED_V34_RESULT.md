# Frozen34 focused qualification

Candidate `8ae9ce530f76e3522e932997f7120af705598246`; Core `44901d8f64cb617714707bd025ecd83f108353c2`. Focused qualification **PASS**; R10 acceptance **not satisfied**.

| Lane | Executed body results |
| --- | --- |
| Normal | 47 full classes; 1840 complete rows: 1828 pass, 12 exact paired fatal delegates |
| ASan/integer/UBSan | 49 selections; 1879 executions, 1840 unique: 1867 pass, 12 paired delegates |
| Frontend TSan | 50 selections; 1915 complete rows: 1902 pass, 13 explicit skips (12 paired fatal delegates and one paired permission companion) |

No unexpected failures, incomplete selections or sanitizer diagnostics in these focused lanes. Actual fatal12/bootstrap8 children pass per profile; TSan permission companion1 passes. Complete DebugLog class83/83; reviewed mandatory block176 includes pre-existing coverage and is not176 new tests.

Public Core integration5 passes in normal and ASan; affected Core integration28 passes per profile. Normal QML250 executions/236 unique bodies across9 suites; affected ASan QML28/28. Overlapping executions are not added into a fictitious unique total.

Exact commands, dependency identities, raw outputs and counts are linked by [FOCUSED_V34_ASSESSMENT.json](FOCUSED_V34_ASSESSMENT.json), `orchestration <FOCUSED34_ORCHESTRATION.json>`, and each recorded job result. Broader checks are separate. Broad lint34 subsequently found the executable-mode integration error; preserve its failure. The Qt picker will be replaced at application level per user decision; frozen34 is immutable and does not contain that future workaround.
