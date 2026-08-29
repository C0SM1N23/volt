volt_add_library(log
    SOURCES
        src/format_entry.cpp
        src/log_drain.cpp
        src/log_file_header.cpp
        src/log_ring.cpp
        src/logger.cpp
        src/record_reader.cpp
        src/record_writer.cpp
        src/rotating_file_sink.cpp
    PUBLIC_LINK volt::pal
)

if(VOLT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
