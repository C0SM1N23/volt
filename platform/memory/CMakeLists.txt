volt_add_library(memory
    SOURCES
        src/allocation_hooks.cpp
        src/allocation_tracker.cpp
        src/arena.cpp
        src/frame_scope.cpp
        src/no_alloc_scope.cpp
    PUBLIC_LINK volt::core volt::trace
)

# std::stacktrace lives in a separate library in libstdc++, and only the debug
# build uses it: that is where a guard violation prints where it came from
# before ending the process. Release builds count the violation instead.
target_link_libraries(memory PUBLIC $<$<CONFIG:Debug>:stdc++exp>)

if(VOLT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
