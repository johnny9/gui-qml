# Copyright (c) 2025-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

set(CMAKE_AUTOMOC ON)

add_compile_definitions(
  QT_NO_KEYWORDS
  QT_USE_QSTRINGBUILDER
)

qt6_add_qml_module(bitcoin_qml
  URI BitcoinApp
  VERSION 1.0
  STATIC
  SOURCES
    bitcoin.cpp
)

target_link_libraries(bitcoin_qml
  INTERFACE
    bitcoin_qmlplugin
  PUBLIC
    Qt6::Widgets
    Qt6::Quick
)

qt6_import_qml_plugins(bitcoin_qml)

add_executable(bitcoin-qml WIN32
  main.cpp
  ${PROJECT_SOURCE_DIR}/src/init/bitcoin-qt.cpp
)

# TODO: Switch to own resource file.
add_windows_resources(bitcoin-qml ${PROJECT_SOURCE_DIR}/src/qt/res/bitcoin-qt-res.rc)
add_windows_application_manifest(bitcoin-qml)

target_link_libraries(bitcoin-qml
  PRIVATE
    core_interface
    bitcoin_qml
    bitcoinqt
    bitcoin_node
)

install_binary_component(bitcoin-qml)

# Use the `bitcoin-qml` target as a drop-in replacement
# for the legacy QtWidgets-based `bitcoin-qt`.
set_target_properties(bitcoin-qt PROPERTIES
  OUTPUT_NAME "bitcoin-qt-legacy"
  EXCLUDE_FROM_ALL TRUE
)
set_target_properties(bitcoin-qml PROPERTIES
  OUTPUT_NAME "bitcoin-qt"
)
