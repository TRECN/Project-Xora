#pragma once

#include "Xora2D/Project/ProjectConfig.h"

#include <filesystem>
#include <string>

namespace Xora2D {

struct ProjectFileResult {
    bool Success = false;
    ProjectError Error = ProjectError::None;
    std::string Message;
    ProjectConfig Config;
};

// Reads and writes .xora project files (YAML). Parsing validates the content: an unsupported format version,
// missing required metadata or invalid values all fail with a descriptive message.
//
//   XoraProject:
//     FormatVersion: 1
//     EngineVersion: 0.1.0
//     Name: MyGame
//     StartupScene: Scenes/Main.scene.yaml
//     Directories: {Assets: Assets, Scenes: Scenes, Scripts: Assets/Scripts, Config: Config}
//     Settings:
//       ProjectVersion: 0.1.0
//       Window: {Width: 1280, Height: 720, VSync: true}
//       Physics: {Gravity: [0, -9.81]}
//       Scripting: {Enabled: true}
class ProjectSerializer {
public:
    static ProjectFileResult Parse(const std::string& yamlText, const std::string& sourceName = "project file");
    static ProjectFileResult LoadFile(const std::filesystem::path& projectFile);

    [[nodiscard]] static std::string Emit(const ProjectConfig& config);
    // Validates, then writes atomically (temporary file + rename) so a failed save never corrupts the project file.
    static ProjectFileResult SaveFile(const ProjectConfig& config, const std::filesystem::path& projectFile);
};

} // namespace Xora2D
