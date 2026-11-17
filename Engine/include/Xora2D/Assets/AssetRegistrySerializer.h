#pragma once

#include "Xora2D/Assets/AssetRegistry.h"

#include <filesystem>
#include <string>

namespace Xora2D {

struct AssetRegistrySerializeResult {
    bool Success = false;
    std::string Message;
};

class AssetRegistrySerializer {
public:
    static constexpr int kFormatVersion = 1;

    static AssetRegistrySerializeResult Load(AssetRegistry& registry, const std::filesystem::path& filepath);
    static AssetRegistrySerializeResult Save(const AssetRegistry& registry, const std::filesystem::path& filepath);
};

} // namespace Xora2D