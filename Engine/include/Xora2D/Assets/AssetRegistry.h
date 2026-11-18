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

    // Updates the relative path index while preserving the stable handle (rename/move).
    bool RelocateAssetPath(AssetHandle handle, const std::string& newNormalizedRelativePath);
    bool SetAssetName(AssetHandle handle, const std::string& name);
    void SetAssetAvailable(AssetHandle handle, bool available);

    // Returns the stable path-derived handle for a new asset. When that handle is already owned by another
    // asset (e.g. one that was renamed away from this path and kept its handle), a deterministic alternate
    // derived from the same path is returned instead so references to the existing asset are never stolen.
    [[nodiscard]] AssetHandle AllocateHandleForPath(const std::string& normalizedRelativePath) const;

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