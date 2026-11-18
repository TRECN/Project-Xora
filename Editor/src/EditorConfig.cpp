#include "Xora2DEditor/EditorConfig.h"

#include <fstream>
#include <yaml-cpp/yaml.h>

namespace Xora2DEditor {

EditorConfigResult EditorConfig::Load(const std::filesystem::path& path) {
    EditorConfigResult result{};
    if (!std::filesystem::exists(path)) {
        result.Success = false;
        result.UsedDefaults = true;
        result.Message = "Config file not found";
        return result;
    }

    try {
        const YAML::Node root = YAML::LoadFile(path.string());
        if (!root["editor"]) {
            result.Success = false;
            result.Message = "Missing editor root";
            return result;
        }
        const YAML::Node editor = root["editor"];
        if (editor["window"]) {
            const YAML::Node window = editor["window"];
            if (window["width"]) {
                m_data.Window.Width = window["width"].as<int>();
            }
            if (window["height"]) {
                m_data.Window.Height = window["height"].as<int>();
            }
            if (window["maximized"]) {
                m_data.Window.Maximized = window["maximized"].as<bool>();
            }
        }
        m_data.RecentProjects.clear();
        if (editor["recent_projects"]) {
            for (const auto& entry : editor["recent_projects"]) {
                m_data.RecentProjects.push_back(entry.as<std::string>());
            }
        }
        result.Success = true;
        result.Message = "Loaded editor configuration";
    } catch (const YAML::Exception& exception) {
        result.Success = false;
        result.Message = exception.what();
    }
    return result;
}

EditorConfigResult EditorConfig::Save(const std::filesystem::path& path) const {
    EditorConfigResult result{};
    try {
        std::filesystem::create_directories(path.parent_path());
        YAML::Emitter emitter;
        emitter << YAML::BeginMap;
        emitter << YAML::Key << "editor";
        emitter << YAML::Value << YAML::BeginMap;
        emitter << YAML::Key << "window";
        emitter << YAML::Value << YAML::BeginMap;
        emitter << YAML::Key << "width" << YAML::Value << m_data.Window.Width;
        emitter << YAML::Key << "height" << YAML::Value << m_data.Window.Height;
        emitter << YAML::Key << "maximized" << YAML::Value << m_data.Window.Maximized;
        emitter << YAML::EndMap;
        emitter << YAML::Key << "recent_projects";
        emitter << YAML::Value << YAML::BeginSeq;
        for (const std::string& project : m_data.RecentProjects) {
            emitter << project;
        }
        emitter << YAML::EndSeq;
        emitter << YAML::EndMap;
        emitter << YAML::EndMap;

        std::ofstream output(path);
        if (!output) {
            result.Message = "Failed to open config file for writing";
            return result;
        }
        output << emitter.c_str();
        result.Success = true;
        result.Message = "Saved editor configuration";
    } catch (const YAML::Exception& exception) {
        result.Message = exception.what();
    }
    return result;
}

EditorConfigResult EditorConfig::LoadOrDefaults(const std::filesystem::path& path, EditorConfig& outConfig) {
    const EditorConfigResult loadResult = outConfig.Load(path);
    if (loadResult.Success) {
        return loadResult;
    }
    EditorConfigResult defaults{};
    defaults.Success = true;
    defaults.UsedDefaults = true;
    defaults.Message = loadResult.Message.empty() ? "Using defaults" : loadResult.Message;
    outConfig = EditorConfig{};
    return defaults;
}

} // namespace Xora2DEditor