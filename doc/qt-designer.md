# Editing QML in Qt Quick Designer

The QML interface can be edited visually with **Qt Quick Designer** in Qt
Creator or with **Qt Design Studio**. The standalone Qt Widgets Designer edits
`.ui` widget forms and cannot open these QML files.

## Qt Creator

1. Open the repository's top-level `CMakeLists.txt`.
2. Select a desktop Qt 6 kit and let CMake configure the project.
3. Open a file below `qml/components`, `qml/controls`, or `qml/pages`.
4. Select **Design** to open the 2D view.

The top-level CMake project adds `qml/designer/mockImports` to
`QML_IMPORT_PATH`. This lets the QML code model resolve the types normally
registered by `qml/bitcoin.cpp` without starting a Bitcoin node.

## Qt Design Studio

Open `BitcoinCoreApp.qmlproject`. The initial document is the design-system
preview at `qml/designer/DesignerPreview.qml`. Other components and pages are
available from the project tree and the Components view.

The `.qmlproject` declares `qml/designer/mockImports` as `mockImports`. Qt
Design Studio uses those files for design-time type information but excludes
them from generated application builds.

## Design-time data

Files under `dummydata` provide deterministic values for C++ context
properties such as `nodeModel`, `walletController`, and `optionsModel`. They are
loaded by Qt Quick Designer only and are not included in `bitcoin_qml.qrc`.

When a new context property or C++ QML type is introduced, add the corresponding
property to `dummydata` or type to
`qml/designer/mockImports/org/bitcoincore/qt`. Keep mocks side-effect free:
they run inside Qt Quick Designer's preview process.

Some images are provided by the production `image://images` provider. They can
appear as empty image bounds in the 2D editor because the designer deliberately
does not load the application backend; their geometry and surrounding controls
remain editable.
