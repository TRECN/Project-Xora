#include "Xora2D/Project/Project.h"

#include "Xora2D/Assets/AssetPaths.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace Xora2D {

namespace {

std::filesystem::path CanonicalOrAbsolute(const std::filesystem::path& path) {
    std::error_code errorCode;
    std::filesystem::path absolute = std::filesystem::absolute(path, errorCode);
    if (errorCode) {
        absolute = path;
    }
    std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, errorCode);
    return errorCode ? absolute.lexically_normal() : canonical;
}

bool IsDirectory(const std::filesystem::path& path) {
    std::error_code errorCode;
    return std::filesystem::is_directory(path, errorCode);
}

} // namespace

Project::Project(std::filesystem::path projectFile, ProjectConfig config)
    : m_projectFile(std::move(projectFile)),
      m_rootDirectory(m_projectFile.parent_path()),
      m_config(std::move(config)) {}

bool Project::HasProjectFileExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension == kProjectFileExtension;
}

ProjectLoadResult Project::Load(const std::filesystem::path& projectFile) {
    ProjectLoadResult result{};
    if (projectFile.empty()) {
        result.Error = ProjectError::FileNotFound;
        result.Message = "No project file given";
        return result;
    }
    if (!HasProjectFileExtension(projectFile)) {
        result.Error = ProjectError::NotAProjectFile;
        result.Message = "Not a Xora2D project file (expected a *.xora file): " + projectFile.string();
        return result;
    }
    const std::filesystem::path canonicalFile = CanonicalOrAbsolute(projectFile);
    ProjectFileResult file = ProjectSerializer::LoadFile(canonicalFile);
    if (!file.Success) {
        result.Error = file.Error;
        result.Message = file.Message;
        return result;
    }

    std::shared_ptr<Project> project(new Project(canonicalFile, std::move(file.Config)));
    if (!IsDirectory(project->GetAssetDirectory())) {
        result.Error = ProjectError::MissingDirectory;
        result.Message = "Project '" + project->GetName() + "' is missing its asset directory '" +
                         project->GetConfig().Directories.Assets + "' (expected at " +
                         project->GetAssetDirectory().string() + ")";
        return result;
    }

    const ProjectDirectories& directories = project->GetConfig().Directories;
    for (const auto& [label, relative] : {std::pair<const char*, const std::string*>{"Scene", &directories.Scenes},
                                          {"Script", &directories.Scripts},
                                          {"Config", &directories.Config}}) {
        const std::optional<std::filesystem::path> absolute = project->ResolvePath(*relative);
        if (!absolute.has_value() || !IsDirectory(*absolute)) {
            result.Warnings.push_back(std::string(label) + " directory '" + *relative + "' is missing");
        }
    }
    const std::string& startupScene = project->GetConfig().StartupScene;
    if (startupScene.empty()) {
        result.Warnings.push_back("No startup scene is configured");
    } else if (!project->HasStartupScene()) {
        result.Warnings.push_back("Startup scene '" + startupScene + "' was not found");
    }

    result.Success = true;
    result.Message = "Opened project '" + project->GetName() + "'";
    result.Loaded = std::move(project);
    return result;
}

std::optional<std::filesystem::path> Project::FindProjectFile(const std::filesystem::path& directory) {
    std::error_code errorCode;
    std::optional<std::filesystem::path> found;
    for (std::filesystem::directory_iterator iterator(directory, errorCode), end; !errorCode && iterator != end;
         iterator.increment(errorCode)) {
        std::error_code statError;
        if (!iterator->is_regular_file(statError) || !HasProjectFileExtension(iterator->path())) {
            continue;
        }
        if (found.has_value()) {
            return std::nullopt; // ambiguous
        }
        found = iterator->path();
    }
    return found;
}

std::filesystem::path Project::GetAssetDirectory() const {
    return m_rootDirectory / m_config.Directories.Assets;
}

std::filesystem::path Project::GetSceneDirectory() const {
    return m_rootDirectory / m_config.Directories.Scenes;
}

