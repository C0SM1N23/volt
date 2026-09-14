find_package(yaml-cpp REQUIRED)

# A developer tool, so it may use exceptions and std::print, which SPEC 7.1
# allows outside the data plane.
add_executable(tt_schedule_gen main.cpp)
target_compile_features(tt_schedule_gen PRIVATE cxx_std_23)
volt_set_warnings(tt_schedule_gen)
volt_set_sanitizers(tt_schedule_gen)
volt_set_coverage(tt_schedule_gen)
target_link_libraries(tt_schedule_gen PRIVATE volt::sched yaml-cpp::yaml-cpp)
