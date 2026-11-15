# R29 public-interface limits and later work

No Core or Qt source changes are made. The public Wallet getAddress/setAddressBook sequence detects an already changed label and refuses that save. Store serializes application writers and propagates immutable actual persistence receipts independently of GUI delivery. It cannot provide an atomic compare-and-swap against external Core/RPC writers between that check and setAddressBook. An upstream public expected-generation/conditional address-book write with an actual typed receipt would remove that remaining race; no atomic guarantee is claimed here.

Core bulk address/coin getters still allocate native collections before our Domain preparation/publication budgets apply. The existing incremental persistent address tree, bounded slices, ingress limits and editor inventory limit do not establish a bound on every Core internal allocation. This remains explicit upstream/final-series resource qualification, not a new hidden exception.

ExactTextDocument uses Qt public QTextDocument::toRawText to avoid toPlainText NBSP normalization. It preserves literal text/UTF-16 contents within the32768-character transport limit, maps native paragraph separators to entered LF and deliberately uses PlainText. This is a bounded native handoff. It is not a claim to distinguish a literal U+2029 from a native paragraph break or to change Qt internals. Official source: https://doc.qt.io/qt-6/qtextdocument.html#toRawText .

Scope197's encrypted watch-only premise is not established for the pinned public Core creation/encryption interface. Legal public watch-only signing is tested and guarded before credentials using privateKeysDisabled. No invalid native state is fabricated to claim the encrypted reproduction. Broader signer device handoff belongs to R24, not R29.

Sanitizer/full platform/configuration/coverage/compatibility matrices remain R42 under the user's changed validation cadence. R29 uses ordinary focused correctness, real threads/public Core, app lint, pinned tidy and QML static analysis. Historical coverage ledgers and qualified ELFs remain immutable; no new percentage or full-series acceptance is claimed.
