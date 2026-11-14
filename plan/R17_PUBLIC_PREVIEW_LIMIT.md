# R17 public fee-preview limitation: decision pending

Two actual Core integration bodies ran on accepted R16 source
`a391ebaa175954db632cbf90fe314255075cd2ad` plus one test-only change,
with Core `44901d8f64cb617714707bd025ecd83f108353c2` unchanged.
The Bech32 control passes: preview and preparation both charge 1,410 sats.
The Taproot-recipient case fails: preview 1,530 sats versus prepared 1,650 sats.
The mempool stays empty. Logs, XML, commands and binary hash are under
`.validation/qt6-r11-r40-20261114/R17/red-048/local-01/`.

The pinned Wallet interface cannot read the effective change-output policy.
Unsigned preparation supplies the actual result but consumes change keys;
passive fee observations must not acquire that mutation silently. No production
workaround or Core change was made. The required user decision is pending:
hold policy-dependent preview migration, or adopt rates-only previews and
redesign dependent amount/Maximum presentation around actual preparation.
No elapsed time supplies that decision. Other R17 work and independent orders
continue. The smallest upstream improvement is a supported nonmutating preview
or effective change-policy capability behind the public wallet interface.
