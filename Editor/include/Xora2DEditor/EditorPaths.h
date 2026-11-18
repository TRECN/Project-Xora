#pragma once

#include <filesystem>

namespace Xora2DEditor {

std::filesystem::path GetEditorConfigDirectory();
std::filesystem::path GetEditorConfigFilePath();
std::filesystem::path GetImGuiLayoutPath();
// Application-level recent project history (never stored inside a project).
std::filesystem::path GetRecentProjectsFilePath();

// True when imgui.ini contains a serialized dock node tree (not just an empty dockspace stub).
bool IsImGuiDockLayoutComplete(const std::filesystem::path& iniPath);

} // namespace Xora2DEditor