#include "Xora2DEditor/Content/EditorAssetAssignments.h"

#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"

#include "Xora2D/Tilemap/TilemapComponent.h"

#include <algorithm>

namespace Xora2DEditor {

const char* AssignmentTargetLabel(Xora2D::AssetType type) {
    switch (type) {
    case Xora2D::AssetType::Texture2D:
        return "Sprite Renderer";
    case Xora2D::AssetType::Audio:
        return "Audio Source";
    case Xora2D::AssetType::PythonScript:
#if XORA2D_SCRIPTING_ENABLED
        return "Python Script";
#else
        return nullptr;
#endif
    case Xora2D::AssetType::AnimationClip:
        return "Animation";
    case Xora2D::AssetType::Tileset:
        return "Tilemap";
    default:
        return nullptr;
    }
}

AssetAssignmentResult AssignAssetToEntity(Xora2D::Scene& scene, Xora2D::Entity entity, Xora2D::AssetHandle handle,
                                          const Xora2D::AssetRegistry& registry) {
    AssetAssignmentResult result{};
    if (!entity.IsValid() || !scene.IsValid(entity)) {
        result.Message = "No valid entity selected";
        return result;
    }
    const Xora2D::AssetMetadata* meta = registry.FindByHandle(handle);
    if (meta == nullptr) {
        result.Message = "Asset is not registered";
        return result;
    }
    if (!meta->Available) {
        result.Message = "Asset file is missing on disk";
        return result;
    }

    switch (meta->Type) {
    case Xora2D::AssetType::Texture2D: {
        if (!entity.HasComponent<Xora2D::SpriteRendererComponent>()) {
            entity.AddComponent<Xora2D::SpriteRendererComponent>();
        }
        entity.GetComponent<Xora2D::SpriteRendererComponent>().TextureAsset = handle;
        scene.ResolveSpriteTextures();
        break;
    }
    case Xora2D::AssetType::Audio:
        if (!entity.HasComponent<Xora2D::AudioSourceComponent>()) {
            entity.AddComponent<Xora2D::AudioSourceComponent>();
        }
        entity.GetComponent<Xora2D::AudioSourceComponent>().ClipAsset = handle;
        break;
    case Xora2D::AssetType::PythonScript:
#if XORA2D_SCRIPTING_ENABLED
        if (!entity.HasComponent<Xora2D::PythonScriptComponent>()) {
            entity.AddComponent<Xora2D::PythonScriptComponent>();
        }
        entity.GetComponent<Xora2D::PythonScriptComponent>().ScriptAsset = handle;
        break;
#else
        result.Message = "Python scripting is disabled in this build";
        return result;
#endif
    case Xora2D::AssetType::AnimationClip: {
        if (!entity.HasComponent<Xora2D::AnimationComponent>()) {
            entity.AddComponent<Xora2D::AnimationComponent>();
        }
        auto& animation = entity.GetComponent<Xora2D::AnimationComponent>();
        animation.DefaultClip = handle;
        if (std::find(animation.ClipAssets.begin(), animation.ClipAssets.end(), handle) == animation.ClipAssets.end()) {
            animation.ClipAssets.push_back(handle);
        }
        break;
    }
    case Xora2D::AssetType::Tileset: {
        if (!entity.HasComponent<Xora2D::TilemapComponent>()) {
            entity.AddComponent<Xora2D::TilemapComponent>().CreateDefaultLayers();
        }
        auto& tilemap = entity.GetComponent<Xora2D::TilemapComponent>();
        tilemap.Tileset = handle;
        tilemap.MarkCollisionDirty();
        break;
    }
    default:
        result.Message = "This asset type cannot be assigned to an entity";
        return result;
    }

    result.Success = true;
    result.Message = "Assigned '" + meta->Name + "' to " + AssignmentTargetLabel(meta->Type);
    return result;
}

} // namespace Xora2DEditor