std::filesystem::path Project::GetScriptDirectory() const {
    return m_rootDirectory / m_config.Directories.Scripts;
}

std::filesystem::path Project::GetConfigDirectory() const {
    return m_rootDirectory / m_config.Directories.Config;
}

std::filesystem::path Project::GetAssetRegistryPath() const {
    return GetAssetDirectory() / "Registry" / "AssetRegistry.yaml";
}

std::optional<std::filesystem::path> Project::ResolvePath(std::string_view projectRelativePath) const {
    const std::optional<std::string> normalized = NormalizeProjectRelativePath(projectRelativePath);
    if (!normalized.has_value()) {
        return std::nullopt;
    }
    const std::filesystem::path absolute = normalized->empty() ? m_rootDirectory : m_rootDirectory / *normalized;
    // Also catches symbolic links inside the project that point outside of it.
    if (!IsAbsolutePathWithinRoot(absolute, m_rootDirectory)) {
        return std::nullopt;
    }
    return absolute;
}

std::optional<std::string> Project::MakeRelative(const std::filesystem::path& absolutePath) const {
    if (!Contains(absolutePath)) {
        return std::nullopt;
    }
    std::error_code errorCode;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolutePath, errorCode);
    if (errorCode) {
        return std::nullopt;
    }
    const std::filesystem::path relative = canonical.lexically_relative(m_rootDirectory);
    const std::string generic = relative.generic_string();
    return NormalizeProjectRelativePath(generic == "." ? std::string{} : generic);
}

bool Project::Contains(const std::filesystem::path& absolutePath) const {
    return IsAbsolutePathWithinRoot(absolutePath, m_rootDirectory);
}

std::optional<std::filesystem::path> Project::GetStartupScenePath() const {
    if (m_config.StartupScene.empty()) {
        return std::nullopt;
    }
    return ResolvePath(m_config.StartupScene);
}

bool Project::HasStartupScene() const {
    const std::optional<std::filesystem::path> path = GetStartupScenePath();
    std::error_code errorCode;
    return path.has_value() && std::filesystem::is_regular_file(*path, errorCode);
}

std::vector<std::string> Project::ListScenes() const {
    std::vector<std::string> scenes;
    std::error_code errorCode;
    std::filesystem::recursive_directory_iterator iterator(
        m_rootDirectory, std::filesystem::directory_options::skip_permission_denied, errorCode);
    for (const std::filesystem::recursive_directory_iterator end; !errorCode && iterator != end;
         iterator.increment(errorCode)) {
        const std::string name = iterator->path().filename().string();
        std::error_code statError;
        if (iterator->is_directory(statError)) {
            if (name.empty() || name.front() == '.' || !Contains(iterator->path())) {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        if (!iterator->is_regular_file(statError) || !IsSupportedSceneExtension(iterator->path())) {
            continue;
        }
        if (const std::optional<std::string> relative = MakeRelative(iterator->path())) {
            scenes.push_back(*relative);
        }
    }
    std::sort(scenes.begin(), scenes.end());
    return scenes;
}

std::vector<std::string> Project::EnsureStandardDirectories() {
    std::vector<std::string> created;
    const ProjectDirectories& directories = m_config.Directories;
    for (const std::string& relative :
         {directories.Assets, directories.Assets + "/Registry", directories.Scenes, directories.Scripts,
          directories.Config}) {
        const std::optional<std::filesystem::path> absolute = ResolvePath(relative);
        if (!absolute.has_value() || IsDirectory(*absolute)) {
            continue;
        }
        std::error_code errorCode;
        if (std::filesystem::create_directories(*absolute, errorCode) && !errorCode) {
            created.push_back(relative);
        }
    }
    return created;
}

ProjectFileResult Project::UpdateConfig(const ProjectConfig& config) {
    ProjectFileResult result = ProjectSerializer::SaveFile(config, m_projectFile);
    if (result.Success) {
        m_config = result.Config;
    }
    return result;
}

} // namespace Xora2D
