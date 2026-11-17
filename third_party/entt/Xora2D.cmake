# EnTT v3.13.2 — vendored from https://github.com/skypjack/entt/releases/tag/v3.13.2
# The full upstream CMake project is not added; Xora2D only needs the headers.

add_library(XoraEnTT INTERFACE)
add_library(EnTT::EnTT ALIAS XoraEnTT)

target_include_directories(XoraEnTT
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src
)