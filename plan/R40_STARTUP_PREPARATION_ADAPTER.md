# R40 startup profile adapter and upstream simplification

The desktop request listener needs the final data directory and network before
Core initialization writes settings, migrates a legacy profile, or initializes
the node. The application uses the pinned public argument/configuration reads
to capture that identity on Native, starts the asynchronous transport on Domain,
and admits the existing Native preparation only for a real Primary receipt.
Forwarded accounts transport handoff, not wallet inspection or completion.
Original producer keys and request dispositions remain in its retained result.

An existing-instance-only probe now precedes onboarding. Its profile-only
resolver uses independent public arguments and the existing public base-chain
factory; it neither reads dynamic settings/completion markers nor changes global
chain parameters. An explicit chooser without a fixed CLI directory retains
chooser precedence. A matching existing peer receives the original owned batch
before a reset wizard can appear. Rejected or Unknown never enter onboarding or
replay. Missing conveys no Primary authority and leaves the queue/input/service
available for the normal final-profile decision after onboarding. It creates no
early listener that could accept requests for an unchosen profile. A genuine
Primary still receives the original reset/onboarding and write-accounting flow.

Core currently has no public immutable prepared-configuration value that both
read-only discovery and initialization consume. The application therefore uses
`ArgsManager::ReadConfigFiles`, public base-chain selection and the canonical
network data-directory read before role discovery. It does not read/write the
dynamic settings file in that prefix. The Primary then runs the existing
`common::InitConfig` and checks the effective identity again before migration
and base initialization.

This is not an atomic configuration-file transaction. Another writer can alter
configuration between those reads. The identity fence prevents wrong-profile
migration/base work, but InitConfig itself may already have created directories
or rewritten settings before a mismatch is detected. A completed onboarding
save can also precede role discovery; its actual saved receipt is retained and
is never described as an absent write. No Core policy or wallet format parser
is duplicated.

Proposed upstream improvement: expose a public read-only preparation result
containing the effective configuration and resolved profile, then let normal
initialization consume that same result without rereading mutable files. An
equivalent public pre-write role-discovery hook after final configuration
resolution would also simplify this adapter. Retire the read/recheck adapter
only when an upstream capability supplies that exact guarantee and the actual
primary/forwarding, configuration-drift and write-preservation controls pass.

The application retains one stable background executor for the endpoint. Full
saved-profile recovery stays on Domain/Native; GUI receives bounded summary and
transport DTOs. Stop first closes atomic ingress, retires sockets/server/lock,
releases preparation/result/queue capabilities, and only then drains that
executor and acknowledges Core's before-interrupt barrier. A rejected physical
retirement is unresolved, not an acknowledgment.

Focused controls live in StartupPreparationTests, OnboardingFlowTests and
StartupPolicyTests. Actual forwarding/write-preservation process checks belong
to `test/functional/interface_qml_startup.py`. Exact candidate hashes, commands,
case counts and failures are retained under the R40 startup-slice task evidence;
this note does not itself claim qualification. Exhaustive coverage, sanitizer
and platform/configuration matrices remain R42 obligations.
