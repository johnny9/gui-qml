# Shared Source close correction from the R40 ownership review

Source close used ordinary Domain admission. A full ordinary queue could reject both the operation and factory close requests after their facades had fenced themselves, leaving the factory registry owning an unclosed Source actor and its Native leases. Three genuine-budget controls reproduced the failure, including both facade close orders while the Qt objects remained alive.

Source creation now reserves one Cleanup relay before exposing its Owner. Close still revokes presentation immediately, then retains the original Owner in a pure mutex-protected slot and activates that reservation. Domain moves the retained owner out and performs the original close/query/accounting decision. Repeated close returns the same task identity. The slot owns no actor before close, so unused actor disposal and reservation failure cannot create a cycle. No wallet, Core, Qt or executor API change is required.

In-flight Native jobs and admitted children keep their original outcomes and queries. A throwing presentation-retire callback does not erase an admitted child or cause another dispatch. The existing held-read timing fixture now delivers the reserved Cleanup relay and Domain outcome while keeping Core read execution held; its Closing, no-read and no-observation assertions remain.

Final focused qualification executes 59 Source bodies (54 existing plus 5 new), including manual timing, unknown/lost replies, actual public review consumers and real threads, plus 2 actual Core import controls:61/61 pass. Pinned Clang23/Bitcoin-plugin checks pass 2 CPP plus 7 fresh MOC. App lint and 2 existing IPC-build process lifecycle cases pass with normal 0/fatal 1 and stderr inspection. Exact commands, identities, revisions, failures and binary/input provenance are retained in plan/evidence/R26_SOURCE_CLOSE.json and workspace R40 artifacts.

Root owns composed R40 receiver/startup/process acceptance. No new Wallet/WalletLoader capability adapter is introduced. Exhaustive coverage/platform/configuration/sanitizers/native Windows remain R42.
