#include "Xora2DEditor/EditorPaths.h"

#include <cstdlib>
#include <fstream>
#include <string>

namespace Xora2DEditor {

namespace {

std::filesystem::path HomeDirectory() {
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home);
    }
    return std::filesystem::temp_directory_path();
}

} // namespace

std::filesystem::path GetEditorConfigDirectory() {
#if defined(__APPLE__)
    return HomeDirectory() / "Library" / "Application Support" / "Xora2D";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
        return std::filesystem::path(xdg) / "Xora2D";
    }
    return HomeDirectory() / ".config" / "Xora2D";
#endif
}

std::filesystem::path GetEditorConfigFilePath() {
    return GetEditorConfigDirectory() / "editor.yaml";
}

std::filesystem::path GetImGuiLayoutPath() {
    return GetEditorConfigDirectory() / "imgui.ini";
}

std::filesystem::path GetRecentProjectsFilePath() {
    return GetEditorConfigDirectory() / "recent_projects.yaml";
}

bool IsImGuiDockLayoutComplete(const std::filesystem::path& iniPath) {
    if (!std::filesystem::exists(iniPath)) {
        return false;
    }

    std::ifstream input(iniPath);
    if (!input) {
        return false;
    }

    bool inDockingSection = false;
    bool hasSplitOrChildNode = false;
    std::string line;
    while (std::getline(input, line)) {
        if (line == "[Docking][Data]") {
            inDockingSection = true;
            continue;
        }
        if (inDockingSection) {
            if (!line.empty() && line.front() == '[') {
                break;
            }
            if (line.find("Split=") != std::string::npos) {
                hasSplitOrChildNode = true;
                break;
            }
            if (line.find("DockNode") != std::string::npos && line.find(" ID=") != std::string::npos) {
                hasSplitOrChildNode = true;
                break;
            }
        }
    }

    return hasSplitOrChildNode;
}

} // namespace Xora2DEditor