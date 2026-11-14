cmake_minimum_required(VERSION 3.22)

# Tests for the QML/Qt6 app

find_package(Qt 6.2 MODULE REQUIRED COMPONENTS Core Gui Network Test Qml Quick QuickTest Widgets)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(NAME bitcoinqml_process_checks
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/test_qml_process_checks.py
)
add_test(NAME bitcoinqml_dev_resources
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_qml_dev_resources.py
)

add_executable(bitcoinqml_bootstrap_tests
  test_bootstrap.cpp
  ../qml/backendexecutor.cpp
  ../qml/quithandler.h
)
target_include_directories(bitcoinqml_bootstrap_tests PRIVATE ..)
target_link_libraries(bitcoinqml_bootstrap_tests PRIVATE Qt6::Widgets Qt6::Quick Qt6::Test)
add_test(NAME bitcoinqml_bootstrap_tests COMMAND bitcoinqml_bootstrap_tests -platform minimal)
set_tests_properties(bitcoinqml_bootstrap_tests PROPERTIES TIMEOUT 30)

add_executable(bitcoinqml_unit_tests
  test_unit_tests_main.cpp
  test_backendexecutor.cpp
  test_bitcoinaddress.cpp
  test_blockclockdial.cpp
  test_blockclockhistory.cpp
  test_blockclockmodel.cpp
  test_bitcoinuri.cpp
  test_qmlfiledialog.cpp
  test_bitcoinamount.cpp
  test_debuglogmodel.cpp
  test_desktoptrayiconcontroller.cpp
  test_desktopwindowbehaviormodel.cpp
  test_peerlistmodel.cpp
  test_peerstatsutil.cpp
  test_psbtqmlmodel.cpp
  test_walletqmlmodel.cpp
  test_walletqmlmodeltransaction.cpp
  test_qmlbitcoinunits.cpp
  test_imageprovider.cpp
  test_nodemodel.cpp
  test_networkstyle.cpp
  test_networkstatusmodel.cpp
  test_networktraffictower.cpp
  test_qmlinitexecutor_api.cpp
  test_shutdownproducers.cpp
  test_guiargs.cpp
  test_options_model.cpp
  test_async_options.cpp
  test_onboarding_storage.cpp
  test_onboardingasync.cpp
  test_banlistmodel.cpp
  test_walletlistmodel.cpp
  test_bumptransactionmodel.cpp
  test_walletqmlcontroller.cpp
  test_display_settings.cpp
  test_addresslistmodel.cpp
  test_receiverequesthistorymodel.cpp
  test_transaction.cpp
  test_transactionactivity.cpp
  test_transactionactivitymodel.cpp
  test_activityfilterproxymodel.cpp
  test_qtinfolog.cpp
  test_qtmessagefilter.cpp
  test_rpccommandexecutor.cpp
  test_rpcconsolemodel.cpp
  test_sendrecipientslistmodel.cpp
  test_syncprogresstracker.cpp
  test_coinslistmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../bitcoin/src/util/chaintype.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/bitcoinuri.cpp
)

target_compile_definitions(bitcoinqml_unit_tests
  PRIVATE
    BITCOINQML_NO_TEST_MAIN
)

target_include_directories(bitcoinqml_unit_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../bitcoin/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../bitcoin/src/univalue/include
    ${CMAKE_CURRENT_BINARY_DIR}/../bitcoin/src
)

target_link_libraries(bitcoinqml_unit_tests
  PRIVATE
    bitcoinqml
    bitcoin_wallet
    bitcoin_common
    bitcoin_util
    bitcoin_node
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Network
    Qt6::Test
    Qt6::Widgets
)

add_test(NAME bitcoinqml_unit_tests COMMAND bitcoinqml_unit_tests -platform minimal)

add_test(NAME bitcoinqml_fatal_exception_tests
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/qml_test_fatal_exceptions.py
    $<TARGET_FILE:bitcoinqml_unit_tests>
)
set_tests_properties(bitcoinqml_fatal_exception_tests PROPERTIES TIMEOUT 120)

if(ENABLE_TEST_AUTOMATION AND UNIX)
  add_test(NAME bitcoinqml_configured_datadir_tests
    COMMAND ${CMAKE_COMMAND} -E env BITCOIN_CORE_APP=$<TARGET_FILE:bitcoin-core-app>
      ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/qml_test_configured_datadir.py
  )
  set_tests_properties(bitcoinqml_configured_datadir_tests PROPERTIES
    TIMEOUT 180
    ENVIRONMENT "QML_TEST_QT_VERSION=${Qt6_VERSION}"
  )

  add_test(NAME bitcoinqml_fatal_shutdown_tests
    COMMAND ${CMAKE_COMMAND} -E env BITCOIN_CORE_APP=$<TARGET_FILE:bitcoin-core-app>
      ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/qml_test_fatal_shutdown.py
  )
  set_tests_properties(bitcoinqml_fatal_shutdown_tests PROPERTIES
    TIMEOUT 180
    ENVIRONMENT "QML_TEST_QT_VERSION=${Qt6_VERSION}"
  )
endif()

option(QML_TEST_DEV_MODE "Load QML tests and components from a live source mirror" OFF)
set(qml_test_manifests ${QML_QRC} ${CMAKE_CURRENT_SOURCE_DIR}/qml/bitcoin_qmltests.qrc)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${qml_test_manifests})
set(qml_test_source_dir "qrc:/qml")
set(qml_test_input_dir ":/test/qml")
if(QML_TEST_DEV_MODE)
  set(qml_test_mirror "${CMAKE_CURRENT_BINARY_DIR}/qml-dev")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/qml/prepare_dev_resources.py)
  execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/qml/prepare_dev_resources.py
      --output ${qml_test_mirror} ${qml_test_manifests}
    COMMAND_ERROR_IS_FATAL ANY
  )
  set(qml_test_source_dir "${qml_test_mirror}/qml")
  set(qml_test_input_dir "${qml_test_mirror}/test/qml")
  qt6_add_resources(QMLTESTS_QRC_CPP ${qml_test_mirror}/assets.qrc)
