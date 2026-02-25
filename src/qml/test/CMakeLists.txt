cmake_minimum_required(VERSION 3.22)

# Tests for the QML/Qt6 app

find_package(Qt 6.2 MODULE REQUIRED COMPONENTS Core Test Qml Quick QuickTest)

add_executable(bitcoinqml_unit_tests
  test_unit_tests_main.cpp
  test_bitcoinamount.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/bitcoinamount.cpp
)

target_compile_definitions(bitcoinqml_unit_tests
  PRIVATE
    BITCOINQML_NO_TEST_MAIN
)

target_include_directories(bitcoinqml_unit_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../bitcoin/src
)

target_link_libraries(bitcoinqml_unit_tests
  PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME bitcoinqml_unit_tests COMMAND bitcoinqml_unit_tests)

add_executable(bitcoinqml_qmltests
  qml/qml_tests_main.cpp
)

target_compile_definitions(bitcoinqml_qmltests
  PRIVATE
    BITCOINQML_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../qml"
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

target_link_libraries(bitcoinqml_qmltests
  PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickTest
)

add_test(NAME bitcoinqml_qmltests COMMAND bitcoinqml_qmltests -platform offscreen)
