# R35 proposed wallet-directory inspection boundary

Decision pending; no Core change or new exception is authorized. R35 delivery
e12365c86cb0b1c5e8bf1381bbcf6dd263b05be2 has focused passing evidence, but root
integration review found a preexisting direct wallet implementation service.
Core is44901d8f64cb617714707bd025ecd83f108353c2, unchanged.

`qml/onboarding_settings.cpp::HasExistingWalletData` calls
`wallet::ListDatabases(proposed_path)` through `wallet/db.h`. It existed at the
accepted a2af base; R35 did not introduce it. Nevertheless, it has not been
established as an approved exception to the user's strict Wallet/WalletLoader
boundary. No global boundary-compliance claim follows from the passing tests.

The public `WalletLoader::listWalletDir()` accepts no path. At this pinned Core
its implementation calls `ListDatabases(GetWalletDir())`, whose directory comes
from global `gArgs`. Constructing a loader with independent proposed arguments
does not fix that read. Changing process-global configuration merely to preview
another directory would violate ownership and concurrent-preview requirements.

The existing preview distinguishes actual wallet databases from junk files;
the root-wallet, explicit-walletdir and unrecognized-entry regressions protect
that behavior. Generic directory presence would silently change the decision.
Duplicating Core's database classifier would bypass the intended boundary too.

Smallest upstream improvement: a typed, read-only WalletLoader inspection of an
explicit directory, returning recognized entries and scoped errors without
loading wallets, mutating global arguments, creating files or starting clients.
Tests should cover proposed/configured directory isolation, network paths,
empty/junk/recognized files, permission errors and concurrent previews. The
inspector would run on Native and preserve deterministic profile decisions.

The affected piece remains unaccepted pending the user's decision. R35's other
new ownership, persistence, migration, recovery and platform evidence is retained.
Independent implementation and checks may continue; no Core subtree edit,
silently weakened detection or new private accessor is permitted.
