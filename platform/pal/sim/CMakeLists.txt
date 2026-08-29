volt_add_library(pal_sim
    SOURCES
        src/sim_event_log.cpp
        src/sim_file.cpp
        src/sim_network.cpp
        src/sim_platform.cpp
        src/sim_process.cpp
        src/sim_scheduler.cpp
        src/sim_socket.cpp
        src/sim_stream_listener.cpp
        src/sim_stream_socket.cpp
        src/sim_thread.cpp
        src/sim_timer.cpp
        src/sim_watchdog_device.cpp
        src/sim_world.cpp
    PUBLIC_LINK volt::pal
)

# Only SimPlatform and its configuration are meant to be named from outside.
target_include_directories(pal_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
