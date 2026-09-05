# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

add_executable(test_bitcoin-qt
  qml/qml_tests_main.cpp
  ../../init/bitcoin-qt.cpp
  test_application.cpp
  test_applicationrouter.cpp
  test_translationmanager.cpp
  test_console_integration.cpp
  test_navigation_integration.cpp
  test_onboarding_integration.cpp
  test_node_integration.cpp
  test_banlistmodel.cpp
  test_blockclockdial.cpp
  test_desktopwindowbehaviormodel.cpp
  test_debuglog.cpp
  test_display_settings.cpp
  test_guiargs.cpp
  test_networkstatusmodel.cpp
  test_networktraffictower.cpp
  test_nodemodel.cpp
  test_noderuntimemodels.cpp
  test_runtimedialogmodel.cpp
  test_mempoolmodel.cpp
  test_onboarding_storage.cpp
  test_peerlistmodel.cpp
  test_peerstatsutil.cpp
  test_rpccommandexecutor.cpp
  test_testbridge.cpp
  test_unit_tests_main.cpp
)
add_windows_application_manifest(test_bitcoin-qt)

if(ENABLE_WALLET)
  add_test(NAME test_bitcoin-qt-no-wallet-broadcast COMMAND test_bitcoin-qt --suite=integration -platform minimal)
  set_tests_properties(test_bitcoin-qt-no-wallet-broadcast PROPERTIES ENVIRONMENT "QML_TEST_NO_WALLET_BROADCAST=1;QML_DISABLE_DISK_CACHE=1")
  add_test(NAME test_bitcoin-qt-blocks-only COMMAND test_bitcoin-qt --suite=integration -platform minimal)
  set_tests_properties(test_bitcoin-qt-blocks-only PROPERTIES ENVIRONMENT "QML_TEST_BLOCKS_ONLY=1;QML_DISABLE_DISK_CACHE=1")
  target_sources(test_bitcoin-qt PRIVATE test_sendsubmit_integration.cpp)
  target_sources(test_bitcoin-qt PRIVATE test_transactionreview.cpp test_sendprepare_integration.cpp)
  qt6_add_resources(test_bitcoin-qt wallet_qml_tests PREFIX /test FILES qml/tst_walletreceive.qml qml/tst_walletsend.qml)
  target_sources(test_bitcoin-qt PRIVATE test_receiverequests.cpp test_wallet_receive_integration.cpp)
  target_sources(test_bitcoin-qt PRIVATE test_activitytimeline.cpp test_wallet_activity_integration.cpp)
  target_sources(test_bitcoin-qt PRIVATE test_activityfilter.cpp)
  target_sources(test_bitcoin-qt PRIVATE test_signverify.cpp test_wallet_address_integration.cpp)
  target_sources(test_bitcoin-qt PRIVATE test_coinselection.cpp test_coinselection_integration.cpp)
  target_sources(test_bitcoin-qt PRIVATE test_psbtdocument.cpp test_psbt_integration.cpp)
  target_sources(test_bitcoin-qt PRIVATE test_psbtexport.cpp test_psbtsigning_integration.cpp)
  target_sources(test_bitcoin-qt PRIVATE test_psbtsubmit_integration.cpp)
  qt6_add_resources(test_bitcoin-qt wallet_psbt_qml_tests PREFIX /test FILES qml/tst_walletpsbt.qml)
  target_sources(test_bitcoin-qt PRIVATE test_transactionhistory.cpp test_wallet_history_integration.cpp)
  target_sources(test_bitcoin-qt PRIVATE test_sendrecipients.cpp test_senddraft_integration.cpp)
  target_sources(test_bitcoin-qt PRIVATE test_feeselection.cpp test_sendpreview.cpp test_sendfees_integration.cpp)
  target_sources(test_bitcoin-qt PRIVATE test_wallet_runtime.cpp test_wallet_catalog.cpp test_wallet_creation.cpp test_wallet_import.cpp test_wallet_security.cpp)
  target_compile_definitions(test_bitcoin-qt PRIVATE QML_WALLET_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/wallet")
  set(QML_HISTORICAL_BINARY_DIR "" CACHE PATH "Explicit pinned v28.2 tools for the designated QML wallet compatibility lane")
  if(QML_HISTORICAL_BINARY_DIR)
    add_test(NAME test_bitcoin-qt-historical-wallet
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/functional/qml_wallet_migration_compat.py
        --configfile=${PROJECT_BINARY_DIR}/test/config.ini
        --historical-binary-dir=${QML_HISTORICAL_BINARY_DIR}
        --require-historical
    )
    set_tests_properties(test_bitcoin-qt-historical-wallet PROPERTIES LABELS "compatibility;historical-wallet")
  endif()
endif()

qt6_add_resources(test_bitcoin-qt qml_tests
  PREFIX /test
  FILES
    qml/tst_blockclock.qml
    qml/tst_globalmenu.qml
    qml/tst_navbutton.qml
    qml/tst_proxylocationinput.qml
    qml/tst_valueinput.qml
)
# See: https://bugreports.qt.io/browse/QTBUG-141858.
get_target_property(warn_flags warn_interface INTERFACE_COMPILE_OPTIONS)
if("-Wtrailing-whitespace=any" IN_LIST warn_flags)
  if(ENABLE_WALLET)
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/qrc_wallet_qml_tests.cpp PROPERTIES COMPILE_OPTIONS -Wno-trailing-whitespace)
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/qrc_wallet_psbt_qml_tests.cpp PROPERTIES COMPILE_OPTIONS -Wno-trailing-whitespace)
  endif()
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/qrc_qml_tests.cpp PROPERTIES COMPILE_OPTIONS -Wno-trailing-whitespace)
endif()
unset(warn_flags)

