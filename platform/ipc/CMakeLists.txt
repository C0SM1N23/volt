volt_add_library(ipc
    SOURCES
        src/control_channel.cpp
        src/free_list.cpp
        src/index_ring.cpp
        src/topic_layout.cpp
        src/topic_state.cpp
    PUBLIC_LINK volt::pal
)

if(VOLT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
