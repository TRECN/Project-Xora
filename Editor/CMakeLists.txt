add_executable(Xora2D-Editor
    src/main.cpp
    src/EditorApplication.cpp
    src/EditorConfig.cpp
    src/EditorContext.cpp
    src/EditorSelection.cpp
    src/EditorSceneCommands.cpp
    src/EditorPlaySession.cpp
    src/EditorResourceAssets.cpp
    src/ComponentInspectorUI.cpp
    src/TransformEditing.cpp
    src/AssetInspectorUI.cpp
    src/Content/AssetPreview.cpp
    src/Content/ContentBrowserQuery.cpp
    src/Content/ContentBrowserState.cpp
    src/Content/ContentBrowserPaths.cpp
    src/Content/ContentBrowserDirectoryCache.cpp
    src/Content/ContentBrowserImport.cpp
    src/Content/ContentBrowserOperations.cpp
    src/Content/AssetThumbnailCache.cpp
    src/Content/EditorFileDialog.cpp
    src/Content/EditorAssetAssignments.cpp
    src/EditorLayer.cpp
    src/EditorPaths.cpp
    src/EditorShortcuts.cpp
    src/EditorUI.cpp
    src/EditorWorkspace.cpp
    src/ImGuiIntegration.cpp
    src/Panels/ViewportPanel.cpp
    src/Panels/SceneHierarchyPanel.cpp
    src/Panels/InspectorPanel.cpp
    src/Panels/ContentBrowserPanel.cpp
    src/Panels/ConsolePanel.cpp
    src/Panels/ProjectSettingsPanel.cpp
    src/Project/RecentProjects.cpp
    src/Project/ProjectManagerUI.cpp
)

target_include_directories(Xora2D-Editor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party/stb
)

target_compile_features(Xora2D-Editor PRIVATE cxx_std_20)
target_link_libraries(Xora2D-Editor PRIVATE Xora2D::Xora2D imgui::imgui yaml-cpp::yaml-cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(Xora2D-Editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET Xora2D-Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Resources"
        "$<TARGET_FILE_DIR:Xora2D-Editor>/Resources"
    COMMENT "Copying editor Resources"
)