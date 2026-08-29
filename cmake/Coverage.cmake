# Applies coverage instrumentation to a target when VOLT_ENABLE_COVERAGE is on.
function(volt_set_coverage target)
    if(NOT VOLT_ENABLE_COVERAGE)
        return()
    endif()

    # -fprofile-update=atomic is not optional here: VOLT runs tests with eight
    # threads through one counter, and non-atomic updates race and produce
    # negative counts that lcov rejects outright.
    target_compile_options(${target} PRIVATE --coverage -fprofile-update=atomic)
    target_link_options(${target} PRIVATE --coverage)
    target_compile_definitions(${target} PRIVATE VOLT_INSTRUMENTED=1)
endfunction()
