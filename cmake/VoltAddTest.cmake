# Registers a GoogleTest-based test executable for a VOLT module, wired
# into the same C++23 standard, warning set, and sanitizer flags as the
# libraries it exercises.
#
#   volt_add_test(<name>
#     SOURCES <src...>
#     LINK <target...>
#   )
function(volt_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LINK" ${ARGN})

    add_executable(${name} ${ARG_SOURCES})
    target_compile_features(${name} PRIVATE cxx_std_23)
    volt_set_warnings(${name})
    volt_set_sanitizers(${name})
    target_link_libraries(${name} PRIVATE GTest::gtest GTest::gtest_main ${ARG_LINK})

    include(GoogleTest)
    gtest_discover_tests(${name} NO_PRETTY_VALUES)
endfunction()
