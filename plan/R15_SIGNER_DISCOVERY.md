# R15 signer discovery and configuration

R15 owns M23 and findings 062, 086, 156–160 and 313. The R33 settings service scope 155 and R24 transaction-signing handoffs remain with their owners. Core remains pinned at `44901d8f64cb617714707bd025ecd83f108353c2` and unchanged.

## Current requirements

The user clarified that explicit physical-device selection is not required. Core/HWI may use any connected device that can service the fixed wallet descriptors. The current Core creation API requires one connected logical signer; the GUI must report this restriction accurately rather than invent automatic multi-device selection.

A protocol fingerprint is a master key-origin hint. Devices with the same keys can share it; it is neither a unique physical-device identifier nor a cryptographic wallet identifier. Discovery row IDs combine the effective command configuration with this hint to avoid accidental stale-result reuse. They do not authenticate device identity or determine the descriptors Core actually creates.

Before descriptors exist, fresh creation may use Core's current sole signer. Once a descriptor request/operation is bound, later discovery must not retarget it. Compatible same-key replacement may succeed; incompatible keys must fail clearly. The GUI does not stamp cached discovery identity onto a created wallet. Core's actual descriptor public keys and later signature verification remain authoritative.

## Implementation

`signerdiscovery::Reader` owns generation-tagged reads and a bounded publication mailbox. It admits at most one process read and retains one latest successor. Older results cannot overwrite a newer configuration or requested revision. Failures retain visible stale rows with their actual cause. Reading getters performs no Core or process work.

`SignerDiscoveryModel` projects typed phases, freshness, availability, errors, effective configuration and separate edited text. Settings writes use an owned command; known persistence is retained if runtime application/readback later fails. Such a result requires explicit refresh before retry, and a retry of an already persisted command avoids rewriting the file. Configuration metadata has a separate read lane so a held device process cannot hide effective/editable state or stop a settings change.

Production discovery uses the public Node `enumeratesigners` RPC because `listExternalSigners()` omits the key-origin hint. Core/configuration/process work runs on background executors. The GUI receives bounded projections and performs presentation and necessary handoffs. Existing Options setters forward to the typed owner in the application; their standalone compatibility path remains for existing callers pending R33.

The model bounds command text to 32768 UTF-16 units plus one invalid sentinel, sixteen devices, names/error messages to 4096 units and fingerprint fields to eight hex digits. Oversized edits never become a valid truncated command. Validation follows Core's command tokenization: executable plus arguments separated by spaces/tabs, without shell quoting or expansion. It verifies the executable that Core will invoke, including bare commands resolved through PATH.

The 30-second GUI deadline is a logical outcome. Core's existing subprocess API cannot cancel a hung invocation. A timed-out physical invocation remains owned until it actually returns; no second process is launched over it. Shutdown waits for actual process and configuration retirement independently of held view publications.

## Consumers

QML binds typed state and sends narrow edit/apply/check commands. CLI configuration remains visible and usable but read-only; persisted settings cannot override it. Returning to a cached settings page requests a fresh check. Hardware creation remains discoverable when compiled support exists even when the signer is absent or locked. Known missing-signer approval errors are retained in the model and produce a specific reconnect/retry action.

## Focused acceptance

Exact commands, selections, source hashes, failures and corrections are retained in the task's validation directory. This document does not claim acceptance before that focused gate completes. Exhaustive coverage, all configurations/platforms/Qt versions, and sanitizer qualification are deferred to R42 by the user.
