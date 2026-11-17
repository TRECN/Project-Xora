# pybind11 — CPython bindings (https://github.com/pybind/pybind11)
# Pinned for reproducible embed builds.
include(FetchContent)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.13.6
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(pybind11)