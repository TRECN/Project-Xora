#pragma once

#include "Xora2D/Core/UUID.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"

#include <optional>

namespace Xora2DEditor {

// Editor-only selection stored by entity UUID (stable across registry re-use within a scene).
class EditorSelection {
public:
    void Select(const Xora2D::UUID& uuid);
    void Clear();

    [[nodiscard]] bool HasSelection() const { return m_selectedUuid.has_value(); }
    [[nodiscard]] const std::optional<Xora2D::UUID>& GetSelectedUUID() const { return m_selectedUuid; }

    [[nodiscard]] bool IsSelected(const Xora2D::UUID& uuid) const;
    [[nodiscard]] Xora2D::Entity Resolve(const Xora2D::Scene& scene) const;
    [[nodiscard]] bool IsSelectionValid(const Xora2D::Scene& scene) const;

    // Drop selection when the entity is gone or the scene was replaced.
    void Sanitize(const Xora2D::Scene& scene);
    void OnEntityDestroyed(const Xora2D::UUID& uuid);
    void OnSceneReplaced();

private:
    std::optional<Xora2D::UUID> m_selectedUuid;
};

} // namespace Xora2DEditor