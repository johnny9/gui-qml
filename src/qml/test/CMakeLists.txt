cmake_minimum_required(VERSION 3.22)

# Tests for the QML/Qt6 app

find_package(Qt 6.2 MODULE REQUIRED COMPONENTS Test Core)

add_executable(bitcoinqml_tests
  test_bitcoinamount.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/bitcoinamount.cpp
)

target_include_directories(bitcoinqml_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../bitcoin/src
)

target_link_libraries(bitcoinqml_tests
  PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME bitcoinqml_tests COMMAND bitcoinqml_tests)

