# A developer tool, so it may use exceptions and std::print, which SPEC 7.1
# allows outside the data plane.
add_executable(volt-logdec main.cpp)
target_compile_features(volt-logdec PRIVATE cxx_std_23)
volt_set_warnings(volt-logdec)
volt_set_sanitizers(volt-logdec)
target_link_libraries(volt-logdec PRIVATE volt::log)
