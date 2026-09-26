# A downstream project of an installed sextant, as a vcpkg user would write it:
# find_package + sextant::sextant, nothing else. Built by the vcpkg-port CI job
# against the port installed from this tree; runs basic_smoke's main.
cmake_minimum_required(VERSION 3.21)
project(sextant_vcpkg_consumer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(VCPKG_TARGET_TRIPLET MATCHES "-static$")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(sextant 1.1 CONFIG REQUIRED)

add_executable(consumer ../basic_smoke/main.cpp)
target_link_libraries(consumer PRIVATE sextant::sextant)

if(WIN32)
    get_target_property(_sextant_type sextant::sextant TYPE)
    if(_sextant_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET consumer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:consumer> $<TARGET_RUNTIME_DLLS:consumer>
            COMMAND_EXPAND_LISTS)
    endif()
endif()
