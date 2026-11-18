#pragma once

#include "Xora2D/Project/ProjectConfig.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Xora2D {

enum class ProjectTemplate : std::uint8_t { Empty2D = 0, Basic2DGame, PhysicsDemo };

struct ProjectTemplateInfo {
    ProjectTemplate Template = ProjectTemplate::Empty2D;
    const char* Id = "";
    const char* DisplayName = "";
    const char* Description = "";
};

[[nodiscard]] const std::vector<ProjectTemplateInfo>& GetProjectTemplates();
[[nodiscard]] const ProjectTemplateInfo& GetProjectTemplateInfo(ProjectTemplate projectTemplate);
[[nodiscard]] std::optional<ProjectTemplate> FindProjectTemplate(std::string_view id);

struct ProjectCreateOptions {
    std::string Name;
    // The project is created in ParentDirectory/Name.
    std::filesystem::path ParentDirectory;
    ProjectTemplate Template = ProjectTemplate::Empty2D;
    // Test / diagnostics hook, called before each creation step with the step name. Returning false aborts creation
    // (used to verify that a failed creation leaves nothing behind).
    std::function<bool(std::string_view step)> BeforeStep;
};

struct ProjectCreateResult {
    bool Success = false;
    ProjectError Error = ProjectError::None;
    std::string Message;
    std::filesystem::path ProjectRoot;
    std::filesystem::path ProjectFile;
};

// Checks name and location without creating anything. Returns an empty string when creation can proceed.
[[nodiscard]] std::string ValidateProjectCreateOptions(const ProjectCreateOptions& options,
                                                       ProjectError* error = nullptr);

// Creates <Parent>/<Name>/ with <Name>.xora, Assets/{Textures,Audio,Scripts,Animations,Registry}, Scenes/, Config/
// and ProjectSettings/, plus the template's content. Everything is written into a hidden staging folder that is
// renamed into place at the end, so a failure leaves no partial project behind. An existing *empty* directory with
// the project name is reused; a non-empty one is never touched.
ProjectCreateResult CreateProject(const ProjectCreateOptions& options);

} // namespace Xora2D
