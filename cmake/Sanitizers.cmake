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

    # AddressSanitizer and ThreadSanitizer define the allocation operators in
    # their own runtime, and the linker takes those instead of the ones in
    # platform/memory. Everything that counts allocations is inert in such a
    # build, and has to say so rather than report a zero it did not measure.
    if(VOLT_SANITIZER STREQUAL "asan" OR VOLT_SANITIZER STREQUAL "tsan")
        target_compile_definitions(${target} PRIVATE VOLT_SANITIZER_OWNS_HEAP=1)
    endif()
endfunction()
