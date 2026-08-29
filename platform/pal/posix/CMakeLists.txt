volt_add_library(pal_posix
    SOURCES
        src/file_descriptor.cpp
        src/posix_clock.cpp
        src/posix_error.cpp
        src/posix_file.cpp
        src/posix_platform.cpp
        src/posix_process.cpp
        src/posix_shared_memory.cpp
        src/posix_socket.cpp
        src/posix_thread.cpp
        src/posix_timer.cpp
        src/posix_watchdog_device.cpp
        src/priority_inheriting_mutex.cpp
    PUBLIC_LINK volt::pal
)

# The implementation headers stay private: only PosixPlatform, PosixClock and
# PriorityInheritingMutex are meant to be named from outside this backend.
target_include_directories(pal_posix PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# shm_open and the POSIX real-time calls live in librt on older glibc, and
# pthread_setname_np needs the threads library.
find_package(Threads REQUIRED)
target_link_libraries(pal_posix PUBLIC Threads::Threads)
