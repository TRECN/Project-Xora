#include "Xora2DEditor/EditorUI.h"

#include "Xora2DEditor/EditorApplication.h"
#include "Xora2DEditor/EditorContext.h"

#include "Xora2DEditor/Panels/ProjectSettingsPanel.h"
#include "Xora2DEditor/PrefabEditorUI.h"

#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Project/Project.h"

#include <cstdio>

#include <imgui.h>
#include <imgui_internal.h>

namespace Xora2DEditor {
namespace {

// Stable dockspace id — must not depend on the active window ID stack (unlike GetID() from outside the host).
const ImGuiID kEditorMainDockSpaceId = ImHashStr("Xora2D.Editor.MainDockSpace");

constexpr float kToolbarHeight = 34.0f;
constexpr float kStatusBarHeight = 22.0f;

void BuildDefaultDockLayout(ImGuiID dockSpaceId, const ImVec2& dockSpaceSize) {
    ImGui::DockBuilderRemoveNode(dockSpaceId);
    ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockSpaceId, dockSpaceSize);

    ImGuiID dockCenter = dockSpaceId;
    ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Left, 0.20f, nullptr, &dockCenter);
    // 25% of total width on the right => 25% / 80% of the remaining strip.
    ImGuiID dockRight =
        ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Right, 0.25f / 0.80f, nullptr, &dockCenter);
    ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.34f, nullptr, &dockCenter);

    ImGui::DockBuilderDockWindow("Viewport", dockCenter);
    ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
    ImGui::DockBuilderDockWindow("Inspector", dockRight);
    ImGui::DockBuilderDockWindow("Content", dockBottom);
    ImGui::DockBuilderDockWindow("Console", dockBottom);
    ImGui::DockBuilderFinish(dockSpaceId);
}