else()
  qt6_add_resources(QMLTESTS_QRC_CPP ${qml_test_manifests})
endif()

add_executable(bitcoinqml_qmltests
  qml/qml_tests_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/backendexecutor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/bitcoinamount.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/bitcoinunits.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/components/blockclockdial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/controls/linegraph.cpp
  ${QMLTESTS_QRC_CPP}
)

target_compile_definitions(bitcoinqml_qmltests
  PRIVATE
    BITCOINQML_QML_SOURCE_DIR="${qml_test_source_dir}"
    QUICK_TEST_SOURCE_DIR="${qml_test_input_dir}"
    BITCOINQML_QML_TEST_MOCKS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/mocks/qml"
)

target_link_libraries(bitcoinqml_qmltests
  PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickTest
    Qt6::Svg
)

target_include_directories(bitcoinqml_qmltests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../bitcoin/src
)

# Use the resource manifest as the authoritative list in both modes. Each file
# gets its own process, so CTest can select and schedule tests independently.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/qml/bitcoin_qmltests.qrc qml_test_entries
  REGEX "<file>tst_.*[.]qml</file>")
set(qml_test_names)
foreach(entry IN LISTS qml_test_entries)
  string(REGEX REPLACE ".*<file>([^<]+)</file>.*" "\\1" filename "${entry}")
  string(REGEX REPLACE "^tst_|[.]qml$" "" test_name "${filename}")
  set(test_name "bitcoinqml_qml_${test_name}")
  add_test(NAME ${test_name} COMMAND bitcoinqml_qmltests -platform minimal
    -input "${qml_test_input_dir}/${filename}")
  list(APPEND qml_test_names ${test_name})
endforeach()
# Seed the longest suites so clean builds do not need timing history to schedule
# them first. These are relative scheduling hints, not timeouts or assertions.
set_tests_properties(bitcoinqml_qml_sendcreate bitcoinqml_qml_requestpayment PROPERTIES COST 20)
set_tests_properties(bitcoinqml_qml_activitylist PROPERTIES COST 16)

set(gui_test_environment "QT_QPA_PLATFORM=minimal;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
set_tests_properties(bitcoinqml_unit_tests bitcoinqml_bootstrap_tests bitcoinqml_fatal_exception_tests
  bitcoinqml_process_checks bitcoinqml_dev_resources PROPERTIES LABELS unit)
set_tests_properties(${qml_test_names} PROPERTIES LABELS qml TIMEOUT 120)
set_tests_properties(bitcoinqml_unit_tests bitcoinqml_bootstrap_tests ${qml_test_names} PROPERTIES
  ENVIRONMENT "${gui_test_environment}")

get_target_property(qt_lib_type Qt6::Core TYPE)
if(qt_lib_type STREQUAL "STATIC_LIBRARY")
  qt6_import_plugins(bitcoinqml_bootstrap_tests INCLUDE Qt6::QMinimalIntegrationPlugin)
  qt6_import_plugins(bitcoinqml_unit_tests INCLUDE Qt6::QMinimalIntegrationPlugin)
  qt6_import_qml_plugins(bitcoinqml_unit_tests)
  qt6_import_plugins(bitcoinqml_qmltests INCLUDE Qt6::QMinimalIntegrationPlugin Qt6::QSvgPlugin Qt6::QSvgIconPlugin)
  qt6_import_qml_plugins(bitcoinqml_qmltests)
  target_link_libraries(bitcoinqml_qmltests PRIVATE
    Qt6::LabsPlatformplugin
    Qt6::qtquickdialogsplugin
  )
  if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    target_link_libraries(bitcoinqml_qmltests PRIVATE Qt6::qtqmlcoreplugin)
  else()
    target_link_libraries(bitcoinqml_qmltests PRIVATE Qt6::qmlsettingsplugin)
  endif()
endif()

add_subdirectory(integration)

if(ENABLE_TEST_AUTOMATION AND UNIX)
  add_executable(bitcoinqml_startup_error_tests test_startup_errors.cpp)
  target_link_libraries(bitcoinqml_startup_error_tests PRIVATE bitcoinqml bitcoinqt Qt6::Test)
  target_include_directories(bitcoinqml_startup_error_tests PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/bitcoin/src ${PROJECT_BINARY_DIR}/bitcoin/src)
  add_test(NAME bitcoinqml_startup_error_tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/qml_test_startup_errors.py $<TARGET_FILE:bitcoinqml_startup_error_tests>)
  set_tests_properties(bitcoinqml_startup_error_tests PROPERTIES TIMEOUT 150)
  if(qt_lib_type STREQUAL "STATIC_LIBRARY")
    qt6_import_plugins(bitcoinqml_startup_error_tests INCLUDE Qt6::QMinimalIntegrationPlugin Qt6::QSvgPlugin Qt6::QSvgIconPlugin)
    target_link_libraries(bitcoinqml_startup_error_tests PRIVATE Qt6::LabsPlatformplugin Qt6::qtquickdialogsplugin)
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
      target_link_libraries(bitcoinqml_startup_error_tests PRIVATE Qt6::qtqmlcoreplugin)
    else()
      target_link_libraries(bitcoinqml_startup_error_tests PRIVATE Qt6::qmlsettingsplugin)
    endif()
  endif()
endif()
