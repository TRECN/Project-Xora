add_library(Xora2D STATIC
    src/Core/Application.cpp
    src/Core/Logger.cpp
    src/Core/Time.cpp
    src/Platform/Window.cpp
    src/Renderer/GraphicsContext.cpp
    src/Renderer/GraphicsDevice.cpp
    src/Renderer/OpenGL/OpenGLContext.cpp
    src/Renderer/OpenGL/OpenGLRendererAPI.cpp
)

add_library(Xora2D::Xora2D ALIAS Xora2D)

target_include_directories(Xora2D
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(Xora2D PUBLIC cxx_std_20)

find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(Xora2D PRIVATE SDL3::SDL3 glad::glad)

find_path(XORA2D_GLM_INCLUDE_DIR glm/glm.hpp
    PATHS
        /opt/homebrew/include
        /usr/local/include
)
if(NOT XORA2D_GLM_INCLUDE_DIR)
    message(FATAL_ERROR "GLM headers not found. Install with: brew install glm")
endif()
target_include_directories(Xora2D PUBLIC ${XORA2D_GLM_INCLUDE_DIR})

if(APPLE)
    find_library(XORA2D_OPENGL_FRAMEWORK OpenGL REQUIRED)
    target_link_libraries(Xora2D PRIVATE ${XORA2D_OPENGL_FRAMEWORK})
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(Xora2D PRIVATE -Wall -Wextra -Wpedantic)
endif()