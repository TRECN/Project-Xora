#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Assets/AssetResult.h"
#include "Xora2D/Assets/AssetTypes.h"

#include <filesystem>
#include <memory>
#include <unordered_map>

namespace Xora2D {

class AnimationClipAsset;
class AudioClip;
class PythonScriptAsset;
class Texture2D;

class AssetManager {
public:
    explicit AssetManager(AssetRegistry& registry);

    AssetRegistry& GetRegistry() { return m_registry; }
    const AssetRegistry& GetRegistry() const { return m_registry; }

    void SetGraphicsResourcesEnabled(bool enabled);
    [[nodiscard]] bool AreGraphicsResourcesEnabled() const { return m_graphicsResourcesEnabled; }

    AssetResult ImportTextureFile(const std::filesystem::path& sourceFile,
                                    const std::string& displayName = {});
    AssetResult RegisterTextureAtRelativePath(const std::string& normalizedRelativePath,
                                                const std::string& displayName = {});
    AssetResult EnsureLegacyTexturePath(const std::string& legacyResourceRelativePath);

    AssetResult RegisterAudioAtRelativePath(const std::string& normalizedRelativePath,
                                            const std::string& displayName = {});

    AssetResult RegisterPythonScriptAtRelativePath(const std::string& normalizedRelativePath,
                                                     const std::string& displayName = {});

    AssetResult RegisterAnimationClipAtRelativePath(const std::string& normalizedRelativePath,
                                                    const std::string& displayName = {});

    AssetResult LoadTexture(AssetHandle handle);
    AssetResult LoadAudio(AssetHandle handle);
    AssetResult LoadPythonScript(AssetHandle handle);
    AssetResult LoadAnimationClip(AssetHandle handle);
    std::shared_ptr<Texture2D> GetLoadedTexture(AssetHandle handle) const;
    std::shared_ptr<const AudioClip> GetLoadedAudio(AssetHandle handle) const;
    std::shared_ptr<const PythonScriptAsset> GetLoadedPythonScript(AssetHandle handle) const;
    std::shared_ptr<const AnimationClipAsset> GetLoadedAnimationClip(AssetHandle handle) const;
    [[nodiscard]] bool IsAssetLoaded(AssetHandle handle) const;

    void UnloadAsset(AssetHandle handle);
    AssetResult ReloadAsset(AssetHandle handle);
    void UnloadAll();

    void PrepareShutdown();

private:
    AssetResult LoadTextureInternal(AssetHandle handle, bool forceReload);
    AssetResult LoadAudioInternal(AssetHandle handle, bool forceReload);
    AssetResult LoadPythonScriptInternal(AssetHandle handle, bool forceReload);
    AssetResult LoadAnimationClipInternal(AssetHandle handle, bool forceReload);

    AssetRegistry& m_registry;
    std::unordered_map<AssetHandle, std::shared_ptr<Texture2D>> m_textureCache;
    std::unordered_map<AssetHandle, std::shared_ptr<const AudioClip>> m_audioCache;
    std::unordered_map<AssetHandle, std::shared_ptr<const PythonScriptAsset>> m_scriptCache;
    std::unordered_map<AssetHandle, std::shared_ptr<const AnimationClipAsset>> m_animationClipCache;
    bool m_graphicsResourcesEnabled = true;
};

} // namespace Xora2D