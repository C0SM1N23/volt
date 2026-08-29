find_package(rapidcheck CONFIG REQUIRED)

volt_add_test(actor_tests
    SOURCES
        actor_test.cpp
        state_property_test.cpp
    LINK
        volt::actor
        rapidcheck
)

# Ubuntu's RapidCheck package instantiates a C++23-deprecated standard helper.
# The suppression is confined to the target that includes the third-party templates.
target_compile_options(actor_tests PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wno-deprecated-declarations>
)
