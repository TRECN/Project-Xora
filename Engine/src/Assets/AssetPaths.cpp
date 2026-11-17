#include "Xora2D/Assets/AssetPaths.h"

#include "Xora2D/Core/Paths.h"

#include <algorithm>
#include <cctype>

namespace Xora2D {

std::filesystem::path GetAssetRoot() {
    return GetResourceRoot() / "Assets";
}

std::filesystem::path DefaultRegistryFilePath() {
    return GetAssetRoot() / "Registry" / "AssetRegistry.yaml";
}

std::filesystem::path ResolveAssetPath(const std::string& relativePath) {
    const std::optional<std::string> normalized = NormalizeAssetRelativePath(relativePath);
    if (!normalized.has_value()) {
        return {};
    }
    return GetAssetRoot() / *normalized;
}

std::optional<std::string> NormalizeAssetRelativePath(const std::string& path) {
    if (path.empty()) {
        return std::nullopt;
    }
    if (path.find('\0') != std::string::npos) {
        return std::nullopt;
    }

    std::string normalized;
    normalized.reserve(path.size());
    for (char character : path) {
        if (character == '\\') {
            normalized.push_back('/');
        } else {
            normalized.push_back(character);
        }
    }

    if (!normalized.empty() && normalized.front() == '/') {
        return std::nullopt;
    }
    if (normalized.find("..") != std::string::npos) {
        return std::nullopt;
    }

    return normalized;
}

bool IsSupportedTextureExtension(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
    if (extension.empty()) {
        return false;
    }
    std::string lowered = extension;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered == ".png" || lowered == ".jpg" || lowered == ".jpeg" || lowered == ".bmp" || lowered == ".tga";
}

bool IsSupportedAudioExtension(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
    if (extension.empty()) {
        return false;
    }
    std::string lowered = extension;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered == ".wav";
}

} // namespace Xora2D