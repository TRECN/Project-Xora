#include "Xora2D/Assets/AssetRegistry.h"

#include "Xora2D/Assets/AssetPaths.h"

namespace Xora2D {

bool AssetRegistry::ValidateMetadata(const AssetMetadata& metadata, std::string& error) const {
    if (!metadata.Handle.IsValid()) {
        error = "Invalid asset handle";
        return false;
    }
    if (metadata.Type == AssetType::Unknown) {
        error = "Unknown asset type";
        return false;
    }
    if (metadata.Name.empty()) {
        error = "Asset name is required";
        return false;
    }
    if (NormalizeAssetRelativePath(metadata.RelativePath).value_or("") != metadata.RelativePath) {
        error = "Invalid or unsafe asset relative path";
        return false;
    }
    return true;
}

bool AssetRegistry::RegisterAsset(AssetMetadata metadata) {
    std::string error;
    if (!ValidateMetadata(metadata, error)) {
        return false;
    }

    if (m_byHandle.contains(metadata.Handle)) {
        return false;
    }

    const auto pathFound = m_byPath.find(metadata.RelativePath);
    if (pathFound != m_byPath.end() && pathFound->second != metadata.Handle) {
        return false;
    }

    const AssetHandle handle = metadata.Handle;
    const std::string relativePath = metadata.RelativePath;
    m_byHandle.emplace(handle, std::move(metadata));
    m_byPath.emplace(relativePath, handle);
    return true;
}

AssetHandle AssetRegistry::AllocateHandleForPath(const std::string& normalizedRelativePath) const {
    AssetHandle candidate = AssetHandle::StableFromRelativePath(normalizedRelativePath);
    for (int attempt = 1; attempt < 1024 && (!candidate.IsValid() || m_byHandle.contains(candidate)); ++attempt) {
        candidate = AssetHandle::StableFromRelativePath(normalizedRelativePath + "#" + std::to_string(attempt));
    }
    return candidate;
}

bool AssetRegistry::RelocateAssetPath(AssetHandle handle, const std::string& newNormalizedRelativePath) {
    const auto found = m_byHandle.find(handle);
    if (found == m_byHandle.end()) {
        return false;
    }
    const std::optional<std::string> normalized = NormalizeAssetRelativePath(newNormalizedRelativePath);
    if (!normalized.has_value()) {
        return false;
    }
    const auto pathConflict = m_byPath.find(*normalized);
    if (pathConflict != m_byPath.end() && pathConflict->second != handle) {
        return false;
    }
    m_byPath.erase(found->second.RelativePath);
    found->second.RelativePath = *normalized;
    m_byPath[*normalized] = handle;
    return true;
}

bool AssetRegistry::SetAssetName(AssetHandle handle, const std::string& name) {
    if (name.empty()) {
        return false;
    }
    const auto found = m_byHandle.find(handle);
    if (found == m_byHandle.end()) {
        return false;
    }
    found->second.Name = name;
    return true;
}

void AssetRegistry::SetAssetAvailable(AssetHandle handle, bool available) {
    const auto found = m_byHandle.find(handle);
    if (found == m_byHandle.end()) {
        return;
    }
    found->second.Available = available;
}

bool AssetRegistry::RemoveAsset(AssetHandle handle) {
    const auto found = m_byHandle.find(handle);
    if (found == m_byHandle.end()) {
        return false;
    }
    m_byPath.erase(found->second.RelativePath);
    m_byHandle.erase(found);
    return true;
}

bool AssetRegistry::Contains(AssetHandle handle) const {
    return m_byHandle.contains(handle);
}

bool AssetRegistry::ContainsPath(const std::string& normalizedRelativePath) const {
    return m_byPath.contains(normalizedRelativePath);
}

const AssetMetadata* AssetRegistry::FindByHandle(AssetHandle handle) const {
    const auto found = m_byHandle.find(handle);
    if (found == m_byHandle.end()) {
        return nullptr;
    }
    return &found->second;
}

const AssetMetadata* AssetRegistry::FindByRelativePath(const std::string& normalizedRelativePath) const {
    const auto found = m_byPath.find(normalizedRelativePath);
    if (found == m_byPath.end()) {
        return nullptr;
    }
    return FindByHandle(found->second);
}

std::vector<AssetMetadata> AssetRegistry::ListAssets() const {
    std::vector<AssetMetadata> output;
    output.reserve(m_byHandle.size());
    for (const auto& entry : m_byHandle) {
        output.push_back(entry.second);
    }
    return output;
}

void AssetRegistry::Clear() {
    m_byHandle.clear();
    m_byPath.clear();
}

} // namespace Xora2D