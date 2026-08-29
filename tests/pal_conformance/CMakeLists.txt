volt_add_test(pal_conformance
    SOURCES posix_conformance.cpp
    LINK volt::pal_posix
)

target_include_directories(pal_conformance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
