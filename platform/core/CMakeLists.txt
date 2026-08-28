volt_add_library(core INTERFACE)

if(VOLT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
