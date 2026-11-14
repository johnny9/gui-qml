# Public lifecycle capability follow-up

Scope075 original load-notification path is addressed app-side: subscribe to public `interfaces::Wallet::handleUnload` inside the original Native notification callback, before Core adds the wallet to its externally unloadable catalog. No Core implementation is changed.

The startup `getWallets()` snapshot and each returned handle's subsequent subscription remain separate public operations. A future public atomic opening subscription or durable per-opening unloaded/status receipt would let consumers prove whether an unload occurred before registration, and would make startup and independently returned-handle reconciliation easier. The focused075 regressions do not claim that broader atomic getter/subscription guarantee. Any future fix requiring Core changes must be noted and escalated before implementation, per the user's boundary.
