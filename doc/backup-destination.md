# Backup destination policy

The file chooser supplies a selection to the wallet's backup operation. The
operation resolves local file URLs, an optional wallet-name suggestion for a
directory, and the `.bak` suffix on the wallet's background executor. Existing
`.bak` and `.dat` suffixes are preserved without regard to case. Other file
suffixes are retained and followed by `.bak`.

The operation requires consent for the resolved destination when that file
already exists or when it rewrites a selected filename. Its typed properties
supply the final path and replacement intent to the confirmation view. The
backup chooser delegates overwrite confirmation to this operation; other file
choosers retain their existing confirmation default. Direct model requests
follow the same policy.

Approval applies to one request and its inspected destination fingerprint.
Refusal cannot invoke Core's backup writer. After approval, Core writes to an
operation-owned temporary directory in the destination directory. The writer
then checks the destination again and installs the staged backup. A changed,
missing or newly created destination causes refusal; a new file is installed
with a non-replacing rename. Application wallets share a mutex around this
final check and install, after Core has released its wallet lock. Existing
symbolic links and non-regular destination files are refused.

The fingerprint and application coordination do not provide a filesystem-wide
compare-and-swap against unrelated processes racing the final replacement.
Failures after a filesystem operation require inspecting the destination before
retrying. Temporary files are cleaned by the background operation.

`WalletQmlModel::backupWallet()` reports request admission. Its typed
`backupOperation` reports pending, confirmation, success or failure. Closing a
wallet retires work before write admission. An admitted write retains its real
outcome even when its GUI observer disappears; it is never reported as cancelled
merely because a view closes. Queued work pins the existing per-wallet backend
until its asynchronous shutdown tail runs. Full storage and security owner
migration remains R14.

Regressions exercise pure transition guards, manual scheduler delivery and
notification reordering, real executor threads, competing application writers,
changed destination fingerprints, native-adapter options, the actual Qt Widgets
fallback chooser, byte preservation, and independent Core restoration of the
resulting backup keys. Native platform chooser certification remains part of
the recorded platform obligations.
