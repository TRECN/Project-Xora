#include "Xora2DEditor/EditorContext.h"

#include "Xora2DEditor/Content/ContentBrowserOperations.h"
#include "Xora2DEditor/Content/ContentBrowserPaths.h"
#include "Xora2DEditor/EditorResourceAssets.h"

#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetRegistrySerializer.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Serialization/SceneSerializer.h"

#include <chrono>

namespace Xora2DEditor {

namespace {

std::shared_ptr<Xora2D::Scene> CreateEmptyScene(const std::string& name) {
    auto scene = Xora2D::Scene::Create();
    scene->SetSceneName(name);
    return scene;
}

// A new scene the user starts from: just a primary camera.
std::shared_ptr<Xora2D::Scene> CreateStarterScene(const std::string& name) {
    auto scene = CreateEmptyScene(name);
    Xora2D::Entity camera = scene->CreateEntity("MainCamera");
    camera.AddComponent<Xora2D::CameraComponent>().Primary = true;
    return scene;
}

std::string SceneNameFromPath(const std::string& contentRelativePath) {
    std::string name = FileNameOf(contentRelativePath);
    constexpr std::string_view kSuffix = ".scene.yaml";
    if (name.size() > kSuffix.size() && name.ends_with(kSuffix)) {
        name.resize(name.size() - kSuffix.size());
    }
    return name;
}

} // namespace

EditorContext::EditorContext() {
    m_scene = Xora2D::Scene::Create();
    m_scene->SetAssetManager(&m_assetManager);
}

EditorContext::~EditorContext() {
    // A project's content root must not outlive the context that opened it.
    if (m_contentActive) {
        (void)m_playSession.Stop();
        if (m_project != nullptr) {
            Xora2D::ResetContentRoot();
        }
    }
}

std::filesystem::path EditorContext::GetAssetRegistryFilePath() const {
    return m_project != nullptr ? m_project->GetAssetRegistryPath() : Xora2D::DefaultRegistryFilePath();
}

void EditorContext::ResetContentServices() {
    m_audioPreview.Stop();
    // Imports that already finished copying are committed into the project they were started in.
    (void)m_importQueue.WaitUntilStaged(std::chrono::seconds(5));
    while (auto batch = m_importQueue.Poll(m_assetManager)) {
        (void)batch;
    }
    m_importQueue.Restart();
    m_thumbnailCache.Restart();
    m_directoryCache.InvalidateAll();
    m_assetDetailsLoader.Invalidate();
    m_contentBrowser = ContentBrowserState{};
    m_inspectorFocus = InspectorFocus::Entity;
}

void EditorContext::DeactivateContent() {
    (void)StopPlayMode();
    ResetContentServices();
    if (m_contentActive) {
        (void)PersistAssetRegistry();
    }
    m_selection.Clear();
    // Release the scene (and the textures its sprites hold) before the assets are unloaded.
    SetScene(CreateEmptyScene("Untitled"));
    m_assetManager.UnloadAll();
    m_assetRegistry.Clear();
    if (m_hostServices.ResetScriptState) {
        m_hostServices.ResetScriptState();
    }
    m_project.reset();
    m_scenePath.clear();
    m_contentActive = false;
    Xora2D::ResetContentRoot();
}

ProjectOpenResult EditorContext::ActivateContent(const std::shared_ptr<Xora2D::Project>& project) {
    ProjectOpenResult result{};
    DeactivateContent();

    if (project != nullptr) {
        Xora2D::SetContentRoot(project->GetRootDirectory());
        for (const std::string& created : project->EnsureStandardDirectories()) {
            result.Warnings.push_back("Created missing folder '" + created + "'");
        }
    }
    m_project = project;
    m_contentActive = true;
    CleanupStaleContentArtifacts();

    const std::filesystem::path registryPath = GetAssetRegistryFilePath();
    std::error_code errorCode;
    std::filesystem::create_directories(registryPath.parent_path(), errorCode);
    if (std::filesystem::exists(registryPath, errorCode)) {
        const auto load = Xora2D::AssetRegistrySerializer::Load(m_assetRegistry, registryPath);
        if (!load.Success) {
            result.Warnings.push_back("Asset registry could not be loaded (" + load.Message + "); it was rebuilt");
            m_assetRegistry.Clear();
        }
    }
    RegisterKnownResourceAssets(m_assetManager);
    if (const RegistrySyncReport sync = SyncRegistryWithDisk(m_assetRegistry); !sync.Missing.empty()) {
        result.Warnings.push_back(std::to_string(sync.Missing.size()) + " registered asset(s) are missing on disk");
    }

    // Startup scene (project) or the sample scene (engine samples).
    std::filesystem::path startupScene;
    std::string startupLabel;
    if (project != nullptr) {
        startupLabel = project->GetConfig().StartupScene;
        if (const auto path = project->GetStartupScenePath()) {
            startupScene = *path;
        }
    } else {
        startupLabel = "Scenes/ExampleScene.scene.yaml";
        startupScene = Xora2D::ResolveContentPath(startupLabel);
    }

    bool loaded = false;
    if (!startupScene.empty() && std::filesystem::is_regular_file(startupScene, errorCode)) {
        const EditorSceneFileResult open = OpenSceneFile(startupScene);
        loaded = open.Success;
        if (!loaded) {
            result.Warnings.push_back(open.Message);
        }
    } else if (!startupLabel.empty()) {
        result.Warnings.push_back("Startup scene '" + startupLabel + "' was not found");
    } else {
        result.Warnings.push_back("The project has no startup scene");
    }
    if (!loaded) {
        SetScene(CreateStarterScene("Untitled"));
        result.StartupSceneMissing = true;
    }

    result.Success = true;
    result.Message = project != nullptr ? "Opened project '" + project->GetName() + "'" : "Opened engine samples";
    for (const std::string& warning : result.Warnings) {
        XORA_LOG_WARN("{}: {}", project != nullptr ? project->GetName() : std::string("Engine samples"), warning);
    }
    XORA_LOG_INFO("{} ({} assets, content root {})", result.Message, m_assetRegistry.Count(),
                  Xora2D::GetContentRoot().string());
    if (m_hostServices.OnProjectChanged) {
        m_hostServices.OnProjectChanged();
    }
    return result;
}

ProjectOpenResult EditorContext::OpenProject(const std::shared_ptr<Xora2D::Project>& project) {
    if (project == nullptr) {
        ProjectOpenResult result{};
        result.Message = "No project to open";
        return result;
    }
    return ActivateContent(project);
}

ProjectOpenResult EditorContext::OpenEngineSamples() {
    return ActivateContent(nullptr);
}

void EditorContext::CloseProject() {
    const bool wasActive = m_contentActive;
    DeactivateContent();
    if (wasActive && m_hostServices.OnProjectChanged) {
        m_hostServices.OnProjectChanged();
    }
}

Xora2D::ProjectFileResult EditorContext::UpdateProjectConfig(const Xora2D::ProjectConfig& config) {
    if (m_project == nullptr) {
        Xora2D::ProjectFileResult result{};
        result.Error = Xora2D::ProjectError::FileNotFound;
        result.Message = "No project is open";
        return result;
    }
    Xora2D::ProjectFileResult result = m_project->UpdateConfig(config);
    if (result.Success && m_hostServices.OnProjectChanged) {
        m_hostServices.OnProjectChanged();
    }
    return result;
}

EditorSceneFileResult EditorContext::OpenSceneFile(const std::filesystem::path& absoluteSceneFile) {
    EditorSceneFileResult result{};
    const std::optional<std::string> relative = AbsoluteToResourceRelative(absoluteSceneFile);
    if (!relative.has_value()) {
        result.Message = "Refusing to open a scene outside the project: " + absoluteSceneFile.string();
        return result;
    }
    // Opening a scene replaces the editable scene; any running play session ends first.
    (void)StopPlayMode();
    auto scene = Xora2D::Scene::Create();
    scene->SetAssetManager(&m_assetManager);
    const auto load = Xora2D::SceneSerializer::Deserialize(*scene, absoluteSceneFile, &m_assetManager);
    if (!load.Success) {
        result.Message = "Cannot open scene '" + *relative + "': " + load.Message;
        return result;
    }
    SetScene(scene);
    m_scenePath = *relative;
    result.Success = true;
    result.Message = "Opened scene " + *relative;
    return result;
}

EditorSceneFileResult EditorContext::SaveSceneAs(const std::string& contentRelativePath) {
    EditorSceneFileResult result{};
    const std::optional<std::string> normalized = NormalizeResourceRelativePath(contentRelativePath);
    if (!normalized.has_value() || normalized->empty() || !Xora2D::IsSupportedSceneExtension(*normalized)) {
        result.Message = "Scene path must be a project-relative *.scene.yaml path";
        return result;
    }
    const std::filesystem::path absolute = ResolveResourceRelativePath(*normalized);
    if (absolute.empty() || m_scene == nullptr) {
        result.Message = "Scene path is outside the project";
        return result;
    }
    std::error_code errorCode;
    std::filesystem::create_directories(absolute.parent_path(), errorCode);
    const auto save = Xora2D::SceneSerializer::Serialize(*m_scene, absolute);
    if (!save.Success) {
        result.Message = "Cannot save scene: " + save.Message;
        return result;
    }
    m_scenePath = *normalized;
    NotifyContentChanged(ParentResourceDirectory(*normalized));
    result.Success = true;
    result.Message = "Saved " + *normalized;
    return result;
}

EditorSceneFileResult EditorContext::SaveScene() {
    if (m_scenePath.empty()) {
        EditorSceneFileResult result{};
        result.Message = "The scene has no file yet; use Save Scene As";
        return result;
    }
    return SaveSceneAs(m_scenePath);
}

EditorSceneFileResult EditorContext::CreateScene(const std::string& contentRelativePath, bool makeStartupScene) {
    EditorSceneFileResult result{};
    const std::optional<std::string> normalized = NormalizeResourceRelativePath(contentRelativePath);
    if (!normalized.has_value() || normalized->empty() || !Xora2D::IsSupportedSceneExtension(*normalized)) {
        result.Message = "Scene path must be a project-relative *.scene.yaml path";
        return result;
    }
    const std::filesystem::path absolute = ResolveResourceRelativePath(*normalized);
    std::error_code errorCode;
    if (absolute.empty()) {
        result.Message = "Scene path is outside the project";
        return result;
    }
    if (std::filesystem::exists(absolute, errorCode)) {
        result.Message = "A file already exists at " + *normalized;
        return result;
    }
    (void)StopPlayMode();
    SetScene(CreateStarterScene(SceneNameFromPath(*normalized)));
    result = SaveSceneAs(*normalized);
    if (result.Success && makeStartupScene && m_project != nullptr) {
        const EditorSceneFileResult startup = SetStartupScene(*normalized);
        if (!startup.Success) {
            return startup;
        }
    }
    return result;
}

EditorSceneFileResult EditorContext::SetStartupScene(const std::string& contentRelativePath) {
    EditorSceneFileResult result{};
    if (m_project == nullptr) {
        result.Message = "No project is open";
        return result;
    }
    Xora2D::ProjectConfig config = m_project->GetConfig();
    config.StartupScene = contentRelativePath;
    const Xora2D::ProjectFileResult save = UpdateProjectConfig(config);
    result.Success = save.Success;
    result.Message = save.Success ? "Startup scene set to " + contentRelativePath : save.Message;
    return result;
}

std::shared_ptr<Xora2D::Scene> EditorContext::GetScene() const {
    if (m_playSession.IsActive() && m_playSession.GetRuntimeScene() != nullptr) {
        return m_playSession.GetRuntimeScene();
    }
    return m_scene;
}

bool EditorContext::StartPlayMode() {
    if (m_scene == nullptr) {
        return false;
    }
    return m_playSession.Play(*m_scene, &m_assetManager);
}

bool EditorContext::StopPlayMode() {
    if (!m_playSession.Stop()) {
        return false;
    }
    // Runtime entities are gone; drop a selection that only existed at runtime.
    if (m_scene != nullptr) {
        m_selection.Sanitize(*m_scene);
    }
    return true;
}

void EditorContext::UpdatePlayMode(float deltaSeconds) {
    (void)m_playSession.Update(deltaSeconds);
}

void EditorContext::SetScene(const std::shared_ptr<Xora2D::Scene>& scene) {
    (void)StopPlayMode();
    m_scene = scene;
    if (m_scene) {
        m_scene->SetAssetManager(&m_assetManager);
    }
    m_scenePath.clear();
    m_selection.OnSceneReplaced();
}

void EditorContext::SelectEntity(const Xora2D::Entity& entity) {
    m_inspectorFocus = InspectorFocus::Entity;
    if (entity.IsValid()) {
        m_selection.Select(entity.GetUUID());
    } else {
        ClearSelection();
    }
}

void EditorContext::SelectEntityByUUID(const Xora2D::UUID& uuid) {
    m_inspectorFocus = InspectorFocus::Entity;
    m_selection.Select(uuid);
}

Xora2D::Entity EditorContext::GetSelectedEntity() const {
    const std::shared_ptr<Xora2D::Scene> scene = GetScene();
    if (scene == nullptr) {
        return {};
    }
    return m_selection.Resolve(*scene);
}

void EditorContext::ClearSelection() {
    m_selection.Clear();
}

void EditorContext::SelectContentItem(const ContentBrowserEntry& entry) {
    if (entry.RegisteredHandle.has_value()) {
        m_contentBrowser.SelectAsset(*entry.RegisteredHandle, entry.ResourceRelativePath);
    } else {
        m_contentBrowser.SelectPath(entry.ResourceRelativePath);
    }
    if (!m_audioPreview.IsPreviewing(entry.RegisteredHandle.value_or(Xora2D::AssetHandle{}))) {
        m_audioPreview.Stop();
    }
    m_inspectorFocus = entry.IsFolder() ? m_inspectorFocus : InspectorFocus::Asset;
}

void EditorContext::NotifyContentChanged(const std::string& resourceRelativePath) {
    m_directoryCache.InvalidateTree(resourceRelativePath);
    m_thumbnailCache.InvalidatePrefix(resourceRelativePath);
    m_assetDetailsLoader.Invalidate();
}

bool EditorContext::PersistAssetRegistry() {
    if (!m_contentActive) {
        return true;
    }
    const std::filesystem::path path = GetAssetRegistryFilePath();
    const auto save = Xora2D::AssetRegistrySerializer::Save(m_assetRegistry, path);
    if (!save.Success) {
        XORA_LOG_WARN("Failed to save asset registry {}: {}", path.string(), save.Message);
        return false;
    }
    return true;
}

void EditorContext::ShutdownContentServices() {
    m_audioPreview.Stop();
    m_audioPreview.SetAudioEngine(nullptr);
    m_importQueue.Shutdown();
    m_thumbnailCache.Shutdown();
    m_thumbnailCache.Clear();
}

} // namespace Xora2DEditor