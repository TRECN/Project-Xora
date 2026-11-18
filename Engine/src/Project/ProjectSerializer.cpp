#include "Xora2D/Project/ProjectSerializer.h"

#include "Xora2D/Core/Version.h"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

namespace Xora2D {

namespace {

constexpr const char* kRootKey = "XoraProject";

ProjectFileResult Fail(ProjectError error, std::string message) {
    ProjectFileResult result{};
    result.Error = error;
    result.Message = std::move(message);
    return result;
}

template <typename T>
bool ReadOptional(const YAML::Node& parent, const char* key, T& value) {
    if (const YAML::Node node = parent[key]; node && !node.IsNull()) {
        value = node.as<T>();
        return true;
    }
    return false;
}

std::string JoinErrors(const std::vector<std::string>& errors) {
    std::string joined;
    for (const std::string& error : errors) {
        if (!joined.empty()) {
            joined += "; ";
        }
        joined += error;
    }
    return joined;
}

} // namespace

ProjectFileResult ProjectSerializer::Parse(const std::string& yamlText, const std::string& sourceName) {
    YAML::Node root;
    try {
        root = YAML::Load(yamlText);
    } catch (const YAML::Exception& exception) {
        return Fail(ProjectError::InvalidYaml, sourceName + " is not valid YAML: " + exception.what());
    }
    if (!root.IsMap() || !root[kRootKey] || !root[kRootKey].IsMap()) {
        return Fail(ProjectError::NotAProjectFile, sourceName + " is not a Xora2D project (missing 'XoraProject')");
    }
    const YAML::Node project = root[kRootKey];

    ProjectConfig config{};
    try {
        if (!project["FormatVersion"]) {
            return Fail(ProjectError::MissingField, sourceName + ": missing required field 'FormatVersion'");
        }
        config.FormatVersion = project["FormatVersion"].as<int>();
        if (config.FormatVersion > kProjectFormatVersion) {
            std::ostringstream stream;
            stream << sourceName << " uses project format version " << config.FormatVersion
                   << ", which is newer than this engine supports (" << kProjectFormatVersion
                   << "). Open it with a newer Xora2D.";
            return Fail(ProjectError::UnsupportedVersion, stream.str());
        }
        if (config.FormatVersion < 1) {
            return Fail(ProjectError::UnsupportedVersion,
                        sourceName + ": invalid project format version " + std::to_string(config.FormatVersion));
        }
        if (!project["Name"] || project["Name"].IsNull()) {
            return Fail(ProjectError::MissingField, sourceName + ": missing required field 'Name'");
        }
        config.Name = project["Name"].as<std::string>();
        ReadOptional(project, "EngineVersion", config.EngineVersion);
        ReadOptional(project, "StartupScene", config.StartupScene);

        if (const YAML::Node directories = project["Directories"]; directories && directories.IsMap()) {
            ReadOptional(directories, "Assets", config.Directories.Assets);
            ReadOptional(directories, "Scenes", config.Directories.Scenes);
            ReadOptional(directories, "Scripts", config.Directories.Scripts);
            ReadOptional(directories, "Config", config.Directories.Config);
        }

        if (const YAML::Node settings = project["Settings"]; settings && settings.IsMap()) {
            ProjectSettings& out = config.Settings;
            ReadOptional(settings, "ProjectVersion", out.ProjectVersion);
            if (const YAML::Node window = settings["Window"]; window && window.IsMap()) {
                ReadOptional(window, "Width", out.WindowWidth);
                ReadOptional(window, "Height", out.WindowHeight);
                ReadOptional(window, "VSync", out.VSync);
            }
            if (const YAML::Node physics = settings["Physics"]; physics && physics.IsMap()) {
                if (const YAML::Node gravity = physics["Gravity"]; gravity) {
                    if (!gravity.IsSequence() || gravity.size() != 2) {
                        return Fail(ProjectError::InvalidValue, sourceName + ": Settings.Physics.Gravity must be [x, y]");
                    }
                    out.Gravity = {gravity[0].as<float>(), gravity[1].as<float>()};
                }
            }
            if (const YAML::Node scripting = settings["Scripting"]; scripting && scripting.IsMap()) {
                ReadOptional(scripting, "Enabled", out.ScriptingEnabled);
            }
        }
    } catch (const YAML::Exception& exception) {
        return Fail(ProjectError::InvalidValue, sourceName + ": invalid value (" + exception.what() + ")");
    }

    std::string nameError;
    if (!IsValidProjectName(config.Name, &nameError)) {
        return Fail(ProjectError::InvalidName, sourceName + ": " + nameError);
    }
    if (const std::vector<std::string> errors = ValidateProjectConfig(config); !errors.empty()) {
        return Fail(ProjectError::InvalidValue, sourceName + ": " + JoinErrors(errors));
    }

    ProjectFileResult result{};
    result.Success = true;
    result.Config = std::move(config);
    result.Message = "Loaded project '" + result.Config.Name + "'";
    return result;
}

ProjectFileResult ProjectSerializer::LoadFile(const std::filesystem::path& projectFile) {
    std::error_code errorCode;
    if (!std::filesystem::exists(projectFile, errorCode)) {
        return Fail(ProjectError::FileNotFound, "Project file not found: " + projectFile.string());
    }
    if (!std::filesystem::is_regular_file(projectFile, errorCode)) {
        return Fail(ProjectError::NotAProjectFile, "Not a project file: " + projectFile.string());
    }
    std::ifstream input(projectFile, std::ios::binary);
    if (!input) {
        return Fail(ProjectError::IoError, "Cannot read project file: " + projectFile.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return Parse(buffer.str(), projectFile.filename().string());
}

std::string ProjectSerializer::Emit(const ProjectConfig& config) {
    YAML::Emitter out;
    // Hand-editable file: "-9.81", not "-9.81000042".
    out.SetFloatPrecision(6);
    out << YAML::BeginMap;
    out << YAML::Key << kRootKey << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "FormatVersion" << YAML::Value << config.FormatVersion;
    out << YAML::Key << "EngineVersion" << YAML::Value << config.EngineVersion;
    out << YAML::Key << "Name" << YAML::Value << config.Name;
    out << YAML::Key << "StartupScene" << YAML::Value << config.StartupScene;

    out << YAML::Key << "Directories" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "Assets" << YAML::Value << config.Directories.Assets;
    out << YAML::Key << "Scenes" << YAML::Value << config.Directories.Scenes;
    out << YAML::Key << "Scripts" << YAML::Value << config.Directories.Scripts;
    out << YAML::Key << "Config" << YAML::Value << config.Directories.Config;
    out << YAML::EndMap;

    const ProjectSettings& settings = config.Settings;
    out << YAML::Key << "Settings" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "ProjectVersion" << YAML::Value << settings.ProjectVersion;
    out << YAML::Key << "Window" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "Width" << YAML::Value << settings.WindowWidth;
    out << YAML::Key << "Height" << YAML::Value << settings.WindowHeight;
    out << YAML::Key << "VSync" << YAML::Value << settings.VSync;
    out << YAML::EndMap;
    out << YAML::Key << "Physics" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "Gravity" << YAML::Value << YAML::Flow << YAML::BeginSeq << settings.Gravity.x
        << settings.Gravity.y << YAML::EndSeq;
    out << YAML::EndMap;
    out << YAML::Key << "Scripting" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "Enabled" << YAML::Value << settings.ScriptingEnabled;
    out << YAML::EndMap;
    out << YAML::EndMap;

    out << YAML::EndMap;
    out << YAML::EndMap;
    return std::string(out.c_str()) + "\n";
}

ProjectFileResult ProjectSerializer::SaveFile(const ProjectConfig& config, const std::filesystem::path& projectFile) {
    if (const std::vector<std::string> errors = ValidateProjectConfig(config); !errors.empty()) {
        return Fail(ProjectError::InvalidValue, JoinErrors(errors));
    }
    ProjectConfig toWrite = config;
    toWrite.FormatVersion = kProjectFormatVersion;
    toWrite.EngineVersion = GetEngineVersionString();

    std::random_device device;
    const std::filesystem::path temporary =
        projectFile.parent_path() / (".xora_save_" + std::to_string(device()) + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return Fail(ProjectError::IoError, "Cannot write project file in " + projectFile.parent_path().string());
        }
        output << Emit(toWrite);
        output.flush();
        if (!output) {
            output.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return Fail(ProjectError::IoError, "Failed while writing project file " + projectFile.string());
        }
    }
    std::error_code errorCode;
    std::filesystem::rename(temporary, projectFile, errorCode);
    if (errorCode) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Fail(ProjectError::IoError, "Cannot replace project file " + projectFile.string() + ": " +
                                               errorCode.message());
    }
    ProjectFileResult result{};
    result.Success = true;
    result.Config = std::move(toWrite);
    result.Message = "Saved project file " + projectFile.filename().string();
    return result;
}

} // namespace Xora2D
