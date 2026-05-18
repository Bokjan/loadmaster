cmake_minimum_required(VERSION 3.12)
project(loadmaster
        VERSION 0.5.2
        DESCRIPTION "Workload generator"
        LANGUAGES CXX)

# Sensible defaults
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src)
