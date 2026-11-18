#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Assets/AssetTypes.h"
#include "Xora2D/Scene/Entity.h"

#include <cstdint>
#include <string>

namespace Xora2D {

class AssetRegistry;
class Scene;

} // namespace Xora2D

namespace Xora2DEditor {

// ImGui drag-drop payload for content browser items. Carries only the stable handle and type (never pointers or
// transient paths); the receiver re-validates both against the registry.
inline constexpr const char* kAssetDragDropPayload = "XORA_ASSET_HANDLE";

struct AssetDragPayload {
    std::uint64_t Handle = 0; // 0 for folders and unregistered files
    std::uint8_t Type = 0;    // Xora2D::AssetType
    // Resource-relative path, used only for moves inside the content browser (NUL-terminated).
    char ResourcePath[512] = {};
};

struct AssetAssignmentResult {
    bool Success = false;
    std::string Message;
};

// Returns which component an asset of `type` would be assigned to ("Sprite Renderer", ...), or nullptr.
[[nodiscard]] const char* AssignmentTargetLabel(Xora2D::AssetType type);

// Assigns a registered asset to the matching component of `entity`, adding the component if needed:
// textures -> SpriteRendererComponent, audio -> AudioSourceComponent, Python scripts -> PythonScriptComponent
// (scripting builds only), animation clips -> AnimationComponent (default clip + clip list).
[[nodiscard]] AssetAssignmentResult AssignAssetToEntity(Xora2D::Scene& scene, Xora2D::Entity entity,
                                                        Xora2D::AssetHandle handle,
                                                        const Xora2D::AssetRegistry& registry);

} // namespace Xora2DEditor
