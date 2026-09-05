# Synthetic legacy migration fixtures

These small, unfunded regtest wallets were generated with the repository-pinned Bitcoin Core 28.2 release. They contain no user keys or funds. The encrypted fixture uses the public test-only passphrase `qml-legacy-test-only`.

`metadata.json` records the downloaded release archive hash, executable hash, individual backup hashes and exact generator command. `../generate_legacy_wallets.py` creates backups through the historical Core RPC, with a keypool of two. It refuses to overwrite existing fixtures. Download the pinned tool explicitly with `test/get_previous_releases.py -t /tmp/qt6-legacy-fixture-tools v28.2`; the helper verifies the release checksum.

Normal integration tests do not invoke that tool or require an old executable. They verify these hashes and copy each backup into their isolated regtest wallet directory before migration. Wrong-passphrase and successful encrypted/unencrypted migration use the production migration model. Missing or modified fixtures fail the suite.

`legacy-companions.bak` adds watched and unwatched 2-of-3 multisig scripts with only one local key, producing both watch-only and solvables companions during migration. `external-signer.bak` and `multikey-watchonly.bak` are descriptor backups for capability/restore coverage. The signer fixture was generated using the repository's synthetic signer; restoring and inspecting it requires neither a physical device nor an external signer command. Fixture metadata records the additional generator command and signer script checksum. Every wallet is unfunded; the fixed public keys in multisig fixtures have well-known test-only secrets and must never receive real funds.

The generator verifies the pinned Linux v28.2 executable checksum before use, chooses an isolated local RPC port, and shuts down its owned child process on failure. It can produce any subset with `--fixtures`; use a new output directory because it refuses to overwrite existing fixtures. The original two backups remain unchanged.

The separate compatibility lane generates one fresh historical backup and migrates it through the current QML UI:

```sh
python3 test/functional/qml_wallet_migration_compat.py \
  --configfile=build/test/config.ini \
  --historical-binary-dir=/path/to/pinned/v28.2/bin --require-historical
```

For CTest, configure `QML_HISTORICAL_BINARY_DIR` to register `test_bitcoin-qt-historical-wallet` with the `historical-wallet` label. That designated lane fails if its tools are missing. Running the Python check locally without the optional tool path reports an explicit skip; it never downloads or discovers an arbitrary executable. Detailed migration errors and companion/capability permutations remain in C++ integration tests.
