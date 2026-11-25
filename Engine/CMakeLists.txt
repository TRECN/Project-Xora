add_library(Xora2D STATIC
    src/Core/Application.cpp
    src/Core/Logger.cpp
    src/Core/Paths.cpp
    src/Core/Time.cpp
    src/Core/Version.cpp
    src/Core/UUID.cpp
    src/Assets/AssetHandle.cpp
    src/Assets/AssetPaths.cpp
    src/Assets/AssetRegistry.cpp
    src/Assets/AssetRegistrySerializer.cpp
    src/Assets/AssetManager.cpp
    src/Animation/AnimationClipAsset.cpp
    src/Animation/AnimationController.cpp
    src/Animation/AnimationPlayback.cpp
    src/Animation/SpriteSheetLayout.cpp
    src/Scene/SceneAnimationSystem.cpp
    src/Input/Input.cpp
    src/Input/InputCodes.cpp
    src/Input/InputManager.cpp
    src/Input/InputMapping.cpp
    src/Input/InputMappingSerializer.cpp
    src/Audio/AudioClip.cpp
    src/Audio/AudioEngine.cpp
    src/Audio/NullAudioBackend.cpp
    src/Audio/MiniaudioPlatform.cpp
    src/Scene/SceneAudioSystem.cpp
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
    src/Renderer/OpenGL/OpenGLColorRenderTarget.cpp
    src/Scene/Components.cpp
    src/Scene/Entity.cpp
    src/Scene/EntityTemplateInst.cpp
    src/Scene/Scene.cpp
    src/Physics/PhysicsUtils.cpp
    src/Physics/PhysicsWorld2D.cpp
    src/Serialization/SceneSerializer.cpp
    src/Project/ProjectConfig.cpp
    src/Project/ProjectSerializer.cpp
    src/Project/Project.cpp
    src/Project/ProjectTemplates.cpp
    src/Prefab/Prefab.cpp
    src/Prefab/PrefabSerializer.cpp
    src/Prefab/PrefabUtility.cpp
    src/Tilemap/Tileset.cpp
    src/Tilemap/TilemapComponent.cpp
    src/Tilemap/TilemapEditing.cpp
    src/Tilemap/TilemapRendering.cpp
    src/Tilemap/TilemapSerializer.cpp
    src/Scripting/PythonScriptAsset.cpp
    src/Scripting/ScenePythonScriptSystem.cpp
    src/Particles/ParticleTypes.cpp
    src/Particles/ParticleEffectAsset.cpp
    src/Particles/ParticleSystemComponent.cpp
    src/Particles/ParticleRendering.cpp
    src/Particles/SceneParticleSystem.cpp
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

target_compile_definitions(Xora2D PRIVATE XORA2D_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/Resources"
    XORA2D_VERSION_STRING="${PROJECT_VERSION}")

if(XORA2D_ENABLE_SCRIPTING)
    target_sources(Xora2D PRIVATE
        src/Scripting/PythonScriptEngine.cpp
        src/Scripting/PythonBindings.cpp
    )
    target_link_libraries(Xora2D PRIVATE pybind11::pybind11 pybind11::embed Python3::Python)
    if(APPLE)
        set_target_properties(Xora2D PROPERTIES BUILD_WITH_INSTALL_RPATH ON)
        set_target_properties(Xora2D PROPERTIES INSTALL_RPATH "@loader_path;${Python3_LIBRARY_DIRS}")
    endif()
    target_compile_definitions(Xora2D PUBLIC XORA2D_SCRIPTING_ENABLED=1)
else()
    target_sources(Xora2D PRIVATE src/Scripting/PythonScriptEngineDisabled.cpp)
    target_compile_definitions(Xora2D PUBLIC XORA2D_SCRIPTING_ENABLED=0)
endif()

find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(Xora2D PRIVATE SDL3::SDL3 glad::glad EnTT::EnTT Box2D::box2d yaml-cpp::yaml-cpp
    miniaudio_headers)

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