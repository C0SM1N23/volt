# Applies the project-wide warning set (SPEC 7.1) to a target. A warning
# is a build failure here, not a suggestion.
function(volt_set_warnings target)
    set(volt_warning_flags
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wcast-qual
        -Wold-style-cast
        -Wnon-virtual-dtor
        -Werror
    )

    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:${volt_warning_flags}>
    )
endfunction()
