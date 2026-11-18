#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Xora2D {

// <content root>/Assets — asset root of the open project (or Resources/Assets without a project).
std::filesystem::path GetAssetRoot();
std::filesystem::path DefaultRegistryFilePath();

// Resolves a project-relative asset path under Resources/Assets/.
std::filesystem::path ResolveAssetPath(const std::string& relativePath);

// Normalizes to forward-slash form without .. or absolute segments; fails on traversal.
std::optional<std::string> NormalizeAssetRelativePath(const std::string& path);

bool IsSupportedTextureExtension(const std::filesystem::path& path);
bool IsSupportedAudioExtension(const std::filesystem::path& path);
bool IsSupportedPythonScriptExtension(const std::filesystem::path& path);
bool IsSupportedAnimationClipExtension(const std::filesystem::path& path);
bool IsSupportedSceneExtension(const std::filesystem::path& path);

// Rejects paths outside `root` (no .. escape).
[[nodiscard]] bool IsAbsolutePathWithinRoot(const std::filesystem::path& absolute,
                                            const std::filesystem::path& root);

} // namespace Xora2D