set(XORA2D_EDITOR_TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/Editor/src/EditorConfig.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/EditorContext.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/EditorSelection.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/EditorSceneCommands.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/EditorPlaySession.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/EditorResourceAssets.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/EditorPaths.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/Project/RecentProjects.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/TransformEditing.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/Content/ContentBrowserPaths.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/Content/ContentBrowserImport.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/Content/ContentBrowserOperations.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/Content/ContentBrowserState.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/Content/ContentBrowserDirectoryCache.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/Content/ContentBrowserQuery.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/Content/AssetThumbnailCache.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/Content/AssetPreview.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/Content/EditorAssetAssignments.cpp
    ${CMAKE_SOURCE_DIR}/Editor/src/Content/EditorFileDialog.cpp
)

add_executable(Xora2D-Tests
    src/CoreTests.cpp
    src/GraphicsTests.cpp
    src/RendererTests.cpp
    src/SceneTests.cpp
    src/PhysicsTests.cpp
    src/SerializationTests.cpp
    src/AssetTests.cpp
    src/InputTests.cpp
    src/AudioTests.cpp
    src/ScriptingTests.cpp
    src/AnimationTests.cpp
    src/EditorTests.cpp
    src/ContentBrowserTests.cpp
    src/TransformInspectorTests.cpp
    src/PlayModeTests.cpp
    src/ProjectTests.cpp
    ${XORA2D_EDITOR_TEST_SOURCES}
)

find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(Xora2D-Tests PRIVATE Xora2D::Xora2D SDL3::SDL3 yaml-cpp::yaml-cpp)
target_include_directories(Xora2D-Tests PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/src
    ${CMAKE_SOURCE_DIR}/Editor/include
    ${CMAKE_SOURCE_DIR}/third_party/glad/include
    ${CMAKE_SOURCE_DIR}/third_party/stb
)
target_compile_features(Xora2D-Tests PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(Xora2D-Tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME Xora2D-Tests COMMAND Xora2D-Tests)
