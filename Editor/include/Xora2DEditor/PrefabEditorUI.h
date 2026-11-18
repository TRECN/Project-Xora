#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Core/UUID.h"
#include "Xora2D/Prefab/PrefabUtility.h"
#include "Xora2D/Scene/Entity.h"

#include <glm/vec2.hpp>

#include <optional>
#include <string>

namespace Xora2D {
class Scene;
}

namespace Xora2DEditor {

class EditorContext;

// Event-driven override tracking for the Inspector: a snapshot of the selected instance is taken when it is
// selected (and after each recorded edit); when the Inspector reports a possible edit (widget edited, click,
// Enter, drop) the snapshot is compared once and the changed properties become overrides. Nothing is compared on
// frames without input.
class PrefabEditTracker {
public:
    void Begin(const Xora2D::Scene* scene, const Xora2D::Entity& entity);
    void End(Xora2D::Entity entity, EditorContext& context, bool possibleEdit);
    void Invalidate() { m_valid = false; }

private:
    const Xora2D::Scene* m_scene = nullptr;
    Xora2D::UUID m_entity{};
    bool m_valid = false;
    Xora2D::PrefabPropertySnapshot m_snapshot;
};

// Inspector section for prefab instances: source prefab, sync status, overrides with per-property Revert, and the
// Apply / Revert All / Refresh / Open Prefab / Unpack actions. Returns true when it changed the entity.
bool DrawPrefabInstanceSection(Xora2D::Entity entity, EditorContext& context);

// Toolbar strip shown while a prefab is open for editing (Save / Cancel).
void DrawPrefabEditBar(EditorContext& context);

// Accepts a prefab dragged from the Content panel onto the last item / custom target. Returns its handle.
[[nodiscard]] std::optional<Xora2D::AssetHandle> AcceptPrefabDragDrop(EditorContext& context);

// Records the result of a prefab operation for display ("last prefab action" line) and the log.
void ReportPrefabResult(bool success, const std::string& message);
[[nodiscard]] const std::string& GetLastPrefabMessage(bool* success = nullptr);

} // namespace Xora2DEditor