target_compile_definitions(test_bitcoin-qt
  PRIVATE
    BITCOINQML_NO_TEST_MAIN
    QUICK_TEST_SOURCE_DIR=":/test/qml"
)

target_link_libraries(test_bitcoin-qt
  PRIVATE
    core_interface
    bitcoinqml
    test_util
    bitcoin_node
    Qt6::QuickTest
    Qt6::Test
)

get_target_property(qt_qml_test_lib_type Qt6::Core TYPE)
if(qt_qml_test_lib_type STREQUAL "STATIC_LIBRARY")
  target_link_libraries(test_bitcoin-qt PRIVATE
    Qt6::qquicklayoutsplugin
    Qt6::qtqmlcoreplugin
    Qt6::qtquickcontrols2basicstyleimplplugin
    Qt6::qtquickcontrols2basicstyleplugin
    Qt6::qtquickcontrols2implplugin
    Qt6::qtquickcontrols2plugin
    Qt6::qtquickdialogsplugin
  )
endif()

import_plugins(test_bitcoin-qt)
qt6_import_qml_plugins(test_bitcoin-qt)

add_test(NAME test_bitcoin-qt
  COMMAND test_bitcoin-qt --suite=unit
)

add_test(NAME test_bitcoin-qt-qml
  COMMAND test_bitcoin-qt --suite=qml -platform minimal
)

add_test(NAME test_bitcoin-qt-integration
  COMMAND test_bitcoin-qt --suite=integration -platform minimal
)

if(ENABLE_WALLET)
  add_test(NAME test_bitcoin-qt-wallet-disabled
    COMMAND test_bitcoin-qt --suite=integration -platform minimal
  )
  set_tests_properties(test_bitcoin-qt-wallet-disabled PROPERTIES
    ENVIRONMENT "QML_TEST_DISABLE_WALLET=1;QML_DISABLE_DISK_CACHE=1"
  )
endif()

set_property(
  TEST
    test_bitcoin-qt
    test_bitcoin-qt-qml
  APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
      QT_FATAL_WARNINGS=set:1
)

# Avoid stale compiled QML from another worktree. The minimal platform plugin
# may also report unavailable desktop services while constructing the system
# tray controller, so keep integration warnings visible but non-fatal.
set_property(
  TEST
    test_bitcoin-qt-qml
    test_bitcoin-qt-integration
  APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
      QML_DISABLE_DISK_CACHE=set:1
)

install_binary_component(test_bitcoin-qt INTERNAL)
