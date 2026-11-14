# R17 fee preview: accepted upstream deferral

The user chose to preserve Core's automatic/configured change policy and defer
draft-048-D048 on 2026-11-14. It remains unresolved and requires Core interface
changes under that policy. The review must display Core's actual prepared fee;
the preview discrepancy is not finding closure or permission to edit Core.

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
workaround or Core change was made. Other R17 work and independent orders
continue with this explicit deferral. The smallest upstream improvement is a supported nonmutating preview
or effective change-policy capability behind the public wallet interface.

The minimal policy query would be:

```cpp
virtual OutputType getTransactionChangeType(
    const std::vector<wallet::CRecipient>& recipients,
    const wallet::CCoinControl& coin_control) = 0;
```

Core's adapter would reuse its existing `TransactionChangeType` helper with the
effective override/default and available internal descriptor managers. It must
not reserve or keep a change destination. The app queries before supplying its
dummy change script, then sizes that script from the returned public type.
`getDefaultAddressType()` exposes the receiving default rather than this
decision; an explicit change-type override can also differ without Taproot.
Actual preparation remains authoritative for the fee and current spendability.

The preview already supports all four public output types. It chooses an explicit
coin-control type when supplied, otherwise the receiving default. The reproduced
case uses SegWit v0 in the preview and Taproot in Core's automatic decision:
12 extra non-witness bytes at10 sat/vB explain the120-sat difference.

The existing public `CCoinControl::m_change_type` can explicitly align preview
and preparation. Three actual Core controls pass, including Taproot and a
configured Legacy override, in `R17/core-controls-05/integration/explicit-public-change-policy/`.
That would make the GUI own change policy; the user chose not to adopt it.
The positive review-fee oracle in `R17/local-06` checks displayed review values
against independent public RPC input/output amounts before conditional R17
acceptance. The original failed preview equality remains under `red-048`.
