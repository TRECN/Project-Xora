#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Xora2DEditor {

struct EditorWindowSettings {
    int Width = 1600;
    int Height = 900;
    bool Maximized = false;
};

struct EditorConfigData {
    EditorWindowSettings Window{};
    std::vector<std::string> RecentProjects;
};

struct EditorConfigResult {
    bool Success = false;
    bool UsedDefaults = false;
    std::string Message;
};

class EditorConfig {
public:
  EditorConfig() = default;

    [[nodiscard]] const EditorConfigData& GetData() const { return m_data; }
    EditorConfigData& GetData() { return m_data; }

    EditorConfigResult Load(const std::filesystem::path& path);
    EditorConfigResult Save(const std::filesystem::path& path) const;

    static EditorConfigResult LoadOrDefaults(const std::filesystem::path& path, EditorConfig& outConfig);

private:
    EditorConfigData m_data;
};

} // namespace Xora2DEditor