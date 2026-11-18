# Dear ImGui (docking branch) — pinned commit for SDL3 + OpenGL3 editor layout.
# https://github.com/ocornut/imgui/tree/docking @ 9dbcb2f1c

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 9dbcb2f1cc0e8a3ffac51d7f9d28efdb228db14b
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(imgui)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})

target_include_directories(imgui
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="imconfig_xora2d.h")

find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(imgui PUBLIC SDL3::SDL3 glad::glad)

target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(imgui PRIVATE -w)
endif()

add_library(imgui::imgui ALIAS imgui)