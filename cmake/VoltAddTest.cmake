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
    volt_set_coverage(${name})
    target_link_libraries(${name} PRIVATE GTest::gtest GTest::gtest_main ${ARG_LINK})

    include(GoogleTest)
    # The target name prefixes every case so `ctest -R <target>` selects one
    # module's tests. Without it the ctest name is whatever GoogleTest derived
    # from the fixture, which says nothing about where the test lives.
    #
    # Generous next to a suite that should finish in milliseconds, but it turns
    # a broken blocking call into a failure instead of a run that never ends:
    # a test waiting on a timer or a socket that a defect left unarmed would
    # otherwise hang the whole pipeline rather than report the defect.
    gtest_discover_tests(${name}
        NO_PRETTY_VALUES
        TEST_PREFIX "${name}."
        PROPERTIES TIMEOUT 60
    )
endfunction()
