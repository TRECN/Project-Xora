#include "Xora2D/Assets/AssetPaths.h"

#include "Xora2D/Core/Paths.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace Xora2D {

std::filesystem::path GetAssetRoot() {
    return GetContentRoot() / "Assets";
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
    return lowered == ".wav" || lowered == ".mp3";
}

bool IsSupportedPythonScriptExtension(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
    if (extension.empty()) {
        return false;
    }
    std::string lowered = extension;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered == ".py";
}

bool IsSupportedSceneExtension(const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    std::string lowered = filename;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered.ends_with(".scene.yaml");
}

bool IsAbsolutePathWithinRoot(const std::filesystem::path& absolute, const std::filesystem::path& root) {
    if (absolute.empty() || root.empty()) {
        return false;
    }
    std::error_code errorCode;
    const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, errorCode);
    if (errorCode) {
        return false;
    }
    const std::filesystem::path canonicalAbsolute = std::filesystem::weakly_canonical(absolute, errorCode);
    if (errorCode) {
        return false;
    }
    auto [rootEnd, absEnd] = std::mismatch(canonicalRoot.begin(), canonicalRoot.end(), canonicalAbsolute.begin(),
                                           canonicalAbsolute.end());
    return rootEnd == canonicalRoot.end();
}

bool IsSupportedPrefabExtension(const std::filesystem::path& path) {
    std::string lowered = path.filename().string();
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered.ends_with(".prefab.yaml") && lowered.size() > std::string_view(".prefab.yaml").size();
}

namespace {
bool HasCompoundExtension(const std::filesystem::path& path, std::string_view suffix) {
    std::string lowered = path.filename().string();
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered.size() > suffix.size() && lowered.ends_with(suffix);
}
} // namespace

bool IsSupportedTilesetExtension(const std::filesystem::path& path) {
    return HasCompoundExtension(path, ".tileset.yaml");
}

bool IsSupportedTilemapExtension(const std::filesystem::path& path) {
    return HasCompoundExtension(path, ".tilemap.yaml");
}

bool IsSupportedParticleEffectExtension(const std::filesystem::path& path) {
    return HasCompoundExtension(path, ".particle.yaml");
}

bool IsSupportedAnimationClipExtension(const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    std::string lowered = filename;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered.ends_with(".anim.yaml");
}

} // namespace Xora2D