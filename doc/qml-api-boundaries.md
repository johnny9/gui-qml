# QML API boundary checks

The existing `qml_lint` target checks real generated C++ type information before
linting both shipped resource variants. Its gate now also compares selected
QObject declarations with the current production moc JSON and exercises typed
QML consumers and invalid state/backend accesses. It does not compile a second
replacement model fixture.

`test/qml_api_policy.json` records the reviewed identities, results and capability
properties. Projection state stays read-only. The explicit writable exceptions
are presentation interest and view source configuration. Node's legacy setters
are audited separately; its listed authoritative observations are read-only.
Every checked mutable property needs its declared real notification signal.
Public Core value records can be explicitly allowed; Core handles, execution
objects and arbitrary callbacks cannot be exposed as commands.

The schema gate reads `ENABLE_WALLET` from the active CMake cache. Node, Settings
and Peer checks apply to either configuration. Wallet-only schema/control cases
are explicitly reported as inapplicable in a wallet-disabled configuration, not
as passing. That configuration additionally rejects the explicitly conditional
`PaymentImportOperation` QML type. Other historical wallet foreign names are
not assumed to disappear without their own configuration qualification.

Metadata membership comes from the current target's `AutogenInfo.json` and
`mocs_compilation.cpp`, so an unchanged but excluded old moc file cannot qualify
a class. Current source properties must match the generated schema. Negative
controls remove notifications, introduce writers/handles, change identity and
result contracts, and exercise direct and transitive forbidden dependencies in
the selected pure-domain header closures. Commented source is not a declaration.

Actual notification emission, coherent publication, original-operation lifetime,
thread affinity and concurrent Core/RPC guarantees still require the existing
component, manual-executor, physical-thread and integration tests. This gate is
not a proof that every indirect GUI call is safe, nor a coverage measurement.
It must run after the existing moc/type-registration prerequisites have completed.

To inspect the schema gate independently after a configured build:

```bash
python3 test/qml_api_schema.py --source-root . --build-dir build \
  --output build/qml-lint/api-schema.json
```

Qt's JavaScript call boundary may ignore surplus arguments to a C++ invokable.
Arity is therefore not treated as authorization. Original typed operation,
prompt, target and effect guards remain the authority.
