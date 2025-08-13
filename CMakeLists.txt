cmake_minimum_required(VERSION 3.16)
project(FinWorkOptimizer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FWO_WITH_SQLITE "Enable SQLite3 persistence" OFF)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
    message(FATAL_ERROR "GCC 11+ required")
  endif()
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
    message(FATAL_ERROR "Clang 13+ required")
  endif()
endif()

if(FWO_WITH_SQLITE)
  find_package(SQLite3)
  if(SQLite3_FOUND)
    add_compile_definitions(FWO_WITH_SQLITE)
    message(STATUS "SQLite3 enabled")
  endif()
endif()

add_executable(FinWorkOptimizer
  src/main.cpp
  src/server/RestServer.cpp
)

add_executable(fwo_rest_server
  src/server/standalone_main.cpp
  src/server/RestServer.cpp
)

target_include_directories(FinWorkOptimizer PRIVATE include)
target_include_directories(fwo_rest_server PRIVATE include)

if (SQLite3_FOUND)
  target_link_libraries(FinWorkOptimizer PRIVATE SQLite::SQLite3)
  target_link_libraries(fwo_rest_server PRIVATE SQLite::SQLite3)
endif()

enable_testing()
add_executable(finance_tests tests/finance_tests.cpp)
target_include_directories(finance_tests PRIVATE include)
add_test(NAME FinanceTests COMMAND finance_tests)
