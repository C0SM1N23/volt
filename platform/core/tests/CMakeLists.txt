volt_add_test(core_tests
    SOURCES
        duration_test.cpp
        endian_test.cpp
        error_test.cpp
        hash_test.cpp
        semantic_version_test.cpp
        span_utils_test.cpp
        timestamp_test.cpp
        types_test.cpp
    LINK volt::core
)
