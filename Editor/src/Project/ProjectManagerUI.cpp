#include "Xora2DEditor/Project/ProjectManagerUI.h"

#include "Xora2D/Core/Version.h"
#include "Xora2D/Platform/Window.h"
#include "Xora2D/Project/Project.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace Xora2DEditor {

namespace {

constexpr float kSidebarWidth = 230.0f;
const ImVec4 kAccent(0.40f, 0.68f, 0.98f, 1.0f);
const ImVec4 kMuted(0.60f, 0.63f, 0.70f, 1.0f);
const ImVec4 kError(0.95f, 0.45f, 0.40f, 1.0f);
const ImVec4 kWarning(0.95f, 0.75f, 0.30f, 1.0f);

template <std::size_t N>
void CopyToBuffer(std::array<char, N>& buffer, const std::string& text) {
    std::snprintf(buffer.data(), buffer.size(), "%s", text.c_str());
}

bool WideButton(const char* label) {
    return ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f));
}

} // namespace

std::filesystem::path DefaultProjectLocation() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        home = std::getenv("USERPROFILE");
    }
    if (home == nullptr || home[0] == '\0') {
        return std::filesystem::current_path();
    }
    const std::filesystem::path documents = std::filesystem::path(home) / "Documents";
    std::error_code errorCode;
    return std::filesystem::is_directory(documents, errorCode) ? documents : std::filesystem::path(home);
}

std::string FormatLastOpened(std::int64_t unixSeconds) {
    if (unixSeconds <= 0) {
        return "-";
    }
    const std::time_t time = static_cast<std::time_t>(unixSeconds);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
    return buffer;
}

ProjectManagerUI::ProjectManagerUI() {
    CopyToBuffer(m_nameBuffer, "MyGame");
    SetDefaultLocation(DefaultProjectLocation());
}

void ProjectManagerUI::SetDefaultLocation(const std::filesystem::path& location) {
    CopyToBuffer(m_locationBuffer, location.string());
}

void ProjectManagerUI::ShowError(std::string title, std::string message) {
    m_errorTitle = std::move(title);
    m_errorMessage = std::move(message);
    m_openErrorPopup = true;
}

void ProjectManagerUI::ShowCreateForm() {
    m_page = Page::Create;
}

void ProjectManagerUI::ShowRecentProjects() {
    m_page = Page::Recent;
}

Xora2D::ProjectCreateOptions ProjectManagerUI::CurrentCreateOptions() const {
    Xora2D::ProjectCreateOptions options{};
    options.Name = m_nameBuffer.data();
    options.ParentDirectory = std::filesystem::path(m_locationBuffer.data());
    options.Template = m_template;
    return options;
}

void ProjectManagerUI::PollDialogs(const ProjectManagerActions& actions) {
    for (const std::filesystem::path& file : m_projectFileDialog.ConsumeCompletedSelections()) {
        if (actions.OpenProject) {
            actions.OpenProject(file);
        }
        break;
    }
    if (std::string error = m_projectFileDialog.ConsumeError(); !error.empty()) {
        ShowError("File dialog", error);
    }
    for (const std::filesystem::path& folder : m_folderDialog.ConsumeCompletedSelections()) {
        CopyToBuffer(m_locationBuffer, folder.string());
        break;
    }
    if (std::string error = m_folderDialog.ConsumeError(); !error.empty()) {
        ShowError("Folder dialog", error);
    }
}

