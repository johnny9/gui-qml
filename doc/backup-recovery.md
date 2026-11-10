# Passwords and recovery copies

Setting a password on the pinned descriptor wallet creates new receiving and
change keys. Existing keys remain in the live wallet. Earlier backup copies may
not cover payments to the new keys. This is a recovery limitation if a user later
loses the live wallet and relies on an earlier backup; encryption itself is not
evidence that funds disappeared from the live wallet.

The Set password introduction explains this before the operation. Encrypted
wallet settings bind recovery guidance directly to the existing Back up wallet
row and observed encryption property. Returning to or recreating settings retains
the guidance beside that wallet-scoped backup action.
Older backups should be retained securely. Changing an existing password does
not generate new keys and keeps its existing success message.

Controlled integration tests preserve the live wallet and restore old/fresh
backups into distinct wallets. They verify later received funds and an actual
post-encryption change output, balances, ownership and unchanged original backup
bytes. The GUI process regression sets the password and writes both backups
through settings before independently verifying restored ownership through RPC.
Missing descriptors alone are not treated as a funds-loss oracle.

This bounded disclosure fix adds no Core call, thread or native I/O implementation.
Existing security/backup calls remain behind their compatibility interfaces;
R14 owns the full M32 background security/recovery migration and retained
regressions. R05 owns final destination/overwrite policy.
