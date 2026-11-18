#pragma once

#include <filesystem>
#include <string>

namespace Xora2D {

// Engine resources (shaders, built-in samples). Located next to the executable / in the source tree, or overridden
// with the XORA2D_RESOURCES environment variable.
std::filesystem::path GetResourceRoot();
std::filesystem::path ResolveResourcePath(const std::string& relativePath);

// Content root: the directory game content (Assets/, Scenes/, ...) is resolved against. While a project is open this
// is the project root directory; otherwise it falls back to GetResourceRoot() so the Sandbox and legacy workflows keep
// working unchanged. Engine shaders always come from GetResourceRoot(), never from a project.
std::filesystem::path GetContentRoot();
std::filesystem::path ResolveContentPath(const std::string& relativePath);
// Points content resolution at a project root (absolute). Thread-safe; callers must stop background workers that
// resolve content paths before switching projects.
void SetContentRoot(const std::filesystem::path& root);
// Returns content resolution to GetResourceRoot().
void ResetContentRoot();
[[nodiscard]] bool HasContentRootOverride();

} // namespace Xora2D
