#pragma once

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Xora2D {

// Version of the .xora project file layout written by this engine. Files with a newer version are rejected.
inline constexpr int kProjectFormatVersion = 1;
inline constexpr std::string_view kProjectFileExtension = ".xora";
// Format version 1 keeps assets under "<root>/Assets" (the asset registry, content browser and scene references are
// all relative to it), so the asset directory is recorded but not configurable yet.
inline constexpr std::string_view kProjectAssetDirectory = "Assets";

enum class ProjectError : std::uint8_t {
    None = 0,
    FileNotFound,
    NotAProjectFile,
    InvalidYaml,
    UnsupportedVersion,
    MissingField,
    InvalidValue,
    MissingDirectory,
    InvalidName,
    InvalidLocation,
    AlreadyExists,
    IoError,
};

[[nodiscard]] const char* ToString(ProjectError error);

// User-facing project settings (stored in the project file, edited in the Project Settings panel).
struct ProjectSettings {
    // Free-form version of the game itself (not the file format), e.g. "1.0.2".
    std::string ProjectVersion = "0.1.0";
    // Window used by the game at runtime.
    int WindowWidth = 1280;
    int WindowHeight = 720;
    bool VSync = true;
    // World gravity in meters / second^2, applied to every runtime scene of the project.
    glm::vec2 Gravity{0.0f, -9.81f};
    // When false, Python scripts are not run in Play mode even if the engine was built with scripting.
    bool ScriptingEnabled = true;

    bool operator==(const ProjectSettings&) const = default;
};

// Project-relative directories (forward slashes, no "..", never absolute).
struct ProjectDirectories {
    std::string Assets{kProjectAssetDirectory};
    std::string Scenes = "Scenes";
    std::string Scripts = "Assets/Scripts";
    std::string Config = "Config";

    bool operator==(const ProjectDirectories&) const = default;
};

// Everything stored in a .xora project file.
struct ProjectConfig {
    std::string Name;
    int FormatVersion = kProjectFormatVersion;
    // Engine version that last wrote the file (informational).
    std::string EngineVersion;
    // Project-relative path of the scene opened first, e.g. "Scenes/Main.scene.yaml". May be empty.
    std::string StartupScene;
    ProjectDirectories Directories;
    ProjectSettings Settings;

    bool operator==(const ProjectConfig&) const = default;
};

inline constexpr int kMinProjectWindowWidth = 160;
inline constexpr int kMinProjectWindowHeight = 120;
inline constexpr int kMaxProjectWindowSize = 16384;
inline constexpr float kMaxProjectGravity = 1000.0f;
inline constexpr std::size_t kMaxProjectNameLength = 64;

// Project names become directory and file names: letters, digits, spaces, '-', '_' and '.', 1-64 characters, not
// starting with '.', '-' or a space and not ending with a space or '.'. Returns false and fills `error` otherwise.
[[nodiscard]] bool IsValidProjectName(std::string_view name, std::string* error = nullptr);

// Normalizes a project-relative path ("Scenes\\Main.scene.yaml" -> "Scenes/Main.scene.yaml"). Rejects absolute
// paths, "..", drive letters / ':' and control characters. An empty input yields an empty string.
[[nodiscard]] std::optional<std::string> NormalizeProjectRelativePath(std::string_view path);

// Returns every problem with the configuration (empty when valid). Does not touch the file system.
[[nodiscard]] std::vector<std::string> ValidateProjectConfig(const ProjectConfig& config);

} // namespace Xora2D