void ProjectManagerUI::Draw(const RecentProjectList& recentProjects, Xora2D::Window* window,
                            const ProjectManagerActions& actions) {
    PollDialogs(actions);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (ImGui::Begin("Xora2D Project Manager", nullptr, flags)) {
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextColored(kAccent, "Xora2D");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kMuted, "Project Manager  |  engine %s", Xora2D::GetEngineVersionString());
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::BeginChild("##Sidebar", ImVec2(kSidebarWidth, 0.0f), ImGuiChildFlags_None);
        DrawSidebar(window, actions);
        ImGui::EndChild();

        ImGui::SameLine(0.0f, 20.0f);
        ImGui::BeginChild("##Page", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
        if (m_page == Page::Recent) {
            DrawRecentProjects(recentProjects, actions);
        } else {
            DrawCreateForm(window, actions);
        }
        ImGui::EndChild();

        DrawErrorPopup();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void ProjectManagerUI::DrawSidebar(Xora2D::Window* window, const ProjectManagerActions& actions) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    if (ImGui::Selectable("Recent Projects", m_page == Page::Recent)) {
        m_page = Page::Recent;
    }
    if (ImGui::Selectable("New Project", m_page == Page::Create)) {
        m_page = Page::Create;
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginDisabled(window == nullptr || m_projectFileDialog.HasPendingRequest());
    if (WideButton("Open Project...")) {
        m_projectFileDialog.RequestOpenProjectFile(*window);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Browse for a *.xora project file");

    if (WideButton("Open Engine Samples")) {
        if (actions.OpenEngineSamples) {
            actions.OpenEngineSamples();
        }
    }
    ImGui::SetItemTooltip("Open the engine's bundled Resources/ folder without a project (legacy workflow)");

    ImGui::Spacing();
    if (WideButton("Quit")) {
        if (actions.Quit) {
            actions.Quit();
        }
    }
    ImGui::PopStyleVar();
}

void ProjectManagerUI::DrawRecentProjects(const RecentProjectList& recentProjects,
                                          const ProjectManagerActions& actions) {
    ImGui::TextColored(kAccent, "Recent Projects");
    ImGui::TextColored(kMuted, "Double-click a project to open it. Removing a project here never deletes its files.");
    ImGui::Spacing();

    const auto& entries = recentProjects.GetEntries();
    if (entries.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("No recent projects yet. Create a new project or open an existing *.xora file.");
        ImGui::Spacing();
        if (ImGui::Button("Create a New Project")) {
            m_page = Page::Create;
        }
    } else {
        const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        const float tableHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 3.0f;
        if (ImGui::BeginTable("##RecentProjects", 4, tableFlags, ImVec2(0.0f, std::max(tableHeight, 120.0f)))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch, 2.4f);
            ImGui::TableSetupColumn("Last opened", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableHeadersRow();

            std::filesystem::path toOpen;
            std::filesystem::path toRemove;
            for (std::size_t index = 0; index < entries.size(); ++index) {
                const RecentProjectEntry& entry = entries[index];
                const bool available = RecentProjectList::IsAvailable(entry);
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetFrameHeightWithSpacing());

                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                const ImGuiSelectableFlags selectableFlags =
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                    ImGuiSelectableFlags_AllowDoubleClick;
                if (!available) {
                    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
                }
                if (ImGui::Selectable(entry.Name.c_str(), false, selectableFlags) && available &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    toOpen = entry.ProjectFile;
                }
                if (!available) {
                    ImGui::PopStyleColor();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                if (available) {
                    ImGui::TextUnformatted(entry.ProjectFile.parent_path().string().c_str());
                    ImGui::SetItemTooltip("%s", entry.ProjectFile.string().c_str());
                } else {
                    ImGui::TextColored(kWarning, "Missing:");
                    ImGui::SameLine();
                    ImGui::TextColored(kMuted, "%s", entry.ProjectFile.string().c_str());
                    ImGui::SetItemTooltip("The project was moved or deleted. Remove it from the list, or open it "
                                          "from its new location with Open Project...");
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(kMuted, "%s", FormatLastOpened(entry.LastOpenedUnixSeconds).c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::BeginDisabled(!available);
                if (ImGui::Button("Open")) {
                    toOpen = entry.ProjectFile;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Remove")) {
                    toRemove = entry.ProjectFile;
                }
                ImGui::SetItemTooltip("Remove from this list (project files are kept)");
                ImGui::PopID();
            }
            ImGui::EndTable();

            if (!toOpen.empty() && actions.OpenProject) {
                actions.OpenProject(toOpen);
            }
            if (!toRemove.empty() && actions.RemoveRecentProject) {
                actions.RemoveRecentProject(toRemove);
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Open by path");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-90.0f);
    const bool submitted = ImGui::InputTextWithHint("##OpenPath", "/path/to/MyGame/MyGame.xora", m_openPathBuffer.data(),
                                                    m_openPathBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool hasPath = m_openPathBuffer[0] != '\0';
    ImGui::BeginDisabled(!hasPath);
    if ((ImGui::Button("Open##ByPath", ImVec2(-FLT_MIN, 0.0f)) || submitted) && hasPath && actions.OpenProject) {
        std::filesystem::path path(m_openPathBuffer.data());
        // A project folder works too when it contains exactly one *.xora file.
        std::error_code errorCode;
        if (std::filesystem::is_directory(path, errorCode)) {
            if (const auto found = Xora2D::Project::FindProjectFile(path)) {
                path = *found;
            }
        }
        actions.OpenProject(path);
    }
    ImGui::EndDisabled();
}

void ProjectManagerUI::DrawCreateForm(Xora2D::Window* window, const ProjectManagerActions& actions) {
    ImGui::TextColored(kAccent, "Create a New Project");
    ImGui::TextColored(kMuted, "The project is created in its own folder: <Location>/<Name>/<Name>.xora");
    ImGui::Spacing();

    const float labelWidth = 110.0f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("##ProjectName", m_nameBuffer.data(), m_nameBuffer.size());

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Location");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-110.0f);
    ImGui::InputText("##ProjectLocation", m_locationBuffer.data(), m_locationBuffer.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(window == nullptr || m_folderDialog.HasPendingRequest());
    if (ImGui::Button("Browse...", ImVec2(-FLT_MIN, 0.0f))) {
        m_folderDialog.RequestOpenFolder(*window, std::filesystem::path(m_locationBuffer.data()));
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Template");
    ImGui::SameLine(labelWidth);
    ImGui::BeginGroup();
    for (const Xora2D::ProjectTemplateInfo& info : Xora2D::GetProjectTemplates()) {
        ImGui::PushID(info.Id);
        const bool selected = m_template == info.Template;
        if (ImGui::RadioButton(info.DisplayName, selected)) {
            m_template = info.Template;
        }
        ImGui::Indent(ImGui::GetFrameHeight() + 4.0f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 520.0f);
        ImGui::TextColored(kMuted, "%s", info.Description);
        ImGui::PopTextWrapPos();
        ImGui::Unindent(ImGui::GetFrameHeight() + 4.0f);
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const Xora2D::ProjectCreateOptions options = CurrentCreateOptions();
    const std::string problem = Xora2D::ValidateProjectCreateOptions(options);
    if (problem.empty()) {
        ImGui::TextColored(kMuted, "Will create:");
        ImGui::SameLine();
        ImGui::TextUnformatted(
            (options.ParentDirectory / options.Name / (options.Name + ".xora")).string().c_str());
    } else {
        ImGui::TextColored(kError, "%s", problem.c_str());
    }
    ImGui::Spacing();

    ImGui::BeginDisabled(!problem.empty());
    if (ImGui::Button("Create Project", ImVec2(160.0f, 0.0f)) && actions.CreateProject) {
        actions.CreateProject(options);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
        m_page = Page::Recent;
    }
}

void ProjectManagerUI::DrawErrorPopup() {
    if (m_openErrorPopup) {
        ImGui::OpenPopup("Project Error");
        m_openErrorPopup = false;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Project Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(kError, "%s", m_errorTitle.c_str());
        ImGui::Spacing();
        ImGui::PushTextWrapPos(500.0f);
        ImGui::TextUnformatted(m_errorMessage.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace Xora2DEditor
