# Tests that run the production application with a real regtest node.
add_library(bitcoinqml_test_application STATIC fixture/test_application.cpp)
target_include_directories(bitcoinqml_test_application PUBLIC
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/bitcoin/src
  ${PROJECT_BINARY_DIR}/bitcoin/src
)
target_link_libraries(bitcoinqml_test_application PUBLIC bitcoinqml bitcoinqt Qt6::Test)

add_executable(bitcoinqml_integration_tests
  runner/test_main.cpp
  runner/test_cases.cpp
  runner/test_process.cpp
  test_lifecycle.cpp
  test_navigation.cpp
  test_node.cpp
)
target_link_libraries(bitcoinqml_integration_tests PRIVATE bitcoinqml_test_application)
add_test(NAME bitcoinqml_integration_tests COMMAND bitcoinqml_integration_tests)

if(qt_lib_type STREQUAL "STATIC_LIBRARY")
  qt6_import_plugins(bitcoinqml_integration_tests INCLUDE Qt6::QMinimalIntegrationPlugin Qt6::QSvgPlugin Qt6::QSvgIconPlugin)
  target_link_libraries(bitcoinqml_integration_tests PRIVATE Qt6::LabsPlatformplugin Qt6::qtquickdialogsplugin)
  if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    target_link_libraries(bitcoinqml_integration_tests PRIVATE Qt6::qtqmlcoreplugin)
  else()
    target_link_libraries(bitcoinqml_integration_tests PRIVATE Qt6::qmlsettingsplugin)
  endif()
  qt6_import_plugins(bitcoin-core-app INCLUDE Qt6::QMinimalIntegrationPlugin)
endif()

set(functional_test_dir "${PROJECT_SOURCE_DIR}/test/functional")
set(integration_tests bitcoinqml_integration_tests)
if(ENABLE_TEST_AUTOMATION)
  add_test(NAME bitcoin-core-app_lifecycle COMMAND ${Python3_EXECUTABLE}
    ${functional_test_dir}/gui_process_check.py
    $<TARGET_FILE:bitcoin-core-app> ${CMAKE_CURRENT_BINARY_DIR}/lifecycle.json
    --qt-version ${Qt6_VERSION})
  list(APPEND integration_tests bitcoin-core-app_lifecycle)
endif()
if(ENABLE_TEST_AUTOMATION AND UNIX)
  add_test(NAME bitcoinqml_configured_datadir_tests
    COMMAND ${CMAKE_COMMAND} -E env BITCOIN_CORE_APP=$<TARGET_FILE:bitcoin-core-app>
      ${Python3_EXECUTABLE} ${functional_test_dir}/qml_test_configured_datadir.py)
  add_test(NAME bitcoinqml_fatal_shutdown_tests
    COMMAND ${CMAKE_COMMAND} -E env BITCOIN_CORE_APP=$<TARGET_FILE:bitcoin-core-app>
      ${Python3_EXECUTABLE} ${functional_test_dir}/qml_test_fatal_shutdown.py)
  list(APPEND integration_tests bitcoinqml_configured_datadir_tests bitcoinqml_fatal_shutdown_tests)
endif()
if(ENABLE_TEST_AUTOMATION)
  add_test(NAME bitcoinqml_node_only
    COMMAND ${CMAKE_COMMAND} -E env BITCOIN_CORE_APP=$<TARGET_FILE:bitcoin-core-app> QML_TEST_CHECK_EXIT=1
      ${Python3_EXECUTABLE} ${functional_test_dir}/qml_test_disablewallet_boot.py --node-only)
  list(APPEND integration_tests bitcoinqml_node_only)
endif()

set_tests_properties(${integration_tests} PROPERTIES
  LABELS integration
  TIMEOUT 120
  ENVIRONMENT "${gui_test_environment}"
)
set_tests_properties(bitcoinqml_integration_tests PROPERTIES TIMEOUT 810)
if(ENABLE_TEST_AUTOMATION AND UNIX)
  set_tests_properties(bitcoinqml_configured_datadir_tests bitcoinqml_fatal_shutdown_tests PROPERTIES TIMEOUT 180)
endif()
add_custom_target(bitcoinqml_integration
  DEPENDS bitcoin-core-app bitcoinqml_integration_tests)