// Play / Pause / Resume / Step / Stop. Invalid transitions are disabled; the badge shows the current mode.
void DrawPlayControls(EditorContext& context) {
    const EditorPlaySession& session = context.GetPlaySession();
    const EditorPlayState state = session.GetState();

    const auto toolbarButton = [](const char* label, bool enabled, const char* tooltip, const ImVec4* tint) {
        ImGui::BeginDisabled(!enabled);
        if (tint != nullptr) {
            ImGui::PushStyleColor(ImGuiCol_Button, *tint);
        }
        const bool pressed = ImGui::Button(label);
        if (tint != nullptr) {
            ImGui::PopStyleColor();
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("%s", tooltip);
        ImGui::SameLine();
        return pressed;
    };

    const ImVec4 playTint(0.18f, 0.45f, 0.24f, 1.0f);
    const ImVec4 stopTint(0.50f, 0.20f, 0.20f, 1.0f);
    if (toolbarButton("Play", session.CanPlay(), "Run a copy of the scene (Ctrl/Cmd+P)", session.CanPlay() ? &playTint : nullptr)) {
        (void)context.StartPlayMode();
    }
    if (toolbarButton("Pause", session.CanPause(), "Pause the simulation (Ctrl/Cmd+Shift+P)", nullptr)) {
        (void)context.PausePlayMode();
    }
    if (toolbarButton("Resume", session.CanResume(), "Resume the simulation (Ctrl/Cmd+Shift+P)", nullptr)) {
        (void)context.ResumePlayMode();
    }
    if (toolbarButton("Step Frame", session.CanStep(), "Advance exactly one simulation frame while paused", nullptr)) {
        (void)context.StepPlayMode();
    }
    if (toolbarButton("Stop", session.CanStop(), "Stop and restore the edited scene (Ctrl/Cmd+P)", session.CanStop() ? &stopTint : nullptr)) {
        (void)context.StopPlayMode();
    }

    ImGui::SameLine(0.0f, 16.0f);
    ImVec4 badge(0.55f, 0.58f, 0.65f, 1.0f);
    const char* label = "EDIT MODE";
    if (state == EditorPlayState::Playing) {
        badge = ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
        label = "PLAYING";
    } else if (state == EditorPlayState::Paused) {
        badge = ImVec4(0.95f, 0.75f, 0.25f, 1.0f);
        label = "PAUSED";
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(badge, "%s", label);
    if (session.IsActive()) {
        ImGui::SameLine();
        ImGui::TextDisabled("frame %llu  |  runtime copy - changes are discarded on Stop",
                            static_cast<unsigned long long>(session.GetSimulatedFrameCount()));
    }
}

} // namespace

EditorUI::EditorUI(EditorWorkspace& workspace)
    : m_workspace(workspace) {}

void EditorUI::SetApplyDefaultLayoutOnNextFrame(bool apply) {
    m_applyDefaultLayout = apply;
}

void EditorUI::RequestResetLayout() {
    m_applyDefaultLayout = true;
    m_deferredLayoutSave = true;
}

void EditorUI::RequestDeferredLayoutSave() {
    m_deferredLayoutSave = true;
}

void EditorUI::OnImGui(EditorContext& context, EditorApplication& application) {
    if (const auto panelId = context.ConsumePanelRequest()) {
        if (EditorPanel* panel = m_workspace.FindPanel(*panelId)) {
            panel->SetVisible(true);
            ImGui::SetWindowFocus(panel->GetTitle());
        }
    }
    for (const std::filesystem::path& file : m_projectDialog.ConsumeCompletedSelections()) {
        application.RequestOpenProject(file);
        break;
    }
    if (std::string error = m_projectDialog.ConsumeError(); !error.empty()) {
        ShowStatusMessage(error);
    }

    DrawMainMenu(context, application);
    DrawToolbar(context);
    DrawDockspace();
    m_workspace.DrawPanels(context);
    DrawScenePathPopup(context);
    DrawStartupScenePopup(context);

    DrawStatusBar(context, application);
    PersistLayoutIfNeeded();
}

void EditorUI::ReportSceneResult(bool success, const std::string& message) {
    ShowStatusMessage(message);
    if (success) {
        XORA_LOG_INFO("{}", message);
    } else {
        XORA_LOG_WARN("{}", message);
    }
}

void EditorUI::RequestSaveScene(EditorContext& context, bool saveAs) {
    if (!context.IsContentActive()) {
        return;
    }
    if (context.IsPlayModeActive()) {
        ShowStatusMessage("Stop Play mode before saving (the runtime copy is never saved).");
        return;
    }
    if (context.IsEditingPrefab()) {
        // Cmd/Ctrl+S while editing a prefab saves the prefab.
        const EditorSceneFileResult save = context.SavePrefabEditor();
        ReportPrefabResult(save.Success, save.Message);
        ShowStatusMessage(save.Message);
        return;
    }
    if (saveAs || context.GetScenePath().empty()) {
        const std::string current =
            context.GetScenePath().empty() ? std::string("Scenes/NewScene.scene.yaml") : context.GetScenePath();
        std::snprintf(m_scenePathBuffer.data(), m_scenePathBuffer.size(), "%s", current.c_str());
        m_scenePathMode = ScenePathMode::SaveAs;
        m_scenePathError.clear();
        m_openScenePathPopup = true;
        return;
    }
    const EditorSceneFileResult result = context.SaveScene();
    ReportSceneResult(result.Success, result.Message);
}

void EditorUI::NotifyStartupSceneMissing(EditorContext& context, std::string missingScene) {
    m_missingStartupScene = std::move(missingScene);
    m_availableScenes = context.HasProject() ? context.GetProject()->ListScenes() : std::vector<std::string>{};
    m_selectedScene = m_availableScenes.empty() ? -1 : 0;
    const std::string suggestion =
        m_missingStartupScene.empty() ? std::string("Scenes/Main.scene.yaml") : m_missingStartupScene;
    std::snprintf(m_newStartupSceneBuffer.data(), m_newStartupSceneBuffer.size(), "%s", suggestion.c_str());
    m_startupSceneError.clear();
    m_openStartupScenePopup = true;
}

void EditorUI::DrawScenePathPopup(EditorContext& context) {
    if (m_openScenePathPopup) {
        ImGui::OpenPopup("Scene File");
        m_openScenePathPopup = false;
    }
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Scene File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const bool newScene = m_scenePathMode == ScenePathMode::NewScene;
    ImGui::TextUnformatted(newScene ? "Create a new scene" : "Save the edited scene as");
    ImGui::TextDisabled("Project-relative path ending in .scene.yaml");
    ImGui::SetNextItemWidth(420.0f);
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    const bool submitted = ImGui::InputText("##ScenePath", m_scenePathBuffer.data(), m_scenePathBuffer.size(),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    if (!m_scenePathError.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", m_scenePathError.c_str());
    }
    if (ImGui::Button(newScene ? "Create" : "Save", ImVec2(110.0f, 0.0f)) || submitted) {
        const EditorSceneFileResult result = newScene ? context.CreateScene(m_scenePathBuffer.data(), false)
                                                      : context.SaveSceneAs(m_scenePathBuffer.data());
        if (result.Success) {
            ReportSceneResult(true, result.Message);
            m_scenePathMode = ScenePathMode::None;
            ImGui::CloseCurrentPopup();
        } else {
            m_scenePathError = result.Message;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
        m_scenePathMode = ScenePathMode::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorUI::DrawStartupScenePopup(EditorContext& context) {
    if (m_openStartupScenePopup) {
        ImGui::OpenPopup("Startup Scene Missing");
        m_openStartupScenePopup = false;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Startup Scene Missing", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::PushTextWrapPos(500.0f);
    if (m_missingStartupScene.empty()) {
        ImGui::TextUnformatted("This project has no startup scene.");
    } else {
        ImGui::Text("The startup scene '%s' could not be opened (it may have been moved or deleted).",
                    m_missingStartupScene.c_str());
    }
    ImGui::TextDisabled("An unsaved scene with only a camera is open. Choose how to continue:");
    ImGui::PopTextWrapPos();

    ImGui::SeparatorText("Create a new startup scene");
    ImGui::SetNextItemWidth(360.0f);
    ImGui::InputText("##NewStartupScene", m_newStartupSceneBuffer.data(), m_newStartupSceneBuffer.size());
    ImGui::SameLine();
    if (ImGui::Button("Create")) {
        const EditorSceneFileResult result = context.CreateScene(m_newStartupSceneBuffer.data(), true);
        if (result.Success) {
            ReportSceneResult(true, result.Message + " (startup scene)");
            ImGui::CloseCurrentPopup();
        } else {
            m_startupSceneError = result.Message;
        }
    }

    ImGui::SeparatorText("Use an existing scene");
    if (m_availableScenes.empty()) {
        ImGui::TextDisabled("The project contains no other scenes.");
    } else {
        const char* preview = m_selectedScene >= 0 ? m_availableScenes[static_cast<std::size_t>(m_selectedScene)].c_str() : "";
        ImGui::SetNextItemWidth(360.0f);
        if (ImGui::BeginCombo("##ExistingScene", preview)) {
            for (std::size_t index = 0; index < m_availableScenes.size(); ++index) {
                if (ImGui::Selectable(m_availableScenes[index].c_str(), static_cast<int>(index) == m_selectedScene)) {
                    m_selectedScene = static_cast<int>(index);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Open") && m_selectedScene >= 0) {
            const std::string& scene = m_availableScenes[static_cast<std::size_t>(m_selectedScene)];
            const auto absolute = context.GetProject()->ResolvePath(scene);
            const EditorSceneFileResult open =
                absolute ? context.OpenSceneFile(*absolute) : EditorSceneFileResult{false, "Invalid scene path"};
            const EditorSceneFileResult startup = open.Success ? context.SetStartupScene(scene) : open;
            if (open.Success && startup.Success) {
                ReportSceneResult(true, startup.Message);
                ImGui::CloseCurrentPopup();
            } else {
                m_startupSceneError = startup.Message;
            }
        }
    }

    if (!m_startupSceneError.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", m_startupSceneError.c_str());
    }
    ImGui::Separator();
    if (ImGui::Button("Continue Without a Startup Scene")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

bool EditorUI::ConsumeCloseRequest() {
    const bool value = m_requestClose;
    m_requestClose = false;
    return value;
}

void EditorUI::ShowStatusMessage(std::string message) {
    m_statusMessage = std::move(message);
}

void EditorUI::DrawMainMenu(EditorContext& context, EditorApplication& application) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            Xora2D::Window* window = context.GetHostServices().MainWindow;
            if (ImGui::MenuItem("New Project...")) {
                application.RequestShowProjectManager(true);
            }
            if (ImGui::MenuItem("Open Project...", nullptr, false, window != nullptr && !m_projectDialog.HasPendingRequest())) {
                m_projectDialog.RequestOpenProjectFile(*window);
            }
            if (ImGui::BeginMenu("Open Recent", !application.GetRecentProjects().GetEntries().empty())) {
                for (const RecentProjectEntry& entry : application.GetRecentProjects().GetEntries()) {
                    const bool available = RecentProjectList::IsAvailable(entry);
                    const std::string label = entry.Name + "##" + entry.ProjectFile.string();
                    if (ImGui::MenuItem(label.c_str(), nullptr, false, available)) {
                        application.RequestOpenProject(entry.ProjectFile);
                    }
                    ImGui::SetItemTooltip("%s%s", entry.ProjectFile.string().c_str(), available ? "" : " (missing)");
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Close Project")) {
                application.RequestShowProjectManager(false);
            }
            ImGui::Separator();
            const bool playing = context.IsPlayModeActive();
            if (ImGui::MenuItem("New Scene...", nullptr, false, !playing)) {
                std::snprintf(m_scenePathBuffer.data(), m_scenePathBuffer.size(), "%s", "Scenes/NewScene.scene.yaml");
                m_scenePathMode = ScenePathMode::NewScene;
                m_scenePathError.clear();
                m_openScenePathPopup = true;
            }
            if (ImGui::MenuItem("Save Scene", "Cmd/Ctrl+S", false, !playing)) {
                RequestSaveScene(context, false);
            }
            if (ImGui::MenuItem("Save Scene As...", "Cmd/Ctrl+Shift+S", false, !playing)) {
                RequestSaveScene(context, true);
            }
            ImGui::Separator();
            if (EditorPanel* settings = m_workspace.FindPanel("ProjectSettings")) {
                if (ImGui::MenuItem("Project Settings...", nullptr, false, context.HasProject())) {
                    settings->SetVisible(true);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                m_requestClose = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Cmd/Ctrl+Z", false, false)) {}
            if (ImGui::MenuItem("Redo", "Cmd/Ctrl+Shift+Z", false, false)) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            for (const char* id : {"Viewport", "Hierarchy", "Inspector", "ContentBrowser", "Console", "ProjectSettings", "Tilemap",
                                   "Tileset"}) {
                if (EditorPanel* panel = m_workspace.FindPanel(id)) {
                    bool visible = panel->IsVisible();
                    if (ImGui::MenuItem(panel->GetTitle(), nullptr, visible)) {
                        panel->SetVisible(!visible);
                    }
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            if (ImGui::MenuItem("Reset Layout")) {
                RequestResetLayout();
                ShowStatusMessage("Default workspace layout restored.");
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Documentation", nullptr, false, false)) {}
            ImGui::Separator();
            ImGui::MenuItem("About Xora2D Editor", nullptr, false, false);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void EditorUI::DrawToolbar(EditorContext& context) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, kToolbarHeight));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("##Toolbar", nullptr, flags)) {
        if (context.IsEditingPrefab()) {
            DrawPrefabEditBar(context);
        } else {
            DrawPlayControls(context);
        }
    }
    ImGui::End();
}

void EditorUI::DrawDockspace() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float yOffset = ImGui::GetFrameHeight() + kToolbarHeight;
    const float availableHeight = viewport->WorkSize.y - yOffset - kStatusBarHeight;
    const ImVec2 dockPos(viewport->WorkPos.x, viewport->WorkPos.y + yOffset);
    const ImVec2 dockSize(viewport->WorkSize.x, availableHeight > 0.0f ? availableHeight : 1.0f);

    ImGui::SetNextWindowPos(dockPos);
    ImGui::SetNextWindowSize(dockSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("EditorDockSpaceHost", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    ImGui::DockSpace(kEditorMainDockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    if (m_applyDefaultLayout) {
        BuildDefaultDockLayout(kEditorMainDockSpaceId, dockSize);
        m_applyDefaultLayout = false;
        m_deferredLayoutSave = true;
    }

    ImGui::End();
}

void EditorUI::PersistLayoutIfNeeded() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.IniFilename == nullptr) {
        return;
    }
    if (!m_deferredLayoutSave && !io.WantSaveIniSettings) {
        return;
    }
    ImGui::SaveIniSettingsToDisk(io.IniFilename);
    m_deferredLayoutSave = false;
}

void EditorUI::DrawStatusBar(EditorContext& context, const EditorApplication& application) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - kStatusBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, kStatusBarHeight));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;

    if (ImGui::Begin("##StatusBar", nullptr, flags)) {
        if (!m_statusMessage.empty()) {
            ImGui::TextUnformatted(m_statusMessage.c_str());
            ImGui::SameLine();
        }
        ImGui::Text("Xora2D Editor | %s", application.GetGraphicsInfo().renderer.c_str());
        ImGui::SameLine();
        if (context.IsEditingPrefab()) {
            ImGui::Text("| Editing prefab: %s", context.GetEditedPrefabName().c_str());
        } else if (const auto scene = context.GetScene()) {
            const std::string& path = context.GetScenePath();
            ImGui::Text("| Scene: %s (%s) [%s]", scene->GetSceneName().c_str(),
                        path.empty() ? "unsaved" : path.c_str(), ToString(context.GetPlayState()));
        }
        ImGui::SameLine();
        if (context.HasProject()) {
            ImGui::Text("| Project: %s", context.GetProject()->GetName().c_str());
        } else {
            ImGui::Text("| Engine samples: %s", Xora2D::GetContentRoot().string().c_str());
        }
    }
    ImGui::End();
}

} // namespace Xora2DEditor