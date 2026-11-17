#pragma once

#include "Xora2D/Assets/AssetMetadata.h"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Xora2D {

class AssetRegistry {
public:
    bool RegisterAsset(AssetMetadata metadata);
    bool RemoveAsset(AssetHandle handle);

    [[nodiscard]] bool Contains(AssetHandle handle) const;
    [[nodiscard]] bool ContainsPath(const std::string& normalizedRelativePath) const;

    [[nodiscard]] const AssetMetadata* FindByHandle(AssetHandle handle) const;
    [[nodiscard]] const AssetMetadata* FindByRelativePath(const std::string& normalizedRelativePath) const;

    [[nodiscard]] std::vector<AssetMetadata> ListAssets() const;
    [[nodiscard]] std::size_t Count() const { return m_byHandle.size(); }

    void Clear();

    [[nodiscard]] bool ValidateMetadata(const AssetMetadata& metadata, std::string& error) const;

private:
    std::unordered_map<AssetHandle, AssetMetadata> m_byHandle;
    std::unordered_map<std::string, AssetHandle> m_byPath;
};

} // namespace Xora2D