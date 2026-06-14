# Copyright (c) 2025-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

set(CMAKE_AUTOMOC ON)

option(ENABLE_TEST_AUTOMATION "Enable test automation bridge for QML UI testing" OFF)

set(QML_QRC "${CMAKE_CURRENT_SOURCE_DIR}/bitcoin_qml.qrc")
qt6_add_resources(QML_QRC_CPP ${QML_QRC})

file(GLOB_RECURSE QML_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
list(FILTER QML_SOURCES EXCLUDE REGEX "/main\\.cpp$")
list(FILTER QML_SOURCES EXCLUDE REGEX "/androidnotifier\\.(cpp|h)$")
if(NOT ENABLE_TEST_AUTOMATION)
  list(FILTER QML_SOURCES EXCLUDE REGEX "/test/")
endif()
list(APPEND QML_SOURCES ${QML_QRC_CPP})

add_library(bitcoinqml STATIC ${QML_SOURCES})

target_compile_definitions(bitcoinqml
  PUBLIC
    QT_NO_KEYWORDS
    QT_USE_QSTRINGBUILDER
)
if(ENABLE_TEST_AUTOMATION)
  target_compile_definitions(bitcoinqml PUBLIC ENABLE_TEST_AUTOMATION)
endif()

target_include_directories(bitcoinqml
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/src
)

target_link_libraries(bitcoinqml
  PUBLIC
    core_interface
    bitcoin_node
    univalue
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:QRencode::QRencode>
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Widgets
)

# Compile QML-app-specific translations and embed them as resources.
qt6_add_translation(QML_GUI_QM_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/locale/bitcoin_qml_es.ts
)
set_source_files_properties(${QML_GUI_QM_FILES} PROPERTIES
  QT_RESOURCE_ALIAS "bitcoin_qml_es.qm"
)
qt6_add_resources(bitcoinqml "qml_gui_translations"
  PREFIX "/translations"
  FILES ${QML_GUI_QM_FILES}
)

# Embed bitcoin-qt translations built by the Qt subdirectory.
file(GLOB BITCOIN_QT_TS_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/qt/locale/bitcoin_*.ts"
)
set(BITCOIN_QT_QM_FILES)
foreach(ts_file IN LISTS BITCOIN_QT_TS_FILES)
  get_filename_component(locale_name "${ts_file}" NAME_WE)
  set(qm_file "${PROJECT_BINARY_DIR}/src/qt/locale/${locale_name}.qm")
  set_source_files_properties(${qm_file} PROPERTIES
    GENERATED TRUE
    QT_RESOURCE_ALIAS "${locale_name}.qm"
  )
  list(APPEND BITCOIN_QT_QM_FILES ${qm_file})
endforeach()
qt6_add_resources(bitcoinqml "bitcoin_qt_translations"
  PREFIX "/translations"
  FILES ${BITCOIN_QT_QM_FILES}
)
add_dependencies(bitcoinqml bitcoin-qt)

add_executable(bitcoin-qml WIN32
  main.cpp
  ${PROJECT_SOURCE_DIR}/src/init/bitcoin-qt.cpp
)

# TODO: Switch to own resource file.
add_windows_resources(bitcoin-qml ${PROJECT_SOURCE_DIR}/src/qt/res/bitcoin-qt-res.rc)
add_windows_application_manifest(bitcoin-qml)

target_link_libraries(bitcoin-qml
  PRIVATE
    bitcoinqml
    bitcoinqt
)

install_binary_component(bitcoin-qml)

if(BUILD_GUI_TESTS)
  add_subdirectory(test)
endif()

# Use the `bitcoin-qml` target as a drop-in replacement
# for the legacy QtWidgets-based `bitcoin-qt`.
set_target_properties(bitcoin-qt PROPERTIES
  OUTPUT_NAME "bitcoin-qt-legacy"
  EXCLUDE_FROM_ALL TRUE
)
set_target_properties(bitcoin-qml PROPERTIES
  OUTPUT_NAME "bitcoin-qt"
)
