# Creates a VOLT module library: wires in the project's C++23 standard,
# warning set, and sanitizer flags, and exposes it under the volt::
# namespace used by every consumer in the tree.
#
#   volt_add_library(<name>
#     [INTERFACE]
#     SOURCES <src...>
#     PUBLIC_LINK <target...>
#     PRIVATE_LINK <target...>
#   )
function(volt_add_library name)
    cmake_parse_arguments(ARG "INTERFACE" "" "SOURCES;PUBLIC_LINK;PRIVATE_LINK" ${ARGN})

    if(ARG_INTERFACE)
        add_library(${name} INTERFACE)
        target_compile_features(${name} INTERFACE cxx_std_23)
        target_include_directories(${name} INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        if(ARG_PUBLIC_LINK)
            target_link_libraries(${name} INTERFACE ${ARG_PUBLIC_LINK})
        endif()
    else()
        add_library(${name} STATIC ${ARG_SOURCES})
        target_compile_features(${name} PUBLIC cxx_std_23)
        target_include_directories(${name} PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        volt_set_warnings(${name})
        volt_set_sanitizers(${name})
        if(ARG_PUBLIC_LINK)
            target_link_libraries(${name} PUBLIC ${ARG_PUBLIC_LINK})
        endif()
        if(ARG_PRIVATE_LINK)
            target_link_libraries(${name} PRIVATE ${ARG_PRIVATE_LINK})
        endif()
    endif()

    add_library(volt::${name} ALIAS ${name})
endfunction()
