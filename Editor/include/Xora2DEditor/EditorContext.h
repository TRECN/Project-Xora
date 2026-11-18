#pragma once

#include "Xora2DEditor/Content/AssetPreview.h"
#include "Xora2DEditor/Content/AssetThumbnailCache.h"
#include "Xora2DEditor/Content/ContentBrowserDirectoryCache.h"
#include "Xora2DEditor/Content/ContentBrowserImport.h"
#include "Xora2DEditor/Content/ContentBrowserState.h"
#include "Xora2DEditor/Content/EditorFileDialog.h"
#include "Xora2DEditor/EditorConfig.h"
#include "Xora2DEditor/EditorHostServices.h"
#include "Xora2DEditor/EditorPlaySession.h"
#include "Xora2DEditor/EditorSelection.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Core/UUID.h"
#include "Xora2D/Prefab/Prefab.h"
#include "Xora2D/Prefab/PrefabUtility.h"
#include "Xora2D/Project/Project.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Xora2DEditor {

// What the Inspector panel shows: the selected entity, or the asset selected in the content browser.
enum class InspectorFocus : std::uint8_t { Entity, Asset };

struct ProjectOpenResult {
    bool Success = false;
    std::string Message;
    std::vector<std::string> Warnings;
    // The configured startup scene is missing or failed to load; an empty scene was opened instead.
    bool StartupSceneMissing = false;
};

struct EditorSceneFileResult {
    bool Success = false;
    std::string Message;
};

// Shared editor state passed to panels (scene, assets, selection, config).
//
// Content lifecycle: nothing is open after construction. OpenProject() activates a project (content root, asset
// registry, startup scene); OpenEngineSamples() activates the engine's own Resources/ tree without a project (legacy
// workflow). CloseProject() / switching tears the previous content down first: play mode stops, background content
// workers restart, the registry is saved and cleared, loaded assets are released and the scene is replaced, so
// nothing from the previous project stays reachable.
class EditorContext {
public:
    EditorContext();
    ~EditorContext();

    EditorContext(const EditorContext&) = delete;
    EditorContext& operator=(const EditorContext&) = delete;

    // Projects ------------------------------------------------------------------------------------------------------
    ProjectOpenResult OpenProject(const std::shared_ptr<Xora2D::Project>& project);
    ProjectOpenResult OpenEngineSamples();
    void CloseProject();
    [[nodiscard]] const std::shared_ptr<Xora2D::Project>& GetProject() const { return m_project; }
    [[nodiscard]] bool HasProject() const { return m_project != nullptr; }
    // True while a project (or the engine samples) is open; the editor shows the Project Manager otherwise.
    [[nodiscard]] bool IsContentActive() const { return m_contentActive; }
    // Validates + saves the project file; on success the host is notified (title, vsync, ...).
    Xora2D::ProjectFileResult UpdateProjectConfig(const Xora2D::ProjectConfig& config);
    [[nodiscard]] std::filesystem::path GetAssetRegistryFilePath() const;

    // Scene files (paths are relative to the content root, e.g. "Scenes/Main.scene.yaml") ---------------------------
    [[nodiscard]] const std::string& GetScenePath() const { return m_scenePath; }
    EditorSceneFileResult OpenSceneFile(const std::filesystem::path& absoluteSceneFile);
    // Saves the editable scene (never the runtime copy) to its current path.
    EditorSceneFileResult SaveScene();
    EditorSceneFileResult SaveSceneAs(const std::string& contentRelativePath);
    // Creates a scene with a camera at `contentRelativePath`, saves and opens it; optionally makes it the startup
    // scene of the open project.
    EditorSceneFileResult CreateScene(const std::string& contentRelativePath, bool makeStartupScene);
    EditorSceneFileResult SetStartupScene(const std::string& contentRelativePath);

    // Prefabs -------------------------------------------------------------------------------------------------------
    // Instantiates into the active scene (the runtime copy while playing, so runtime instances are discarded on Stop).
    // Refused while a prefab is being edited (no nested prefabs).
    Xora2D::PrefabInstantiateResult InstantiatePrefab(Xora2D::AssetHandle prefab,
                                                      std::optional<glm::vec2> position = std::nullopt);
    // Writes to project files: refused while playing so runtime changes never reach prefab assets.
    Xora2D::PrefabCreateResult CreatePrefab(const std::vector<Xora2D::UUID>& entities,
                                            const Xora2D::PrefabCreateOptions& options);

    // Isolated prefab editing: a private copy of the prefab becomes the active scene (Hierarchy / Inspector /
    // Viewport edit it); the open scene is untouched. Save writes the asset (new revision) and returns to the scene;
    // Close discards the copy.
    EditorSceneFileResult OpenPrefabEditor(Xora2D::AssetHandle prefab);
    EditorSceneFileResult SavePrefabEditor();
    void ClosePrefabEditor();
    [[nodiscard]] bool IsEditingPrefab() const { return m_prefabEdit.Working != nullptr; }
    [[nodiscard]] Xora2D::AssetHandle GetEditedPrefab() const { return m_prefabEdit.Handle; }
    [[nodiscard]] std::string GetEditedPrefabName() const;

