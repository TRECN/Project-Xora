#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Xora2D {

class Scene;
class AssetManager;

struct SceneSerializeResult {
    bool Success = false;
    std::string Message;
    // Non-fatal problems found while loading (e.g. a prefab asset referenced by instances is missing).
    std::vector<std::string> Warnings;
};

class SceneSerializer {
public:
    static constexpr int kFormatVersion = 2;
    static constexpr int kLegacyFormatVersion = 1;

    static SceneSerializeResult Serialize(const Scene& scene, const std::filesystem::path& filepath);
    static SceneSerializeResult Deserialize(Scene& scene, const std::filesystem::path& filepath,
                                            AssetManager* assetManager = nullptr);
};

} // namespace Xora2D