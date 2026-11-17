cmake_minimum_required(VERSION 3.24)

project(Xora2D
    VERSION 0.1.0
    DESCRIPTION "Xora2D 2D game engine"
    LANGUAGES CXX
)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

add_subdirectory(third_party/glad)
include(${CMAKE_SOURCE_DIR}/third_party/entt/Xora2D.cmake)
include(${CMAKE_SOURCE_DIR}/third_party/box2d/Xora2D.cmake)
add_subdirectory(Engine)
add_subdirectory(Sandbox)
add_subdirectory(Tests)
