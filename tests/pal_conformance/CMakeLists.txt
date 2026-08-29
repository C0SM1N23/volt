volt_add_test(pal_conformance
    SOURCES
        posix_conformance.cpp
        sim_conformance.cpp
        sim_determinism_test.cpp
        sim_network_model_test.cpp
    LINK volt::pal_posix volt::pal_sim
)

target_include_directories(pal_conformance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
