# R15 upstream interface notes

No Core or Qt source is changed by R15.

* Public signer enumeration omits the protocol key-origin fingerprint. The app uses the public `enumeratesigners` Node RPC, validates its documented fields and bounds its own projection. A typed public enumeration result could remove RPC parsing.
* Core's synchronous signer subprocess API exposes neither cancellation/deadline nor an output-limited streaming handle. App deadlines therefore cannot claim physical cancellation. A bounded cancellable subprocess capability would make reliable shutdown/deadline reporting easier. This is the existing R09 limitation; the app retains ownership until physical completion.
* `WalletLoader::createWallet` has no captured expected-origin/configuration argument. The current single-signer GUI does not promise explicit device selection, so this is not an acceptance exception. If a future GUI promises a particular key origin, an optional expected-origin argument, checked against Core's captured enumeration before descriptor import, would avoid a discovery-to-create gap. An origin fingerprint is still only a hint; descriptors and verified signatures establish actual key compatibility.
* Public Wallet offers no read-only descriptor/origin projection suitable for passive post-creation identity inspection. Do not allocate an address, reserve a key, create a PSBT or access wallet internals merely to verify discovery identity. A future read-only descriptor capability could simplify diagnostics, with precise privacy/budget semantics.
* Device replacement during actual signing and HWI compatibility errors belong to R24. R15 preserves fixed operation/descriptor ownership, tests creation handoffs, and never treats a matching fingerprint alone as cryptographic proof.
