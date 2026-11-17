add_library(Xora2D STATIC
    src/Core/Application.cpp
    src/Core/Logger.cpp
    src/Core/Paths.cpp
    src/Core/Time.cpp
    src/Platform/Window.cpp
    src/Renderer/BufferLayout.cpp
    src/Renderer/GraphicsContext.cpp
    src/Renderer/GraphicsDevice.cpp
    src/Renderer/OrthographicCamera.cpp
    src/Renderer/Renderer2D.cpp
    src/Renderer/OpenGL/OpenGLBuffer.cpp
    src/Renderer/OpenGL/OpenGLShader.cpp
    src/Renderer/OpenGL/OpenGLTexture2D.cpp
    src/Renderer/OpenGL/OpenGLVertexArray.cpp
    src/Renderer/OpenGL/OpenGLContext.cpp
    src/Renderer/OpenGL/OpenGLRendererAPI.cpp
)

add_library(Xora2D::Xora2D ALIAS Xora2D)

target_include_directories(Xora2D
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party/stb
)

target_compile_features(Xora2D PUBLIC cxx_std_20)

target_compile_definitions(Xora2D PRIVATE XORA2D_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/Resources")

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