# R35 proposed wallet-directory inspection boundary

The user requested an app helper that meets the strict boundary. A public-only
fresh-process route now has10 passing actual controls; production integration
and its focused gate remain pending. No Core change or private exception is
authorized. Earlier delivery e12365c8 retains its focused evidence and the
inherited private helper is not accepted. Core44901d8f64 is unchanged.

The preview feeds onboarding's existing-profile classification. Configuration,
settings, chain data or a recognized wallet database can mark a selected
directory as an existing profile. Wallet data alone must count, so setup does
not automatically apply fresh-install pruning recommendations to it. The
preview does not load wallets or read balances/keys.

`qml/onboarding_settings.cpp::HasExistingWalletData` calls
`wallet::ListDatabases(proposed_path)` through `wallet/db.h`. It existed at the
accepted a2af base; R35 did not introduce it. Nevertheless, it has not been
established as an approved exception to the user's strict Wallet/WalletLoader
boundary. No global boundary-compliance claim follows from the passing tests.

The public `WalletLoader::listWalletDir()` accepts no path. At this pinned Core
its implementation calls `ListDatabases(GetWalletDir())`, whose directory comes
from global `gArgs`. Constructing a loader with independent proposed arguments
does not fix that read. Changing the parent's global configuration merely to preview
another directory would violate ownership and concurrent-preview requirements.

The existing preview distinguishes actual wallet databases from junk files;
the root-wallet, explicit-walletdir and unrecognized-entry regressions protect
that behavior. Generic directory presence would silently change the decision.
Duplicating Core's database classifier would bypass the intended boundary too.

The app helper uses public Init factories, Node::forceSetting, the existing
public SelectParams startup glue and WalletLoader::listWalletDir/getWalletDir
in a fresh headless child. Child-only directory/network settings leave the
parent untouched. No node initialization, wallet load or database-format parser
is introduced. Exact network overrides matter because SQLite recognition uses
the selected network's message-start bytes.

Proof: `.validation/qt6-r11-r40-20261114/R35/public-directory-prototype/` contains
FEASIBILITY.json, REPORT.md and runtime/RESULT.json. Ten controls pass against
two real public-Core-RPC-created wallet fixtures, including root identity,
explicit directory, junk/empty/missing paths, wrong network/custom signet and
parent/child isolation. Targets were mounted read-only; content and filesystem
metadata remained unchanged. This is feasibility, not a production pass.

The production adapter must capture the original path/network/overrides, run
off GUI, bound child execution/output, cancel and reap safely, and distinguish
process/protocol failure from an empty directory. Manual timing and actual
process/Core integration remain required. Independent R36 work continues.

Optional upstream improvement: a typed, read-only WalletLoader inspection of an
explicit directory, returning recognized entries and scoped errors without
loading wallets, mutating global arguments, creating files or starting clients.
Tests should cover proposed/configured directory isolation, network paths,
empty/junk/recognized files, permission errors and concurrent previews. The
inspector would run on Native and preserve deterministic profile decisions.

The affected piece remains unaccepted pending production implementation and
validation of that public helper. R35's other
new ownership, persistence, migration, recovery and platform evidence is retained.
Independent implementation and checks may continue; no Core subtree edit,
silently weakened detection or new private accessor is permitted.
