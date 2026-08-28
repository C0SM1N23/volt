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
endfunction()
