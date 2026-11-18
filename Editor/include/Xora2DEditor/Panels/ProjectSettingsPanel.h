#pragma once

#include "Xora2DEditor/EditorPanel.h"

#include "Xora2D/Project/ProjectConfig.h"

#include <memory>
#include <string>
#include <vector>

namespace Xora2D {
class Project;
}

namespace Xora2DEditor {

// Edits the open project's settings on a draft copy; Apply validates and saves the .xora file.
class ProjectSettingsPanel final : public EditorPanel {
public:
    ProjectSettingsPanel();

    [[nodiscard]] const char* GetTitle() const override { return "Project Settings"; }
    [[nodiscard]] std::string GetId() const override { return "ProjectSettings"; }

    void OnImGui(EditorContext& context) override;

private:
    void Refresh(const std::shared_ptr<Xora2D::Project>& project);

    std::weak_ptr<Xora2D::Project> m_project;
    Xora2D::ProjectConfig m_draft;
    Xora2D::ProjectConfig m_baseline;
    std::vector<std::string> m_scenes;
    std::string m_status;
    bool m_statusIsError = false;
};

} // namespace Xora2DEditor
