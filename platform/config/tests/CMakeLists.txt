volt_add_test(config_tests
    SOURCES config_test.cpp
    LINK volt::config yaml-cpp::yaml-cpp
)

target_compile_definitions(config_tests PRIVATE
    VOLT_CONFIG_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data"
    VOLT_CONFIG_EXAMPLE="${CMAKE_SOURCE_DIR}/config/node_a.yaml"
    VOLT_CLUSTER_EXAMPLE="${CMAKE_SOURCE_DIR}/config/cluster.yaml"
)
