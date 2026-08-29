# Applies the sanitizer selected via VOLT_SANITIZER to a target.
# VOLT_SANITIZER itself is validated once in the root CMakeLists.txt.
function(volt_set_sanitizers target)
    if(VOLT_SANITIZER STREQUAL "none")
        return()
    elseif(VOLT_SANITIZER STREQUAL "asan")
        set(sanitize_flags -fsanitize=address -fno-omit-frame-pointer)
    elseif(VOLT_SANITIZER STREQUAL "ubsan")
        set(sanitize_flags -fsanitize=undefined -fno-omit-frame-pointer)
    elseif(VOLT_SANITIZER STREQUAL "tsan")
        set(sanitize_flags -fsanitize=thread)
    else()
        message(FATAL_ERROR "Unknown VOLT_SANITIZER value: ${VOLT_SANITIZER}")
    endif()

    target_compile_options(${target} PRIVATE ${sanitize_flags} -g)
    target_link_options(${target} PRIVATE ${sanitize_flags})

    # Lets a test tell that it is running under instrumentation. A timing
    # measurement taken under a sanitizer says nothing about the code, so the
    # benchmarks report that they were skipped rather than assert on a number
    # the tooling invented. Coverage sets the same definition.
    target_compile_definitions(${target} PRIVATE VOLT_INSTRUMENTED=1)
endfunction()
