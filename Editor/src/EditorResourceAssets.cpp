#include "Xora2DEditor/EditorResourceAssets.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Core/Paths.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Xora2DEditor {

namespace {

static std::string ExtensionLower(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return ext;
}

} // namespace

void RegisterKnownResourceAssets(Xora2D::AssetManager& assetManager) {
    const std::filesystem::path assetsRoot = Xora2D::GetContentRoot() / "Assets";
    if (!std::filesystem::exists(assetsRoot)) {
        return;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(assetsRoot)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path& file = entry.path();
        const std::filesystem::path relative = std::filesystem::relative(file, assetsRoot);
        const std::string normalized = relative.generic_string();
        const std::string displayName = file.stem().string();
        const std::string ext = ExtensionLower(file);

        if (Xora2D::IsSupportedPrefabExtension(file)) {
            (void)assetManager.RegisterPrefabAtRelativePath(normalized, file.stem().stem().string());
        } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
            (void)assetManager.RegisterTextureAtRelativePath(normalized, displayName);
        } else if (ext == ".wav" || ext == ".ogg") {
            (void)assetManager.RegisterAudioAtRelativePath(normalized, displayName);
        } else if (ext == ".py") {
            (void)assetManager.RegisterPythonScriptAtRelativePath(normalized, displayName);
        } else if (ext == ".anim.yaml" || ext == ".yaml") {
            if (relative.parent_path().filename() == "Clips" || file.filename().string().find("Clip") != std::string::npos) {
                (void)assetManager.RegisterAnimationClipAtRelativePath(normalized, displayName);
            }
        }
    }
}

} // namespace Xora2DEditor