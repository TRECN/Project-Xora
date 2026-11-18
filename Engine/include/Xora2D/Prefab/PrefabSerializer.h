#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Xora2D {

class AssetManager;
class Prefab;

struct PrefabLoadResult {
    bool Success = false;
    std::string Message;
    std::shared_ptr<Prefab> Loaded;
    // Referenced texture / audio / script / animation assets that are not registered. Non-fatal: the references are
    // kept so instances work again once the assets come back.
    std::vector<std::string> MissingAssets;
};

struct PrefabSaveResult {
    bool Success = false;
    std::string Message;
};

// Reads and writes prefab assets. The entity entries use exactly the scene format (shared serializer code):
//
//   Prefab:
//     FormatVersion: 1
//     Name: Crate
//     Revision: 3
//   Entities:
//     - Entity: <prefab-local uuid>
//       TagComponent: {Tag: Crate}
//       TransformComponent: {...}
//       SpriteRendererComponent: {...}
//       ...
class PrefabSerializer {
public:
    static PrefabLoadResult Parse(const std::string& yamlText, const std::string& sourceName,
                                  AssetManager* assets = nullptr);
    static PrefabLoadResult Load(const std::filesystem::path& file, AssetManager* assets = nullptr);

    [[nodiscard]] static std::string Emit(const Prefab& prefab);
    // Emits, re-parses the result to validate it, then writes through a temporary file + rename, so a failed save
    // never damages an existing prefab. Refuses to replace an existing file unless `allowOverwrite`.
    static PrefabSaveResult Save(const Prefab& prefab, const std::filesystem::path& file, bool allowOverwrite);
};

} // namespace Xora2D
