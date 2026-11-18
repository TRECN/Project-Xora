#pragma once

#include "Xora2D/Core/Application.h"
#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/EditorLayer.h"
#include "Xora2DEditor/ImGuiIntegration.h"
#include "Xora2DEditor/Project/ProjectManagerUI.h"
#include "Xora2DEditor/Project/RecentProjects.h"

#include "Xora2D/Project/ProjectTemplates.h"

#include <glm/vec4.hpp>

#include <filesystem>
#include <memory>
#include <cstdint>
#include <optional>
#include <vector>

namespace Xora2D {
class ColorRenderTarget;
}

namespace Xora2DEditor {

struct EditorLaunchOptions {
    // Opened directly, skipping the Project Manager (--project <file>).
    std::filesystem::path ProjectFile;
};

class EditorApplication final : public Xora2D::Application {
public:
    explicit EditorApplication(EditorLaunchOptions options = {});
    ~EditorApplication() override;

    int Run() override;

    void RenderSceneIntoTarget(Xora2D::ColorRenderTarget& target, const glm::vec4& clearColor);
    void RequestLayoutSave();

    // Project actions run between frames (never while ImGui is drawing with the current project's textures).
    void RequestOpenProject(const std::filesystem::path& projectFile);
    // Closes the current project and returns to the Project Manager (create page when `createProject`).
    void RequestShowProjectManager(bool createProject);
    [[nodiscard]] const RecentProjectList& GetRecentProjects() const { return m_recentProjects; }

protected:
    void OnUpdate(double deltaSeconds) override;
    void HandleEvent(Xora2D::Event& event) override;
    void OnShutdownRequested() override;

private:
    struct PendingProjectAction {
        enum class Kind : std::uint8_t { Open, Create, Close, OpenEngineSamples };
        Kind Type = Kind::Open;
        std::filesystem::path ProjectFile;
        Xora2D::ProjectCreateOptions Create;
        bool ShowCreatePage = false;
    };

    void WireRuntimeScene(Xora2D::Scene& scene);
    void UpdateGameplayInputGate();
    void OpenSceneAsset(const std::filesystem::path& sceneFile);
    void LoadEditorConfiguration();
    void SaveEditorConfiguration();

    void ProcessPendingProjectAction();
    bool OpenProjectFile(const std::filesystem::path& projectFile);
    void CreateAndOpenProject(const Xora2D::ProjectCreateOptions& options);
    void OnProjectChanged();
    void SaveRecentProjects();
    void DrawProjectManager();

    EditorContext m_context;
    EditorLayer m_layer;
    ImGuiIntegration m_imgui;
    // Gameplay input reaches the InputManager only while playing with the viewport focused (keys) / hovered (mouse).
    bool m_allowGameplayKeyboard = false;
    bool m_allowGameplayMouse = false;
    std::vector<std::filesystem::path> m_droppedFiles;

    RecentProjectList m_recentProjects;
    ProjectManagerUI m_projectManager;
    std::optional<PendingProjectAction> m_pendingProjectAction;
};

} // namespace Xora2DEditor