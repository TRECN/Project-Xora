# yaml-cpp 0.8.0 — fetched at configure time (pinned tag).
# https://github.com/jbeder/yaml-cpp/releases/tag/0.8.0

include(FetchContent)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0
    GIT_SHALLOW TRUE
)

set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# yaml-cpp 0.8.0 still declares an ancient cmake_minimum_required; allow it under CMake 3.24+.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_MakeAvailable(yaml-cpp)

if(NOT TARGET yaml-cpp::yaml-cpp AND TARGET yaml-cpp)
    add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
endif()