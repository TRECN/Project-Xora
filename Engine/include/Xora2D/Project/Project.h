#pragma once

#include "Xora2D/Project/ProjectConfig.h"
#include "Xora2D/Project/ProjectSerializer.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Xora2D {

class Project;

struct ProjectLoadResult {
    bool Success = false;
    ProjectError Error = ProjectError::None;
    std::string Message;
    std::shared_ptr<Project> Loaded;
    // Non-fatal problems (missing startup scene, missing optional folders, ...).
    std::vector<std::string> Warnings;
};

// A loaded game project: its .xora file, root directory and configuration. Independent of the editor UI and of the
// game runtime; the editor (or a game host) decides what to do with it.
//
// All project paths are project-relative. ResolvePath() refuses anything that would leave the project root, including
// through symbolic links.
class Project {
public:
    // Validates the file (format, version, metadata) and the directory layout. The asset directory must exist; a
    // missing startup scene or missing optional folders are reported as warnings.
    static ProjectLoadResult Load(const std::filesystem::path& projectFile);

    // The single *.xora file directly inside `directory`, if there is exactly one.
    static std::optional<std::filesystem::path> FindProjectFile(const std::filesystem::path& directory);
    [[nodiscard]] static bool HasProjectFileExtension(const std::filesystem::path& path);

    [[nodiscard]] const ProjectConfig& GetConfig() const { return m_config; }
    [[nodiscard]] const std::string& GetName() const { return m_config.Name; }
    [[nodiscard]] const ProjectSettings& GetSettings() const { return m_config.Settings; }
    [[nodiscard]] const std::filesystem::path& GetProjectFile() const { return m_projectFile; }
    [[nodiscard]] const std::filesystem::path& GetRootDirectory() const { return m_rootDirectory; }

    [[nodiscard]] std::filesystem::path GetAssetDirectory() const;
    [[nodiscard]] std::filesystem::path GetSceneDirectory() const;
    [[nodiscard]] std::filesystem::path GetScriptDirectory() const;
    [[nodiscard]] std::filesystem::path GetConfigDirectory() const;
    [[nodiscard]] std::filesystem::path GetAssetRegistryPath() const;

    // Project-relative -> absolute. std::nullopt for unsafe paths or paths that escape the root.
    [[nodiscard]] std::optional<std::filesystem::path> ResolvePath(std::string_view projectRelativePath) const;
    // Absolute -> project-relative (forward slashes). std::nullopt when outside the project.
    [[nodiscard]] std::optional<std::string> MakeRelative(const std::filesystem::path& absolutePath) const;
    [[nodiscard]] bool Contains(const std::filesystem::path& absolutePath) const;

    [[nodiscard]] std::optional<std::filesystem::path> GetStartupScenePath() const;
    // True when a startup scene is configured and the file exists.
    [[nodiscard]] bool HasStartupScene() const;

    // Project-relative paths of all *.scene.yaml files in the project (sorted).
    [[nodiscard]] std::vector<std::string> ListScenes() const;

    // Creates the standard folders that are missing (never deletes anything). Returns the folders created.
    std::vector<std::string> EnsureStandardDirectories();

    // Validates and saves a new configuration; the in-memory configuration only changes when the save succeeded.
    ProjectFileResult UpdateConfig(const ProjectConfig& config);

private:
    Project(std::filesystem::path projectFile, ProjectConfig config);

    std::filesystem::path m_projectFile;
    std::filesystem::path m_rootDirectory;
    ProjectConfig m_config;
};

} // namespace Xora2D
