#include "Xora2D/Assets/AssetManager.h"

#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Audio/AudioClip.h"
#include "Xora2D/Renderer/Texture2D.h"
#include "Xora2D/Scripting/PythonScriptAsset.h"

#include <filesystem>

namespace Xora2D {

AssetManager::AssetManager(AssetRegistry& registry)
    : m_registry(registry) {}

void AssetManager::SetGraphicsResourcesEnabled(bool enabled) {
    if (!enabled) {
        PrepareShutdown();
    }
    m_graphicsResourcesEnabled = enabled;
}

AssetResult AssetManager::RegisterTextureAtRelativePath(const std::string& normalizedRelativePath,
                                                          const std::string& displayName) {
    AssetResult result{};
    const std::optional<std::string> normalized = NormalizeAssetRelativePath(normalizedRelativePath);
    if (!normalized.has_value()) {
        result.Message = "Invalid relative asset path";
        return result;
    }

    if (const AssetMetadata* existing = m_registry.FindByRelativePath(*normalized)) {
        result.Success = true;
        result.Handle = existing->Handle;
        result.Message = "Asset already registered";
        return result;
    }

    const std::filesystem::path absolute = ResolveAssetPath(*normalized);
    if (!std::filesystem::exists(absolute)) {
        result.Message = "Asset file does not exist: " + absolute.string();
        return result;
    }

    AssetMetadata metadata{};
    metadata.Handle = AssetHandle::StableFromRelativePath(*normalized);
    metadata.Type = AssetType::Texture2D;
    metadata.Name = displayName.empty() ? absolute.stem().string() : displayName;
    metadata.RelativePath = *normalized;
    metadata.Available = true;

    if (m_registry.Contains(metadata.Handle)) {
        result.Message = "Duplicate asset handle for path";
        return result;
    }

    if (!m_registry.RegisterAsset(metadata)) {
        result.Message = "Failed to register asset metadata";
        return result;
    }

    result.Success = true;
    result.Handle = metadata.Handle;
    return result;
}

AssetResult AssetManager::ImportTextureFile(const std::filesystem::path& sourceFile,
                                              const std::string& displayName) {
    AssetResult result{};
    if (!std::filesystem::exists(sourceFile)) {
        result.Message = "Source texture file does not exist";
        return result;
    }
    if (!IsSupportedTextureExtension(sourceFile)) {
        result.Message = "Unsupported texture file format";
        return result;
    }

    const std::string filename = sourceFile.filename().string();
    const std::string relative = std::string("Textures/") + filename;
    const std::filesystem::path destination = ResolveAssetPath(relative);
    std::error_code errorCode;
    std::filesystem::create_directories(destination.parent_path(), errorCode);

    if (const AssetMetadata* existing = m_registry.FindByRelativePath(relative)) {
        result.Success = true;
        result.Handle = existing->Handle;
        result.Message = "Texture already imported";
        return result;
    }

    if (!std::filesystem::exists(destination)) {
        std::filesystem::copy_file(sourceFile, destination, std::filesystem::copy_options::skip_existing, errorCode);
        if (errorCode) {
            result.Message = "Failed to copy texture into Assets tree";
            return result;
        }
    }

    return RegisterTextureAtRelativePath(relative, displayName.empty() ? sourceFile.stem().string() : displayName);
}

AssetResult AssetManager::EnsureLegacyTexturePath(const std::string& legacyResourceRelativePath) {
    const std::filesystem::path legacyAbsolute = ResolveResourcePath(legacyResourceRelativePath);
    if (!std::filesystem::exists(legacyAbsolute)) {
        AssetResult result{};
        result.Message = "Legacy texture path not found";
        return result;
    }

    const std::string filename = legacyAbsolute.filename().string();
    const std::string assetRelative = std::string("Textures/") + filename;

    if (const AssetMetadata* existing = m_registry.FindByRelativePath(assetRelative)) {
        AssetResult result{};
        result.Success = true;
        result.Handle = existing->Handle;
        return result;
    }

    const std::filesystem::path destination = ResolveAssetPath(assetRelative);
    std::error_code errorCode;
    std::filesystem::create_directories(destination.parent_path(), errorCode);
    if (!std::filesystem::exists(destination)) {
        std::filesystem::copy_file(legacyAbsolute, destination, std::filesystem::copy_options::skip_existing, errorCode);
    }

    return RegisterTextureAtRelativePath(assetRelative, legacyAbsolute.stem().string());
}

AssetResult AssetManager::RegisterAudioAtRelativePath(const std::string& normalizedRelativePath,
                                                      const std::string& displayName) {
    AssetResult result{};
    const std::optional<std::string> normalized = NormalizeAssetRelativePath(normalizedRelativePath);
    if (!normalized.has_value()) {
        result.Message = "Invalid relative asset path";
        return result;
    }

    if (const AssetMetadata* existing = m_registry.FindByRelativePath(*normalized)) {
        result.Success = true;
        result.Handle = existing->Handle;
        result.Message = "Asset already registered";
        return result;
    }

    const std::filesystem::path absolute = ResolveAssetPath(*normalized);
    if (!std::filesystem::exists(absolute)) {
        result.Message = "Asset file does not exist: " + absolute.string();
        return result;
    }
    if (!IsSupportedAudioExtension(absolute)) {
        result.Message = "Unsupported audio file format";
        return result;
    }

    AssetMetadata metadata{};
    metadata.Handle = AssetHandle::StableFromRelativePath(*normalized);
    metadata.Type = AssetType::Audio;
    metadata.Name = displayName.empty() ? absolute.stem().string() : displayName;
    metadata.RelativePath = *normalized;
    metadata.Available = true;

    if (m_registry.Contains(metadata.Handle)) {
        result.Message = "Duplicate asset handle for path";
        return result;
    }
    if (!m_registry.RegisterAsset(metadata)) {
        result.Message = "Failed to register asset metadata";
        return result;
    }

    result.Success = true;
    result.Handle = metadata.Handle;
    return result;
}

AssetResult AssetManager::LoadTexture(AssetHandle handle) {
    return LoadTextureInternal(handle, false);
}

AssetResult AssetManager::LoadAudio(AssetHandle handle) {
    return LoadAudioInternal(handle, false);
}

AssetResult AssetManager::RegisterPythonScriptAtRelativePath(const std::string& normalizedRelativePath,
                                                             const std::string& displayName) {
    AssetResult result{};
    const std::optional<std::string> normalized = NormalizeAssetRelativePath(normalizedRelativePath);
    if (!normalized.has_value()) {
        result.Message = "Invalid relative asset path";
        return result;
    }

    if (const AssetMetadata* existing = m_registry.FindByRelativePath(*normalized)) {
        result.Success = true;
        result.Handle = existing->Handle;
        result.Message = "Asset already registered";
        return result;
    }

    const std::filesystem::path absolute = ResolveAssetPath(*normalized);
    if (!std::filesystem::exists(absolute)) {
        result.Message = "Asset file does not exist: " + absolute.string();
        return result;
    }
    if (!IsSupportedPythonScriptExtension(absolute)) {
        result.Message = "Unsupported Python script extension";
        return result;
    }

    AssetMetadata metadata{};
    metadata.Handle = AssetHandle::StableFromRelativePath(*normalized);
    metadata.Type = AssetType::PythonScript;
    metadata.Name = displayName.empty() ? absolute.stem().string() : displayName;
    metadata.RelativePath = *normalized;
    metadata.Available = true;

    if (m_registry.Contains(metadata.Handle)) {
        result.Message = "Duplicate asset handle for path";
        return result;
    }
    if (!m_registry.RegisterAsset(metadata)) {
        result.Message = "Failed to register asset metadata";
        return result;
    }

    result.Success = true;
    result.Handle = metadata.Handle;
    return result;
}

AssetResult AssetManager::LoadPythonScript(AssetHandle handle) {
    return LoadPythonScriptInternal(handle, false);
}

AssetResult AssetManager::LoadTextureInternal(AssetHandle handle, bool forceReload) {
    AssetResult result{};
    if (!handle.IsValid()) {
        result.Message = "Invalid asset handle";
        return result;
    }
    if (!m_graphicsResourcesEnabled) {
        result.Message = "Graphics resources are disabled";
        return result;
    }

    const AssetMetadata* metadata = m_registry.FindByHandle(handle);
    if (metadata == nullptr) {
        result.Message = "Asset handle is not registered";
        return result;
    }
    if (metadata->Type != AssetType::Texture2D) {
        result.Message = "Asset is not a Texture2D";
        return result;
    }

    if (!forceReload && m_textureCache.contains(handle)) {
        result.Success = true;
        result.Handle = handle;
        return result;
    }

    const std::filesystem::path absolute = ResolveAssetPath(metadata->RelativePath);
    if (!std::filesystem::exists(absolute)) {
        result.Message = "Texture file missing on disk: " + absolute.string();
        XORA_LOG_WARN("{}", result.Message);
        return result;
    }

    try {
        std::shared_ptr<Texture2D> texture(
            Texture2D::CreateFromFile(absolute.string()).release());
        m_textureCache[handle] = std::move(texture);
        result.Success = true;
        result.Handle = handle;
        return result;
    } catch (const std::exception& exception) {
        m_textureCache.erase(handle);
        result.Message = exception.what();
        XORA_LOG_ERROR("Failed to load texture asset {}: {}", handle.ToString(), exception.what());
        return result;
    }
}

std::shared_ptr<Texture2D> AssetManager::GetLoadedTexture(AssetHandle handle) const {
    const auto found = m_textureCache.find(handle);
    if (found == m_textureCache.end()) {
        return nullptr;
    }
    return found->second;
}

AssetResult AssetManager::LoadAudioInternal(AssetHandle handle, bool forceReload) {
    AssetResult result{};
    if (!handle.IsValid()) {
        result.Message = "Invalid asset handle";
        return result;
    }

    const AssetMetadata* metadata = m_registry.FindByHandle(handle);
    if (metadata == nullptr) {
        result.Message = "Asset handle is not registered";
        return result;
    }
    if (metadata->Type != AssetType::Audio) {
        result.Message = "Asset is not Audio";
        return result;
    }

    if (!forceReload && m_audioCache.contains(handle)) {
        result.Success = true;
        result.Handle = handle;
        return result;
    }

    const std::filesystem::path absolute = ResolveAssetPath(metadata->RelativePath);
    if (!std::filesystem::exists(absolute)) {
        result.Message = "Audio file missing on disk: " + absolute.string();
        XORA_LOG_WARN("{}", result.Message);
        return result;
    }

    std::shared_ptr<AudioClip> clip;
    const AudioLoadResult loadResult = AudioClip::LoadFromFile(absolute, clip);
    if (!loadResult.Success || clip == nullptr) {
        m_audioCache.erase(handle);
        result.Message = loadResult.Message.empty() ? "Failed to load audio clip" : loadResult.Message;
        return result;
    }

    m_audioCache[handle] = std::move(clip);
    result.Success = true;
    result.Handle = handle;
    return result;
}

std::shared_ptr<const AudioClip> AssetManager::GetLoadedAudio(AssetHandle handle) const {
    const auto found = m_audioCache.find(handle);
    if (found == m_audioCache.end()) {
        return nullptr;
    }
    return found->second;
}

AssetResult AssetManager::LoadPythonScriptInternal(AssetHandle handle, bool forceReload) {
    AssetResult result{};
    if (!handle.IsValid()) {
        result.Message = "Invalid asset handle";
        return result;
    }

    const AssetMetadata* metadata = m_registry.FindByHandle(handle);
    if (metadata == nullptr) {
        result.Message = "Asset handle is not registered";
        return result;
    }
    if (metadata->Type != AssetType::PythonScript) {
        result.Message = "Asset is not a Python script";
        return result;
    }

    if (!forceReload && m_scriptCache.contains(handle)) {
        result.Success = true;
        result.Handle = handle;
        return result;
    }

    const std::filesystem::path absolute = ResolveAssetPath(metadata->RelativePath);
    if (!std::filesystem::exists(absolute)) {
        result.Message = "Python script file missing on disk: " + absolute.string();
        XORA_LOG_WARN("{}", result.Message);
        return result;
    }

    auto script = std::make_shared<PythonScriptAsset>();
    const ScriptLoadResult loadResult =
        PythonScriptAsset::LoadFromFile(absolute, metadata->RelativePath, *script);
    if (!loadResult.Success || !script->IsValid()) {
        m_scriptCache.erase(handle);
        result.Message = loadResult.Message.empty() ? "Failed to load Python script" : loadResult.Message;
        return result;
    }

    m_scriptCache[handle] = std::move(script);
    result.Success = true;
    result.Handle = handle;
    return result;
}

std::shared_ptr<const PythonScriptAsset> AssetManager::GetLoadedPythonScript(AssetHandle handle) const {
    const auto found = m_scriptCache.find(handle);
    if (found == m_scriptCache.end()) {
        return nullptr;
    }
    return found->second;
}

bool AssetManager::IsAssetLoaded(AssetHandle handle) const {
    return m_textureCache.contains(handle) || m_audioCache.contains(handle) || m_scriptCache.contains(handle);
}

void AssetManager::UnloadAsset(AssetHandle handle) {
    m_textureCache.erase(handle);
    m_audioCache.erase(handle);
    m_scriptCache.erase(handle);
}

AssetResult AssetManager::ReloadAsset(AssetHandle handle) {
    return LoadTextureInternal(handle, true);
}

void AssetManager::UnloadAll() {
    m_textureCache.clear();
    m_audioCache.clear();
    m_scriptCache.clear();
}

void AssetManager::PrepareShutdown() {
    UnloadAll();
    m_graphicsResourcesEnabled = false;
}

} // namespace Xora2D