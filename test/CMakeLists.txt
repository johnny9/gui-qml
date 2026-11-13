cmake_minimum_required(VERSION 3.22)

# Tests for the QML/Qt6 app

find_package(Qt 6.2 MODULE REQUIRED COMPONENTS Core Gui Network Test Qml Quick QuickTest Widgets)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(NAME bitcoinqml_cleanup_tests
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/test_qml_cleanup.py
)
set_tests_properties(bitcoinqml_cleanup_tests PROPERTIES LABELS unit)

add_test(NAME bitcoinqml_process_checks
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/test_qml_process_checks.py
)

add_test(NAME bitcoinqml_test_directory_checks
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/test_qml_test_directories.py
)

add_executable(bitcoinqml_bootstrap_tests
  test_bootstrap.cpp
  ../qml/backendexecutor.cpp
  ../qml/execution.cpp
  ../qml/quithandler.h
)
target_include_directories(bitcoinqml_bootstrap_tests PRIVATE ..)
target_link_libraries(bitcoinqml_bootstrap_tests PRIVATE Qt6::Widgets Qt6::Quick Qt6::Test sanitize_interface)
add_test(NAME bitcoinqml_bootstrap_tests COMMAND bitcoinqml_bootstrap_tests -platform minimal)
set_tests_properties(bitcoinqml_bootstrap_tests PROPERTIES TIMEOUT 30)

add_executable(bitcoinqml_unit_tests
  test_unit_tests_main.cpp
  test_amountparse.cpp
  test_appmode.cpp
  test_backendexecutor.cpp
  test_backuppolicy.cpp
  test_backuppolicyoperation.cpp
  test_execution.cpp
  test_guipresentation.cpp
  test_atomicsharedptr.cpp
  test_persistenttrees.cpp
  test_core_settings_model.cpp
  test_uipreferences.cpp
  test_flowpresentation.cpp
  test_flowdetailsmodel.cpp
  test_startupeffects.cpp
  test_qtlogsink.cpp
  test_bitcoinaddress.cpp
  test_blockclockdial.cpp
  test_blockclockhistory.cpp
  test_blockclockmodel.cpp
  test_bitcoinuri.cpp
  test_bitcoinurimodel.cpp
  test_qmlfiledialog.cpp
  test_bitcoinamount.cpp
  test_debuglogmodel.cpp
  test_monospaceoutputmodel.cpp
  test_desktoptrayiconcontroller.cpp
  test_desktopwindowbehaviormodel.cpp
  test_peerlistmodel.cpp
  test_peerstatsutil.cpp
  test_qmlbitcoinunits.cpp
  test_imageprovider.cpp
  test_qrimageprovider.cpp
  test_imageexportoperation.cpp
  test_nodemodel.cpp
  test_startupdiagnosticsmodel.cpp
  test_secretinput.cpp
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
  test_display_settings.cpp
  test_qtinfolog.cpp
  test_qtmessagefilter.cpp
  test_rpccommandexecutor.cpp
  test_rpcconsolemodel.cpp
  test_syncprogresstracker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../bitcoin/src/util/chaintype.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/bitcoinuri.cpp
)

if(ENABLE_TEST_AUTOMATION)
  target_sources(bitcoinqml_unit_tests PRIVATE test_testbridge.cpp)
endif()

if(ENABLE_WALLET)
  target_sources(bitcoinqml_unit_tests PRIVATE
    test_activityfilterproxymodel.cpp
    test_addresslistmodel.cpp
    test_bumptransactionmodel.cpp
    test_signverifymessagemodel.cpp
    test_signertransaction.cpp
    test_coinslistmodel.cpp
    test_psbtfee.cpp
    test_psbtqmlmodel.cpp
    test_receiverequesthistorymodel.cpp
    test_receiverequestoperation.cpp
    test_sendrecipientslistmodel.cpp
    test_sendrecipient.cpp
    test_transaction.cpp
    test_transactionactivity.cpp
    test_transactionactivitymodel.cpp
    test_activitydetailmodel.cpp
    test_walletlistmodel.cpp
    test_walletqmlmodel.cpp
    test_walletqmlmodeltransaction.cpp
    test_walletqmlcontroller.cpp
    test_walletlifecycle.cpp
    test_walletopening.cpp
    test_walletsecurity.cpp
    test_walletsend.cpp
    test_walletprepare.cpp
    test_wallettransaction.cpp
    test_walletreceipts.cpp
    test_walletrequests.cpp
    test_walletreservations.cpp
    test_walletbalanceadapter.cpp
    test_walletbalancereader.cpp
  )
