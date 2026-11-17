#pragma once

#include <filesystem>
#include <string>

namespace Xora2D {

class Scene;
class AssetManager;

struct SceneSerializeResult {
    bool Success = false;
    std::string Message;
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