#pragma once

#include "Xora2DEditor/Content/EditorFileDialog.h"
#include "Xora2DEditor/Project/RecentProjects.h"

#include "Xora2D/Project/ProjectTemplates.h"

#include <array>
#include <filesystem>
#include <functional>
#include <string>

namespace Xora2D {
class Window;
}

namespace Xora2DEditor {

// Requests the Project Manager hands to its host. The host performs them between frames.
struct ProjectManagerActions {
    std::function<void(const std::filesystem::path& projectFile)> OpenProject;
    std::function<void(const Xora2D::ProjectCreateOptions& options)> CreateProject;
    // Removes the entry from the history only; project files are never touched.
    std::function<void(const std::filesystem::path& projectFile)> RemoveRecentProject;
    std::function<void()> OpenEngineSamples;
    std::function<void()> Quit;
};

// Full-window launcher shown before a project is open: recent projects, open / browse, create from a template.
class ProjectManagerUI {
public:
    ProjectManagerUI();

    void Draw(const RecentProjectList& recentProjects, Xora2D::Window* window, const ProjectManagerActions& actions);

    void ShowError(std::string title, std::string message);
    void ShowCreateForm();
    void ShowRecentProjects();
    void SetDefaultLocation(const std::filesystem::path& location);

private:
    enum class Page { Recent, Create };

    void DrawSidebar(Xora2D::Window* window, const ProjectManagerActions& actions);
    void DrawRecentProjects(const RecentProjectList& recentProjects, const ProjectManagerActions& actions);
    void DrawCreateForm(Xora2D::Window* window, const ProjectManagerActions& actions);
    void DrawErrorPopup();
    void PollDialogs(const ProjectManagerActions& actions);
    [[nodiscard]] Xora2D::ProjectCreateOptions CurrentCreateOptions() const;

    Page m_page = Page::Recent;
    std::array<char, 128> m_nameBuffer{};
    std::array<char, 1024> m_locationBuffer{};
    std::array<char, 1024> m_openPathBuffer{};
    Xora2D::ProjectTemplate m_template = Xora2D::ProjectTemplate::Basic2DGame;

    EditorFileDialog m_projectFileDialog;
    EditorFileDialog m_folderDialog;

    bool m_openErrorPopup = false;
    std::string m_errorTitle;
    std::string m_errorMessage;
};

// Default parent folder for new projects: ~/Documents if present, otherwise the home folder.
[[nodiscard]] std::filesystem::path DefaultProjectLocation();
[[nodiscard]] std::string FormatLastOpened(std::int64_t unixSeconds);

} // namespace Xora2DEditor
