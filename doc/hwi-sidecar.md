# Bundled HWI test artifacts

This branch builds HWI 3.2.0 as a headless PyInstaller one-folder sidecar and
packages it with `bitcoin-core-app`. It deliberately produces two native test
artifacts:

| Target | Artifact | Runtime layout |
|---|---|---|
| Linux x86_64 | `bitcoin-qml-hwi-test-x86_64-linux-gnu.tar.gz` | `bin/bitcoin-core-app`, `libexec/hwi/hwi` |
| macOS arm64 | `Bitcoin-QML-HWI-test-arm64-macos.zip` | `Bitcoin-QML.app/Contents/MacOS/bitcoin-core-app`, `Contents/MacOS/libexec/hwi/hwi` |

These artifacts are labeled `UNTRUSTED-TEST`. CI creates a fresh BIP340 key
for every job, embeds its public key in the app, signs the HWI manifest, and
destroys the private key before upload. This proves that tampering is detected;
it does not establish who published the artifact.

## Build order

Use Python 3.12 and Poetry 2.3.1. PyInstaller output must be built natively.

1. Build `hwi/dist/hwi` with `HWI_LIBUSB_PATH` set to the exact target libusb
   shared library.
2. On macOS, ad-hoc sign every Mach-O file in that directory and regenerate
   `hwi-manifest.json`.
3. Sign the final manifest with `bitcoin/contrib/hwi/sign_hwi_manifest.py`.
4. Configure gui-qml with `BUNDLED_HWI_DIR` and the printed
   `HWI_MANIFEST_PUBKEY`.
5. Build `bitcoin-core-app` with a fresh build directory and the depends
   toolchain.
6. Assemble the platform layout. On macOS, sign the outer `.app` without
   `--deep`, verify it with `codesign --verify --deep --strict`, and confirm
   that the HWI manifest hash did not change.

A local test key must be supplied as a regular mode-`0600` file outside the
repository. Do not pass the secret in a command-line argument. The helper
`contrib/generate_hwi_test_key.py` can create a throwaway key without printing
it; it refuses to overwrite an existing file.

Packaged builds ignore a saved GUI signer path and use only the verified
sidecar. The GUI displays the bundle verification state and does not expose a
manual path editor. Developers may bypass this only by starting the app with
both `-signerpolicy=any` and an explicit `-signer=/absolute/path`; the UI labels
that mode as an unsigned developer override.

## Automated acceptance

- HWI tests cover canonical manifest output, target and version metadata, and
  unsafe, broken, or escaping symlinks.
- Bitcoin tests cover signatures, complete file-set equality, hashes, sizes,
  schema and target mismatches, and non-absolute or composite commands.
- gui-qml tests cover the locked bundled status and archive modes, symlinks,
  metadata, and relative layout.
- Each packaged app is launched from that final relative layout. Its RPC must
  enumerate through the signed HWI, while a copied bundle containing one
  undeclared file must be rejected before HWI executes.
- Linux CI reports every ELF dependency and its maximum required GLIBC symbol,
  failing if it exceeds GLIBC 2.35.
- macOS CI requires a native arm64 worker, signs sidecar Mach-O files
  leaf-first, and verifies the complete app signature after packaging.

## Coldcard MK4 manual gate

Run this checklist separately on the extracted Linux and macOS artifacts.
Record the artifact SHA-256, operating-system version, Coldcard model and
firmware version, and sanitized application logs. Do not upgrade the Coldcard
as part of the test.

1. Connect one Coldcard MK4 over USB and enumerate exactly one signer.
2. Create a native-SegWit external-signer wallet.
3. Derive a receive address, display it on the Coldcard, and confirm that the
   device and gui-qml show the same address.
4. Fund the wallet on regtest or signet, sign a simple PSBT on the Coldcard,
   and broadcast it.

All four operations must pass on both systems. An HWI or firmware
incompatibility is a blocker, not a waived result. Taproot display is outside
this first Coldcard gate.

## Deferred production work

The notarized preview publisher, a controlled release signing key, Guix-built
Python closure, broader Linux ABI baseline, and Bitcoin depends download target
remain separate follow-up work. No production trust should be inferred from
these CI artifacts.
