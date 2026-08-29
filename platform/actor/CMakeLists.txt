volt_add_library(actor
    SOURCES
        src/environment_context.cpp
        src/real_environment.cpp
        src/sim_environment.cpp
        src/state_reader.cpp
        src/state_writer.cpp
    PUBLIC_LINK
        volt::core
        volt::memory
        volt::pal_posix
        volt::pal_sim
)

target_compile_options(actor PRIVATE -fno-exceptions)

if(VOLT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
