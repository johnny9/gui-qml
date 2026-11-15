# Version 2 wallet app preferences

This is a separate, lazy migration of app preferences. Core wallet databases,
receive-request records and Core's persistent settings retain their formats.
No credentials, opening leases, operation tokens or transient permissions are
persisted.

Each entry is stored as one QVariantMap value at
`walletSettings/v2/{aliases,reservations}/w<lowercase UTF-8 hex identity>`.
It contains integer `version: 2`, exact canonical `identity`, and a `payload`
map. The unnamed wallet uses `w`. Case, significant whitespace, slashes,
percent signs and distinct Unicode sequences remain distinguishable even on
case-insensitive settings backends. The existing per-network QSettings scope
is retained. This migration does not introduce a new datadir scoping policy.

Alias payloads contain a QString `alias`. Reservation payloads contain a
QString `address` and QStringList `retired`. The production reservation adapter
receives the registry's captured canonical identity. Its legacy reader/mirror
uses the original public Wallet name; it does not resolve a later selection.

An identity without a v2 entry reads its legacy values without modifying them.
The first explicit write imports its existing state and stages one complete
v2 envelope together with legacy compatibility values, then synchronizes
QSettings and reports its actual status. Alias snapshots, catalogs and creation
validation all use the same reader. Clearing uses an empty string/tombstone;
QSettings recursive removal is never used for a raw wallet-name entry.

A complete v2 envelope is authoritative after commit. A missing v2 entry reads
legacy state; a complete entry survives an interrupted/missing legacy mirror;
an incomplete, mismatched or unsupported v2 entry fails closed rather than
reviving stale legacy state. Empty v2 values remain explicit tombstones. Qt's
atomic INI sync remains enabled. Native settings backends do not provide an
app-level multi-key transaction; the one-envelope recovery rule covers a
partial mirror. A failed sync is a failure/uncertain persistence outcome, not
success merely because an in-memory value changed.

Older GUI-QML binaries can read the maintained legacy mirrors. Writes by an
older binary after v2 migration are not imported back automatically: the old
recursive-delete behavior cannot distinguish an intentional clear from another
wallet's accidental deletion. Concurrent mixed-version writers and writable
downgrades therefore have no synchronization guarantee. Qt Widgets does not
consume these GUI-QML-only alias/reservation keys; wallet-file compatibility
is unaffected. Exhaustive native-backend/downgrade matrices remain R42 work.

Golden persisted fixtures cover before commit, after envelope commit before
mirror, committed clear and completed mirror. Independent alias deletion and
reservation deletion oracles remain separate. The settings-status control
records an already-checked failure path and does not promote the original
unchecked-write candidate to a newly proven production defect.
