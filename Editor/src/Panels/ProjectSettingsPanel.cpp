#include "Xora2DEditor/Panels/ProjectSettingsPanel.h"

#include "Xora2DEditor/EditorContext.h"

#include "Xora2D/Project/Project.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace Xora2DEditor {

namespace {

const ImVec4 kMuted(0.60f, 0.63f, 0.70f, 1.0f);
const ImVec4 kError(0.95f, 0.45f, 0.40f, 1.0f);
const ImVec4 kOk(0.45f, 0.85f, 0.50f, 1.0f);

bool EditString(const char* label, std::string& value, std::size_t capacity = 128) {
    std::array<char, 256> buffer{};
    std::snprintf(buffer.data(), std::min(buffer.size(), capacity), "%s", value.c_str());
    if (ImGui::InputText(label, buffer.data(), std::min(buffer.size(), capacity))) {
        value = buffer.data();
        return true;
    }
    return false;
}

void ReadOnlyRow(const char* label, const std::string& value) {
    ImGui::TextColored(kMuted, "%s", label);
    ImGui::SameLine(170.0f);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
}

} // namespace

ProjectSettingsPanel::ProjectSettingsPanel() {
    // Opened from File > Project Settings.
    SetVisible(false);
}

void ProjectSettingsPanel::Refresh(const std::shared_ptr<Xora2D::Project>& project) {
    m_project = project;
    m_draft = project->GetConfig();
    m_scenes = project->ListScenes();
}

void ProjectSettingsPanel::OnImGui(EditorContext& context) {
    bool visible = IsVisible();
    ImGui::SetNextWindowSize(ImVec2(560.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Project Settings", &visible)) {
        ImGui::End();
        SetVisible(visible);
        return;
    }
    SetVisible(visible);

    const std::shared_ptr<Xora2D::Project>& project = context.GetProject();
    if (project == nullptr) {
        ImGui::TextWrapped("No project is open. Project settings are available once a project is opened from the "
                           "Project Manager (File > Close Project).");
        ImGui::End();
        return;
    }
    if (m_project.lock() != project) {
        Refresh(project);
        m_baseline = project->GetConfig();
        m_status.clear();
    }

    const Xora2D::ProjectConfig& saved = project->GetConfig();
    if (!(m_baseline == saved)) {
        // Changed elsewhere (e.g. a new startup scene was chosen): follow it unless the draft has local edits.
        if (m_draft == m_baseline) {
            m_draft = saved;
        }
        m_baseline = saved;
        m_scenes = project->ListScenes();
    }
    const bool dirty = !(m_draft == saved);

    ImGui::SeparatorText("Project");
    ImGui::SetNextItemWidth(260.0f);
    EditString("Name", m_draft.Name, 65);
    ImGui::SetItemTooltip("Display name. The project folder and .xora file keep their names.");
    ImGui::SetNextItemWidth(160.0f);
    EditString("Project version", m_draft.Settings.ProjectVersion, 33);

    const std::string preview = m_draft.StartupScene.empty() ? std::string("(none)") : m_draft.StartupScene;
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::BeginCombo("Startup scene", preview.c_str())) {
        if (ImGui::Selectable("(none)", m_draft.StartupScene.empty())) {
            m_draft.StartupScene.clear();
        }
        for (const std::string& scene : m_scenes) {
            if (ImGui::Selectable(scene.c_str(), scene == m_draft.StartupScene)) {
                m_draft.StartupScene = scene;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan")) {
        m_scenes = project->ListScenes();
    }
    if (!m_draft.StartupScene.empty() && !project->ResolvePath(m_draft.StartupScene).has_value()) {
        ImGui::TextColored(kError, "Startup scene path is not inside the project");
    }

    ImGui::SeparatorText("Paths (project-relative)");
    ReadOnlyRow("Project root", project->GetRootDirectory().string());
    ReadOnlyRow("Assets", m_draft.Directories.Assets);
    ReadOnlyRow("Scenes", m_draft.Directories.Scenes);
    ReadOnlyRow("Scripts", m_draft.Directories.Scripts);
    ReadOnlyRow("Config", m_draft.Directories.Config);

    ImGui::SeparatorText("Runtime window");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Width", &m_draft.Settings.WindowWidth, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Height", &m_draft.Settings.WindowHeight, 0);
    ImGui::Checkbox("VSync", &m_draft.Settings.VSync);
    ImGui::SetItemTooltip("Also applied to the editor while this project is open");

    ImGui::SeparatorText("Physics");
    ImGui::SetNextItemWidth(260.0f);
    ImGui::DragFloat2("Gravity (m/s^2)", &m_draft.Settings.Gravity.x, 0.05f, -Xora2D::kMaxProjectGravity,
                      Xora2D::kMaxProjectGravity, "%.2f");
    ImGui::SetItemTooltip("Applied to every runtime scene when Play starts");

    ImGui::SeparatorText("Scripting");
#if XORA2D_SCRIPTING_ENABLED
    ImGui::Checkbox("Run Python scripts in Play mode", &m_draft.Settings.ScriptingEnabled);
#else
    ImGui::BeginDisabled();
    ImGui::Checkbox("Run Python scripts in Play mode", &m_draft.Settings.ScriptingEnabled);
    ImGui::EndDisabled();
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("This editor was built without Python scripting; the setting is kept for other builds.");
    ImGui::PopStyleColor();
#endif

    ImGui::SeparatorText("File");
    ReadOnlyRow("Project file", project->GetProjectFile().filename().string());
    ReadOnlyRow("Format version", std::to_string(m_draft.FormatVersion));
    ReadOnlyRow("Saved by engine", m_draft.EngineVersion.empty() ? std::string("-") : m_draft.EngineVersion);

    ImGui::Spacing();
    const std::vector<std::string> errors = Xora2D::ValidateProjectConfig(m_draft);
    for (const std::string& error : errors) {
        ImGui::TextColored(kError, "%s", error.c_str());
    }

    ImGui::BeginDisabled(!dirty || !errors.empty());
    if (ImGui::Button("Apply")) {
        const Xora2D::ProjectFileResult result = context.UpdateProjectConfig(m_draft);
        m_statusIsError = !result.Success;
        m_status = result.Message;
        if (result.Success) {
            m_draft = project->GetConfig();
            m_baseline = m_draft;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Revert")) {
        m_draft = saved;
        m_status.clear();
    }
    ImGui::EndDisabled();
    if (dirty) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "Unsaved changes");
    }
    if (!m_status.empty()) {
        ImGui::TextColored(m_statusIsError ? kError : kOk, "%s", m_status.c_str());
    }
    ImGui::End();
}

} // namespace Xora2DEditor
