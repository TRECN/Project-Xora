#pragma once

#include <filesystem>
#include <string>

namespace Xora2D {

// Locates the Resources/ tree when running from the build directory or
// source tree.
std::filesystem::path GetResourceRoot();
std::filesystem::path ResolveResourcePath(const std::string& relativePath);

} // namespace Xora2D