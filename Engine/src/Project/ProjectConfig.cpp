#include "Xora2D/Project/ProjectConfig.h"

#include <cmath>
#include <sstream>

namespace Xora2D {

const char* ToString(ProjectError error) {
    switch (error) {
    case ProjectError::None:
        return "None";
    case ProjectError::FileNotFound:
        return "FileNotFound";
    case ProjectError::NotAProjectFile:
        return "NotAProjectFile";
    case ProjectError::InvalidYaml:
        return "InvalidYaml";
    case ProjectError::UnsupportedVersion:
        return "UnsupportedVersion";
    case ProjectError::MissingField:
        return "MissingField";
    case ProjectError::InvalidValue:
        return "InvalidValue";
    case ProjectError::MissingDirectory:
        return "MissingDirectory";
    case ProjectError::InvalidName:
        return "InvalidName";
    case ProjectError::InvalidLocation:
        return "InvalidLocation";
    case ProjectError::AlreadyExists:
        return "AlreadyExists";
    case ProjectError::IoError:
        return "IoError";
    }
    return "Unknown";
}

bool IsValidProjectName(std::string_view name, std::string* error) {
    const auto fail = [error](const char* message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (name.empty()) {
        return fail("Project name is empty");
    }
    if (name.size() > kMaxProjectNameLength) {
        return fail("Project name is longer than 64 characters");
    }
    if (name.front() == '.' || name.front() == '-' || name.front() == ' ') {
        return fail("Project name cannot start with '.', '-' or a space");
    }
    if (name.back() == ' ' || name.back() == '.') {
        return fail("Project name cannot end with a space or '.'");
    }
    for (const char character : name) {
        const auto value = static_cast<unsigned char>(character);
        const bool allowed = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                             (value >= '0' && value <= '9') || character == ' ' || character == '-' ||
                             character == '_' || character == '.';
        if (!allowed) {
            return fail("Project name may only contain letters, digits, spaces, '-', '_' and '.'");
        }
    }
    if (name.find("..") != std::string_view::npos) {
        return fail("Project name cannot contain '..'");
    }
    return true;
}

std::optional<std::string> NormalizeProjectRelativePath(std::string_view path) {
    std::string result;
    if (path.empty()) {
        return result;
    }
    if (path.front() == '/' || path.front() == '\\' || path.find('\0') != std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        const std::string_view segment = path.substr(start, end - start);
        if (!segment.empty() && segment != ".") {
            if (segment == "..") {
                return std::nullopt;
            }
            for (const char character : segment) {
                if (static_cast<unsigned char>(character) < 0x20 || character == ':') {
                    return std::nullopt;
                }
            }
            if (!result.empty()) {
                result.push_back('/');
            }
            result.append(segment);
        }
        start = end + 1;
    }
    return result;
}

namespace {

void ValidateDirectory(const std::string& value, const char* label, std::vector<std::string>& errors) {
    const std::optional<std::string> normalized = NormalizeProjectRelativePath(value);
    if (!normalized.has_value() || normalized->empty() || *normalized != value) {
        errors.push_back(std::string(label) + " directory '" + value +
                         "' must be a non-empty project-relative path (forward slashes, no '..')");
    }
}

} // namespace

std::vector<std::string> ValidateProjectConfig(const ProjectConfig& config) {
    std::vector<std::string> errors;
    std::string nameError;
    if (!IsValidProjectName(config.Name, &nameError)) {
        errors.push_back(nameError);
    }
    if (config.FormatVersion < 1 || config.FormatVersion > kProjectFormatVersion) {
        std::ostringstream stream;
        stream << "Unsupported project format version " << config.FormatVersion << " (supported: 1-"
               << kProjectFormatVersion << ")";
        errors.push_back(stream.str());
    }

    const ProjectDirectories& directories = config.Directories;
    ValidateDirectory(directories.Assets, "Asset", errors);
    ValidateDirectory(directories.Scenes, "Scene", errors);
    ValidateDirectory(directories.Scripts, "Script", errors);
    ValidateDirectory(directories.Config, "Config", errors);
    if (directories.Assets != kProjectAssetDirectory) {
        errors.push_back("Asset directory must be 'Assets' in project format version 1");
    }
    const std::string assetPrefix = std::string(kProjectAssetDirectory) + "/";
    if (directories.Scripts != kProjectAssetDirectory && !directories.Scripts.starts_with(assetPrefix)) {
        errors.push_back("Script directory must be inside the asset directory (scripts are registered assets)");
    }

    if (!config.StartupScene.empty()) {
        const std::optional<std::string> scene = NormalizeProjectRelativePath(config.StartupScene);
        if (!scene.has_value() || *scene != config.StartupScene) {
            errors.push_back("Startup scene '" + config.StartupScene +
                             "' must be a project-relative path (forward slashes, no '..')");
        } else if (!scene->ends_with(".scene.yaml")) {
            errors.push_back("Startup scene must be a .scene.yaml file");
        }
    }

    const ProjectSettings& settings = config.Settings;
    if (settings.ProjectVersion.empty() || settings.ProjectVersion.size() > 32) {
        errors.push_back("Project version must be 1-32 characters");
    } else {
        for (const char character : settings.ProjectVersion) {
            if (static_cast<unsigned char>(character) < 0x20) {
                errors.push_back("Project version contains control characters");
                break;
            }
        }
    }
    if (settings.WindowWidth < kMinProjectWindowWidth || settings.WindowWidth > kMaxProjectWindowSize ||
        settings.WindowHeight < kMinProjectWindowHeight || settings.WindowHeight > kMaxProjectWindowSize) {
        std::ostringstream stream;
        stream << "Window size " << settings.WindowWidth << "x" << settings.WindowHeight << " is out of range ("
               << kMinProjectWindowWidth << "x" << kMinProjectWindowHeight << " to " << kMaxProjectWindowSize << "x"
               << kMaxProjectWindowSize << ")";
        errors.push_back(stream.str());
    }
    if (!std::isfinite(settings.Gravity.x) || !std::isfinite(settings.Gravity.y) ||
        std::fabs(settings.Gravity.x) > kMaxProjectGravity || std::fabs(settings.Gravity.y) > kMaxProjectGravity) {
        errors.push_back("Gravity components must be finite and within +/-1000 m/s^2");
    }
    return errors;
}

} // namespace Xora2D