    // Replaces the editable scene (stops Play mode first if it is running).
    void SetScene(const std::shared_ptr<Xora2D::Scene>& scene);
    // The scene panels show and edit: the runtime copy while a play session is active, otherwise the editable scene.
    [[nodiscard]] std::shared_ptr<Xora2D::Scene> GetScene() const;
    // The editable (persistent) scene, regardless of play state.
    [[nodiscard]] std::shared_ptr<Xora2D::Scene> GetEditScene() const { return m_scene; }

    Xora2D::AssetManager& GetAssetManager() { return m_assetManager; }
    const Xora2D::AssetManager& GetAssetManager() const { return m_assetManager; }
    Xora2D::AssetRegistry& GetAssetRegistry() { return m_assetRegistry; }

    EditorConfig& GetConfig() { return m_config; }
    const EditorConfig& GetConfig() const { return m_config; }

    void SelectEntity(const Xora2D::Entity& entity);
    void SelectEntityByUUID(const Xora2D::UUID& uuid);
    [[nodiscard]] Xora2D::Entity GetSelectedEntity() const;
    void ClearSelection();
    [[nodiscard]] EditorSelection& GetSelection() { return m_selection; }
    [[nodiscard]] const EditorSelection& GetSelection() const { return m_selection; }

    // Play mode. Selection is kept by UUID, so a selected entity resolves to its runtime copy while playing and back
    // to the editable entity after Stop; runtime-only entities are deselected on Stop.
    [[nodiscard]] bool IsPlayModeActive() const { return m_playSession.IsActive(); }
    [[nodiscard]] EditorPlayState GetPlayState() const { return m_playSession.GetState(); }
    EditorPlaySession& GetPlaySession() { return m_playSession; }
    const EditorPlaySession& GetPlaySession() const { return m_playSession; }
    bool StartPlayMode();
    bool PausePlayMode() { return m_playSession.Pause(); }
    bool ResumePlayMode() { return m_playSession.Resume(); }
    bool StepPlayMode() { return m_playSession.Step(); }
    bool StopPlayMode();
    // Advances the runtime scene (no-op in edit mode). Called once per editor frame.
    void UpdatePlayMode(float deltaSeconds);

    // Viewport focus/hover from the last UI frame; gameplay input is only delivered while playing and focused.
    void SetViewportInputState(bool focused, bool hovered) {
        m_viewportFocused = focused;
        m_viewportHovered = hovered;
    }
    [[nodiscard]] bool IsViewportFocused() const { return m_viewportFocused; }
    [[nodiscard]] bool IsViewportHovered() const { return m_viewportHovered; }

    EditorHostServices& GetHostServices() { return m_hostServices; }
    const EditorHostServices& GetHostServices() const { return m_hostServices; }

    ContentBrowserState& GetContentBrowser() { return m_contentBrowser; }
    const ContentBrowserState& GetContentBrowser() const { return m_contentBrowser; }
    ContentBrowserDirectoryCache& GetDirectoryCache() { return m_directoryCache; }
    AssetThumbnailCache& GetThumbnailCache() { return m_thumbnailCache; }
    EditorFileDialog& GetFileDialog() { return m_fileDialog; }
    ContentImportQueue& GetImportQueue() { return m_importQueue; }
    AssetDetailsLoader& GetAssetDetailsLoader() { return m_assetDetailsLoader; }
    AudioPreviewPlayer& GetAudioPreview() { return m_audioPreview; }

    [[nodiscard]] InspectorFocus GetInspectorFocus() const { return m_inspectorFocus; }
    void SetInspectorFocus(InspectorFocus focus) { m_inspectorFocus = focus; }

    // Selects a content-browser item and shows it in the Inspector. Entity selection is kept so assets can be
    // assigned to the previously selected entity.
    void SelectContentItem(const ContentBrowserEntry& entry);

    // Call after any change to project files or the registry so cached listings/thumbnails/details refresh.
    void NotifyContentChanged(const std::string& resourceRelativePath);
    // Saves the asset registry of the open project / samples. No-op (true) when nothing is open.
    bool PersistAssetRegistry();

    // Stops background workers and preview playback (call before the audio engine / GL context go away).
    void ShutdownContentServices();

private:
    ProjectOpenResult ActivateContent(const std::shared_ptr<Xora2D::Project>& project);
    void DeactivateContent();
    void ResetContentServices();

    struct PrefabEditState {
        Xora2D::AssetHandle Handle{};
        std::shared_ptr<Xora2D::Prefab> Working;
        std::optional<Xora2D::UUID> SceneSelection;
    };
    PrefabEditState m_prefabEdit;

    std::shared_ptr<Xora2D::Project> m_project;
    bool m_contentActive = false;
    std::string m_scenePath;
    std::shared_ptr<Xora2D::Scene> m_scene;
    Xora2D::AssetRegistry m_assetRegistry;
    Xora2D::AssetManager m_assetManager{m_assetRegistry};
    EditorConfig m_config;
    EditorSelection m_selection;
    EditorPlaySession m_playSession;
    bool m_viewportFocused = false;
    bool m_viewportHovered = false;
    EditorHostServices m_hostServices;
    ContentBrowserState m_contentBrowser;
    ContentBrowserDirectoryCache m_directoryCache;
    AssetThumbnailCache m_thumbnailCache;
    EditorFileDialog m_fileDialog;
    ContentImportQueue m_importQueue;
    AssetDetailsLoader m_assetDetailsLoader;
    AudioPreviewPlayer m_audioPreview;
    InspectorFocus m_inspectorFocus = InspectorFocus::Entity;
};

} // namespace Xora2DEditor