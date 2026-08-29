volt_add_library(memory
    SOURCES
        src/arena.cpp
        src/frame_scope.cpp
    PUBLIC_LINK volt::core
)

if(VOLT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
