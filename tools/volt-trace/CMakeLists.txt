# A developer tool, so it may use exceptions and std::print, which SPEC 7.1
# allows outside the data plane.
add_executable(volt-trace main.cpp)
target_compile_features(volt-trace PRIVATE cxx_std_23)
volt_set_warnings(volt-trace)
volt_set_sanitizers(volt-trace)
volt_set_coverage(volt-trace)
target_link_libraries(volt-trace PRIVATE volt::trace)