endif()

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
    $<TARGET_NAME_IF_EXISTS:bitcoin_wallet>
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
      ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/interface_qml_shutdown.py --case "fatal-*"
  )
  set_tests_properties(bitcoinqml_fatal_shutdown_tests PROPERTIES
    TIMEOUT 180
    ENVIRONMENT "QML_TEST_QT_VERSION=${Qt6_VERSION}"
  )
endif()

qt6_add_resources(QMLTESTS_QRC_CPP ${QML_QRC} qml/bitcoin_qmltests.qrc)

add_executable(bitcoinqml_qmltests
  qml/qml_tests_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/flowpresentation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/monospaceoutputmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/monospaceoutputmodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/bitcoinaddress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/bitcoinaddress.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/uipreferences.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/uipreferences.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/backendexecutor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/execution.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/amountparse.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/bitcoinamount.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/bitcoinunits.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/components/blockclockdial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/controls/linegraph.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/qrimageprovider.cpp
  ${QMLTESTS_QRC_CPP}
)

target_compile_definitions(bitcoinqml_qmltests
  PRIVATE
    BITCOINQML_QML_SOURCE_DIR="qrc:/qml"
    QUICK_TEST_SOURCE_DIR=":/test/qml"
    BITCOINQML_QML_TEST_MOCKS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/mocks/qml"
)

target_link_libraries(bitcoinqml_qmltests
  PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickTest
    Qt6::Test
    Qt6::Svg
    QRencode::QRencode
    sanitize_interface
)

target_include_directories(bitcoinqml_qmltests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../bitcoin/src
    ${PROJECT_BINARY_DIR}/bitcoin/src
)

set(QML_TEST_RENDERING_PLATFORM "minimal" CACHE STRING "Platform used by the QML component and warning tests")
set(qml_test_platform "${QML_TEST_RENDERING_PLATFORM}")
add_test(NAME bitcoinqml_qmltests COMMAND bitcoinqml_qmltests -platform ${qml_test_platform})
# Qt 6.2 has no QTest::failOnWarning(); leave this check disabled there too,
# matching the component runner's version guard.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.3)
  # console.warn() does not emit QQmlEngine::warnings. Also reject messages
  # printed outside a test function, before failOnWarning() is installed.
  set_tests_properties(bitcoinqml_qmltests PROPERTIES
    FAIL_REGULAR_EXPRESSION "(QWARN|QCRITICAL)[^\r\n]*qml:|\\[(warning|critical)\\] qml:")
endif()

add_test(NAME bitcoinqml_qmlwarning_tests
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/qml/test_warnings.py
    $<TARGET_FILE:bitcoinqml_qmltests> ${Qt6_VERSION} ${qml_test_platform} ${PROJECT_BINARY_DIR}
)
set_tests_properties(bitcoinqml_qmlwarning_tests PROPERTIES TIMEOUT 120 LABELS qml
  ENVIRONMENT "QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")

set(gui_test_environment "QT_QPA_PLATFORM=minimal;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
set_tests_properties(bitcoinqml_unit_tests bitcoinqml_bootstrap_tests bitcoinqml_fatal_exception_tests bitcoinqml_test_directory_checks
  bitcoinqml_process_checks PROPERTIES LABELS unit)
set_tests_properties(bitcoinqml_qmltests PROPERTIES LABELS qml)
set_tests_properties(bitcoinqml_unit_tests bitcoinqml_bootstrap_tests bitcoinqml_qmltests PROPERTIES
  ENVIRONMENT "${gui_test_environment}")
set_property(TEST bitcoinqml_qmltests PROPERTY ENVIRONMENT
  "QT_QPA_PLATFORM=${qml_test_platform};QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
# Include severity and category so the console warning check can distinguish
# console.warn() from console.log() during QML loading and teardown.
set_property(TEST bitcoinqml_qmltests APPEND PROPERTY ENVIRONMENT
  "QT_MESSAGE_PATTERN=[%{type}] %{category}: %{message}")

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
