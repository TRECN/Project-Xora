#include "Xora2DEditor/EditorApplication.h"

#include "Xora2DEditor/Content/ContentBrowserOperations.h"
#include "Xora2DEditor/Content/ContentBrowserPaths.h"
#include "Xora2DEditor/EditorConfig.h"
#include "Xora2DEditor/EditorPaths.h"
#include "Xora2DEditor/EditorResourceAssets.h"

#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Events/Event.h"
#include "Xora2D/Events/KeyEvent.h"
#include "Xora2D/Events/MouseEvent.h"
#include "Xora2D/Events/WindowEvent.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Renderer/ColorRenderTarget.h"
#include "Xora2D/Renderer/OrthographicCamera.h"
#include "Xora2D/Renderer/RendererAPI.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetRegistrySerializer.h"
#include "Xora2D/Physics/PhysicsWorld2D.h"
#include "Xora2D/Project/Project.h"
#include "Xora2D/Scripting/ScriptServices.h"
#include "Xora2D/Serialization/SceneSerializer.h"

#include <glm/vec4.hpp>
#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <glm/common.hpp>

namespace Xora2DEditor {

namespace {

Xora2D::OrthographicCamera FramePrefabContent(Xora2D::Scene& scene, float width, float height) {
    glm::vec2 minimum{1e9f};
    glm::vec2 maximum{-1e9f};
    bool any = false;
    for (const Xora2D::UUID& id : scene.GetEntityUUIDs()) {
        const Xora2D::Entity entity = scene.FindEntityByUUID(id);
        const auto& transform = entity.GetComponent<Xora2D::TransformComponent>();
        glm::vec2 half{25.0f};
        if (entity.HasComponent<Xora2D::SpriteRendererComponent>()) {
            half = glm::abs(entity.GetComponent<Xora2D::SpriteRendererComponent>().Size * transform.Scale) * 0.5f;
        }
        minimum = glm::min(minimum, transform.Translation - half);
        maximum = glm::max(maximum, transform.Translation + half);
        any = true;
    }
    if (!any) {
        minimum = {-100.0f, -100.0f};
        maximum = {100.0f, 100.0f};
    }
    const glm::vec2 size = glm::max(maximum - minimum, glm::vec2{1.0f}) * 1.4f; // margin
    const float zoom = std::clamp(std::min(width / size.x, height / size.y), 0.05f, 4.0f);
    const glm::vec2 center = (minimum + maximum) * 0.5f;
    Xora2D::OrthographicCamera camera;
    camera.SetViewportSize(width, height);
    camera.SetZoom(zoom);
    // The camera position is the bottom-left corner of the view.
    camera.SetPosition(center - glm::vec2{width, height} * (0.5f / zoom));
    return camera;
}

} // namespace

EditorApplication::EditorApplication(EditorLaunchOptions options)
    : Xora2D::Application(Xora2D::WindowProperties{
          .title = "Xora2D - Project Manager",
          .width = 1600,
          .height = 900,
          .enableOpenGL = true,
      }) {
    LoadEditorConfiguration();
    SetClearColor(glm::vec4(0.08f, 0.09f, 0.12f, 1.0f));

    const std::filesystem::path iniPath = GetImGuiLayoutPath();
    if (!m_imgui.Initialize(GetWindow(), GetGraphicsContext(), iniPath)) {
        XORA_LOG_WARN("Dear ImGui failed to initialize");
    }
    const bool layoutComplete = IsImGuiDockLayoutComplete(iniPath);
    m_layer.GetUI().SetApplyDefaultLayoutOnNextFrame(!layoutComplete);
    if (!layoutComplete) {
        XORA_LOG_INFO("Editor layout: applying default (no complete dock layout in imgui.ini)");
    }

    m_context.GetAssetManager().SetGraphicsResourcesEnabled(true);
    m_context.GetHostServices().MainWindow = &GetWindow();
    m_context.GetHostServices().AudioEngine = &GetAudioEngine();
    m_context.GetAudioPreview().SetAudioEngine(&GetAudioEngine());
    m_context.GetHostServices().OnProjectChanged = [this]() { OnProjectChanged(); };
    // Script modules are cached per asset handle; handles are path-derived, so two projects can share one.
    m_context.GetHostServices().ResetScriptState = [this]() { GetPythonScriptEngine().PrepareShutdown(); };
    m_imgui.SetFileDropHandler([this](const std::filesystem::path& file) {
        // Collected per frame and submitted as one import batch into the folder the browser is showing.
        m_droppedFiles.push_back(file);
    });
    m_context.GetHostServices().OpenScene = [this](const std::filesystem::path& sceneFile) { OpenSceneAsset(sceneFile); };
    m_context.GetPlaySession().SetHooks(PlaySessionHooks{
        .WireRuntimeScene = [this](Xora2D::Scene& scene) { WireRuntimeScene(scene); },
        .ClearGameplayInput = [this]() { GetInputManager().ClearState(); },
    });

    if (const auto load = m_recentProjects.Load(GetRecentProjectsFilePath()); !load.Success) {
        XORA_LOG_WARN("Recent projects: {}", load.Message);
    }
    if (!m_recentProjects.GetEntries().empty()) {
        // New projects default to the folder the most recent project lives in.
        const std::filesystem::path recentParent = m_recentProjects.GetEntries().front().ProjectFile.parent_path().parent_path();
        std::error_code errorCode;
        if (std::filesystem::is_directory(recentParent, errorCode)) {
            m_projectManager.SetDefaultLocation(recentParent);
        }
    }

    m_layer.Attach(m_context);
    OnProjectChanged();
    if (!options.ProjectFile.empty() && !OpenProjectFile(options.ProjectFile)) {
        XORA_LOG_WARN("Could not open {}; showing the Project Manager", options.ProjectFile.string());
    }
    XORA_LOG_INFO("Xora2D Editor ready");
}

EditorApplication::~EditorApplication() {
    if (m_context.IsEditingPrefab()) {
        XORA_LOG_WARN("Closing the editor while editing prefab '{}': unsaved prefab changes were discarded",
                      m_context.GetEditedPrefabName());
        m_context.ClosePrefabEditor();
    }
    // Tear the runtime scene down while the audio and Python engines are still alive.
    (void)m_context.StopPlayMode();
    m_imgui.SetFileDropHandler(nullptr);
    m_context.ShutdownContentServices();
    (void)m_context.PersistAssetRegistry();
    SaveEditorConfiguration();
    SaveRecentProjects();
    m_layer.Detach();
    GetWindow().SetSdlEventHook(nullptr);
    m_imgui.Shutdown();
}

void EditorApplication::LoadEditorConfiguration() {
    const auto result = EditorConfig::LoadOrDefaults(GetEditorConfigFilePath(), m_context.GetConfig());
    if (!result.Success) {
        XORA_LOG_WARN("Editor config: {}", result.Message);
        return;
    }
    if (result.UsedDefaults) {
        XORA_LOG_INFO("Editor config: using defaults ({})", result.Message);
    }
}

void EditorApplication::SaveEditorConfiguration() {
    auto& window = m_context.GetConfig().GetData().Window;
    window.Width = GetWindow().GetWidth();
    window.Height = GetWindow().GetHeight();
    const auto saveResult = m_context.GetConfig().Save(GetEditorConfigFilePath());
    if (!saveResult.Success) {
        XORA_LOG_WARN("Failed to save editor config: {}", saveResult.Message);
    }
    m_imgui.SaveLayout();
}

void EditorApplication::OpenSceneAsset(const std::filesystem::path& sceneFile) {
    const EditorSceneFileResult result = m_context.OpenSceneFile(sceneFile);
    m_layer.GetUI().ShowStatusMessage(result.Message);
    if (result.Success) {
        XORA_LOG_INFO("{}", result.Message);
    } else {
        XORA_LOG_WARN("{}", result.Message);
    }
}

void EditorApplication::RequestOpenProject(const std::filesystem::path& projectFile) {
    PendingProjectAction action{};
    action.Type = PendingProjectAction::Kind::Open;
    action.ProjectFile = projectFile;
    m_pendingProjectAction = std::move(action);
}

void EditorApplication::RequestShowProjectManager(bool createProject) {
    PendingProjectAction action{};
    action.Type = PendingProjectAction::Kind::Close;
    action.ShowCreatePage = createProject;
    m_pendingProjectAction = std::move(action);
}

void EditorApplication::ProcessPendingProjectAction() {
    if (!m_pendingProjectAction.has_value()) {
        return;
    }
    PendingProjectAction action = std::move(*m_pendingProjectAction);
    m_pendingProjectAction.reset();
    switch (action.Type) {
    case PendingProjectAction::Kind::Open:
        (void)OpenProjectFile(action.ProjectFile);
        break;
    case PendingProjectAction::Kind::Create:
        CreateAndOpenProject(action.Create);
        break;
    case PendingProjectAction::Kind::Close:
        m_context.CloseProject();
        if (action.ShowCreatePage) {
            m_projectManager.ShowCreateForm();
        } else {
            m_projectManager.ShowRecentProjects();
        }
        break;
    case PendingProjectAction::Kind::OpenEngineSamples: {
        const ProjectOpenResult result = m_context.OpenEngineSamples();
        m_layer.GetUI().ShowStatusMessage(result.Message);
        break;
    }
    }
}

bool EditorApplication::OpenProjectFile(const std::filesystem::path& projectFile) {
    const Xora2D::ProjectLoadResult load = Xora2D::Project::Load(projectFile);
    if (!load.Success) {
        XORA_LOG_WARN("Open project failed: {}", load.Message);
        std::string message = load.Message;
        if (load.Error == Xora2D::ProjectError::FileNotFound && m_recentProjects.Contains(projectFile)) {
            message += "\n\nThe project may have been moved or deleted. You can remove it from the recent list.";
        }
        m_projectManager.ShowError("Could not open project", message);
        return false;
    }
    // Load warnings (missing startup scene / folders) are reported by EditorContext::OpenProject.
    const ProjectOpenResult open = m_context.OpenProject(load.Loaded);
    if (!open.Success) {
        m_projectManager.ShowError("Could not open project", open.Message);
        return false;
    }
    m_recentProjects.Add(load.Loaded->GetName(), load.Loaded->GetProjectFile(), RecentProjectList::NowUnixSeconds());
    SaveRecentProjects();
    m_layer.GetUI().ShowStatusMessage(open.Message);
    if (open.StartupSceneMissing) {
        m_layer.GetUI().NotifyStartupSceneMissing(m_context, load.Loaded->GetConfig().StartupScene);
    }
    return true;
}

void EditorApplication::CreateAndOpenProject(const Xora2D::ProjectCreateOptions& options) {
    const Xora2D::ProjectCreateResult created = Xora2D::CreateProject(options);
    if (!created.Success) {
        XORA_LOG_WARN("Create project failed: {}", created.Message);
        m_projectManager.ShowError("Could not create project", created.Message);
        return;
    }
    XORA_LOG_INFO("{}", created.Message);
    m_projectManager.SetDefaultLocation(options.ParentDirectory);
    (void)OpenProjectFile(created.ProjectFile);
}

void EditorApplication::SaveRecentProjects() {
    if (const auto save = m_recentProjects.Save(GetRecentProjectsFilePath()); !save.Success) {
        XORA_LOG_WARN("Recent projects: {}", save.Message);
    }
}

// Window title and project-level runtime settings that apply to the editor itself.
void EditorApplication::OnProjectChanged() {
    const std::shared_ptr<Xora2D::Project>& project = m_context.GetProject();
    if (project != nullptr) {
        GetWindow().SetTitle("Xora2D Editor - " + project->GetName() + " (" + project->GetRootDirectory().string() +
                             ")");
        if (!GetGraphicsContext().SetVSync(project->GetSettings().VSync)) {
            XORA_LOG_WARN("VSync setting could not be applied");
        }
    } else if (m_context.IsContentActive()) {
        GetWindow().SetTitle("Xora2D Editor - Engine Samples");
        (void)GetGraphicsContext().SetVSync(true);
    } else {
        GetWindow().SetTitle("Xora2D - Project Manager");
        (void)GetGraphicsContext().SetVSync(true);
    }
}

void EditorApplication::DrawProjectManager() {
    ProjectManagerActions actions{};
    actions.OpenProject = [this](const std::filesystem::path& file) { RequestOpenProject(file); };
    actions.CreateProject = [this](const Xora2D::ProjectCreateOptions& options) {
        PendingProjectAction action{};
        action.Type = PendingProjectAction::Kind::Create;
        action.Create = options;
        m_pendingProjectAction = std::move(action);
    };
    actions.RemoveRecentProject = [this](const std::filesystem::path& file) {
        // History only: the project's files are never touched.
        if (m_recentProjects.Remove(file)) {
            SaveRecentProjects();
        }
    };
    actions.OpenEngineSamples = [this]() {
        PendingProjectAction action{};
        action.Type = PendingProjectAction::Kind::OpenEngineSamples;
        m_pendingProjectAction = std::move(action);
    };
    actions.Quit = [this]() { Close(); };
    m_projectManager.Draw(m_recentProjects, &GetWindow(), actions);
}

// Only the runtime (play) scene gets audio and scripting: the editable scene never plays sounds or runs scripts.
void EditorApplication::WireRuntimeScene(Xora2D::Scene& scene) {
    scene.SetAudioEngine(&GetAudioEngine());
    const std::shared_ptr<Xora2D::Project>& project = m_context.GetProject();
    if (project != nullptr) {
        // Project settings: world gravity for every runtime scene; scripts can be switched off per project.
        scene.GetPhysicsWorld().SetGravity(project->GetSettings().Gravity);
        if (!project->GetSettings().ScriptingEnabled) {
            XORA_LOG_INFO("Play mode: Python scripting is disabled in the project settings");
            return;
        }
    }
    scene.SetPythonScriptEngine(&GetPythonScriptEngine());
    // Same services the Sandbox provides. The engine sets ScriptServices::Scene itself around each scene update.
    Xora2D::ScriptServices services{};
    services.Input = &GetInputManager();
    services.Audio = &GetAudioEngine();
    services.Assets = &m_context.GetAssetManager();
    GetPythonScriptEngine().SetServices(services);
}

void EditorApplication::UpdateGameplayInputGate() {
    const GameplayInputGate gate = ComputeGameplayInputGate(m_context.GetPlayState(), m_context.IsViewportFocused(),
                                                            m_context.IsViewportHovered(), ImGui::GetIO().WantTextInput);
    const bool keyboard = gate.Keyboard;
    const bool mouse = gate.Mouse;
    if ((m_allowGameplayKeyboard && !keyboard) || (m_allowGameplayMouse && !mouse)) {
        // Focus left the game view (or play paused/stopped): release everything so no key or button sticks.
        GetInputManager().ClearState();
    }
    m_allowGameplayKeyboard = keyboard;
    m_allowGameplayMouse = mouse;
}

int EditorApplication::Run() {
    while (IsRunning()) {
        ProcessPendingProjectAction();
        GetInputManager().BeginFrame();
        GetWindow().PollEvents();
        if (!IsRunning()) {
            break;
        }

        if (!m_context.IsContentActive()) {
            // Project Manager: no scene, no play mode, no content services.
            m_droppedFiles.clear();
            m_imgui.NewFrame();
            UpdateGameplayInputGate();
            (void)GetTime().Tick();
            GetAudioEngine().Update(0.0f);
            DrawProjectManager();
            const int framebufferWidth = GetWindow().GetFramebufferWidth();
            const int framebufferHeight = GetWindow().GetFramebufferHeight();
            if (framebufferWidth > 0 && framebufferHeight > 0) {
                GetRendererAPI().SetViewport(0, 0, framebufferWidth, framebufferHeight);
            }
            GetRendererAPI().SetClearColor(m_clearColor);
            GetRendererAPI().Clear();
            m_imgui.RenderDrawData();
            GetRendererAPI().SwapBuffers();
            continue;
        }

        if (!m_droppedFiles.empty()) {
            m_context.GetImportQueue().Submit(std::move(m_droppedFiles),
                                              m_context.GetContentBrowser().GetCurrentDirectory());
            m_droppedFiles.clear();
        }
        m_context.GetAudioPreview().Update();

        m_imgui.NewFrame();
        UpdateGameplayInputGate();

        const double deltaSeconds = GetTime().Tick();
        GetAudioEngine().Update(static_cast<float>(deltaSeconds));
        OnUpdate(deltaSeconds);

        // Build UI first so viewport sizes are current, then render the scene off-screen.
        m_layer.OnImGui(m_context, *this);
        if (m_layer.GetUI().ConsumeCloseRequest()) {
            Close();
        }
        m_layer.RenderViewport(*this, m_context);

        const int framebufferWidth = GetWindow().GetFramebufferWidth();
        const int framebufferHeight = GetWindow().GetFramebufferHeight();
        if (framebufferWidth > 0 && framebufferHeight > 0) {
            GetRendererAPI().SetViewport(0, 0, framebufferWidth, framebufferHeight);
        }
        GetRendererAPI().SetClearColor(m_clearColor);
        GetRendererAPI().Clear();
        m_imgui.RenderDrawData();
        GetRendererAPI().SwapBuffers();
    }
    return EXIT_SUCCESS;
}

void EditorApplication::OnUpdate(double deltaSeconds) {
    m_layer.OnUpdate(static_cast<float>(deltaSeconds), m_context);
}

void EditorApplication::RequestLayoutSave() {
    m_layer.GetUI().RequestDeferredLayoutSave();
}

void EditorApplication::RenderSceneIntoTarget(Xora2D::ColorRenderTarget& target, const glm::vec4& clearColor) {
    auto scene = m_context.GetScene();
    if (scene == nullptr || !target.IsValid()) {
        return;
    }
    scene->SetViewportSize(static_cast<float>(target.GetWidth()), static_cast<float>(target.GetHeight()));
    target.Bind();
    GetRendererAPI().SetClearColor(clearColor);
    GetRendererAPI().Clear();
    if (const auto tileCamera = m_context.GetTilemapEditor().GetViewCamera(
            m_context, static_cast<float>(target.GetWidth()), static_cast<float>(target.GetHeight()))) {
        // Tile painting uses the editor camera (zoom / pan independent of the scene camera).
        scene->OnRender(GetRenderer2D(), *tileCamera);
    } else if (m_context.IsEditingPrefab()) {
        // Prefab content has no camera of its own: frame all of its entities.
        scene->OnRender(GetRenderer2D(), FramePrefabContent(*scene, static_cast<float>(target.GetWidth()),
                                                             static_cast<float>(target.GetHeight())));
    } else {
        scene->OnRender(GetRenderer2D());
    }
    target.Unbind();
    GetGraphicsContext().MakeCurrent();
}

void EditorApplication::HandleEvent(Xora2D::Event& event) {
    if (event.GetType() == Xora2D::EventType::WindowClose || event.GetType() == Xora2D::EventType::AppQuit) {
        Xora2D::Application::HandleEvent(event);
        return;
    }
    if (event.GetType() == Xora2D::EventType::WindowResize) {
        Xora2D::Application::HandleEvent(event);
        return;
    }

    switch (event.GetType()) {
    case Xora2D::EventType::KeyPressed:
    case Xora2D::EventType::KeyReleased:
        if (!m_allowGameplayKeyboard) {
            return;
        }
        break;
    case Xora2D::EventType::MouseButtonPressed:
    case Xora2D::EventType::MouseButtonReleased:
    case Xora2D::EventType::MouseMoved:
    case Xora2D::EventType::MouseScrolled:
        if (!m_allowGameplayMouse) {
            return;
        }
        break;
    default:
        break;
    }

    Xora2D::Application::HandleEvent(event);
}

void EditorApplication::OnShutdownRequested() {
    if (m_context.IsEditingPrefab()) {
        XORA_LOG_WARN("Closing the editor while editing prefab '{}': unsaved prefab changes were discarded",
                      m_context.GetEditedPrefabName());
        m_context.ClosePrefabEditor();
    }
    // Closing during Play: run script on_destroy / stop runtime audio before the engines prepare for shutdown.
    (void)m_context.StopPlayMode();
    m_context.ShutdownContentServices();
    (void)m_context.PersistAssetRegistry();
    SaveRecentProjects();
    m_layer.Detach();
    SaveEditorConfiguration();
    Xora2D::Application::OnShutdownRequested();
}

} // namespace Xora2DEditor