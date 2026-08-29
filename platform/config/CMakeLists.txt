find_package(yaml-cpp REQUIRED)

volt_add_library(config
    SOURCES
        src/config_hash.cpp
        src/config_loader.cpp
        src/config_reloader.cpp
        src/load_report.cpp
        src/schema.cpp
    PUBLIC_LINK volt::core
    PRIVATE_LINK yaml-cpp::yaml-cpp
)

if(VOLT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
