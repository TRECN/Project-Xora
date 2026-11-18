#include "Xora2DEditor/EditorSelection.h"

namespace Xora2DEditor {

void EditorSelection::Select(const Xora2D::UUID& uuid) {
    if (uuid.IsValid()) {
        m_selectedUuid = uuid;
    } else {
        Clear();
    }
}

void EditorSelection::Clear() {
    m_selectedUuid.reset();
}

bool EditorSelection::IsSelected(const Xora2D::UUID& uuid) const {
    return m_selectedUuid.has_value() && *m_selectedUuid == uuid;
}

Xora2D::Entity EditorSelection::Resolve(const Xora2D::Scene& scene) const {
    if (!m_selectedUuid.has_value()) {
        return {};
    }
    return scene.FindEntityByUUID(*m_selectedUuid);
}

bool EditorSelection::IsSelectionValid(const Xora2D::Scene& scene) const {
  if (!m_selectedUuid.has_value()) {
        return false;
    }
    return Resolve(scene).IsValid();
}

void EditorSelection::Sanitize(const Xora2D::Scene& scene) {
    if (m_selectedUuid.has_value() && !Resolve(scene).IsValid()) {
        Clear();
    }
}

void EditorSelection::OnEntityDestroyed(const Xora2D::UUID& uuid) {
    if (IsSelected(uuid)) {
        Clear();
    }
}

void EditorSelection::OnSceneReplaced() {
    Clear();
}

} // namespace Xora2DEditor