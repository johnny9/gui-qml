# R13 public-interface follow-ups

Core remains unchanged at `44901d8f64cb617714707bd025ecd83f108353c2`.
Production restore/migration stays behind public Wallet/WalletLoader APIs.

* Typed partial results should enumerate durable wallet effects, auxiliary
  names, warnings and recovery paths even when later settings work fails.
  The adapter preserves uncertainty when the current API throws without that
  receipt. Pinned449 already returns a successful warning under `-nosettings`;
  R13 does not claim the obsolete deletion/late-exception premise occurs there.
* A typed encryption probe should distinguish known encrypted/unencrypted
  from unknown/error. The current boolean can miss a legacy backup's required
  credentials. The app handles the exact contextual credential-required result
  with a fresh prompt bound to the same copied file, without recopying it.
* Expected database-generation or opened-file inputs could make external-file
  identity checks atomic. The adapter checks original paths, format/header,
  size and modification facts around effects; it does not claim arbitrary
  external replacement is impossible between those checks and Core's open.
* Optional startup-persistence arguments and typed credential/pruning causes
  would remove contextual error matching and separate durable wallet work from
  preference effects more directly. Generic failure never authorizes replay.

Detailed source-specific observations and failed premises remain in
`.validation/qt6-r11-r40-20261114/R13/upstream-interface-notes.md`. Its historical
alias-storage paragraph describes the worker's pre-R14 base; combined code
retains the separately qualified R14 v2 preferences and migration policy.
No internal classifier, private-wallet access or subtree exception is used.
