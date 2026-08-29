cmake_minimum_required(VERSION 3.28)

project(volt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# VOLT uses headers, not C++20 modules. Leaving module scanning enabled makes
# GCC emit -fmodules-ts and -fdeps-format flags into compile_commands.json,
# which clang-tidy rejects as unknown arguments.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(VOLT_BUILD_TESTS "Build the VOLT test suites" ON)
option(VOLT_BUILD_TOOLS "Build the VOLT developer tools" OFF)
option(VOLT_ENABLE_TRACING "Compile in Perfetto-compatible tracing instrumentation" OFF)
option(VOLT_ENABLE_COVERAGE "Instrument the build to measure line coverage" OFF)

set(VOLT_SANITIZER "none" CACHE STRING "Sanitizer to build with: none, asan, ubsan, tsan")
set_property(CACHE VOLT_SANITIZER PROPERTY STRINGS none asan ubsan tsan)
if(NOT VOLT_SANITIZER MATCHES "^(none|asan|ubsan|tsan)$")
    message(FATAL_ERROR "VOLT_SANITIZER must be one of: none, asan, ubsan, tsan (got '${VOLT_SANITIZER}')")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Coverage)
include(Sanitizers)
include(VoltAddLibrary)
include(VoltAddTest)

if(VOLT_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
endif()

add_subdirectory(platform/core)
add_subdirectory(platform/pal)
add_subdirectory(platform/log)

if(VOLT_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(VOLT_BUILD_TESTS)
    add_subdirectory(tests/pal_conformance)
endif()
