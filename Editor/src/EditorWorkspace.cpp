#include "Xora2DEditor/EditorWorkspace.h"

#include "Xora2DEditor/EditorApplication.h"
#include "Xora2DEditor/Panels/ViewportPanel.h"

namespace Xora2DEditor {

void EditorWorkspace::RegisterPanel(std::unique_ptr<EditorPanel> panel) {
    if (panel == nullptr) {
        return;
    }
    const std::string id = panel->GetId();
    if (auto* viewport = dynamic_cast<ViewportPanel*>(panel.get())) {
        m_viewportPanel = viewport;
    }
    m_panelIndex[id] = panel.get();
    m_panels.push_back(std::move(panel));
}

void EditorWorkspace::Attach(EditorContext& context) {
    for (const auto& panel : m_panels) {
        panel->OnAttach(context);
    }
}

void EditorWorkspace::Detach() {
    for (const auto& panel : m_panels) {
        panel->OnDetach();
    }
}

void EditorWorkspace::DrawPanels(EditorContext& context) {
    for (const auto& panel : m_panels) {
        if (!panel->IsVisible()) {
            continue;
        }
        panel->OnImGui(context);
    }
}

void EditorWorkspace::RenderSceneToViewport(EditorApplication& application, EditorContext& context) {
    if (m_viewportPanel != nullptr) {
        m_viewportPanel->RenderScene(application, context);
    }
}

EditorPanel* EditorWorkspace::FindPanel(const std::string& id) const {
    const auto found = m_panelIndex.find(id);
    return found == m_panelIndex.end() ? nullptr : found->second;
}

ViewportPanel* EditorWorkspace::GetViewportPanel() const {
    return m_viewportPanel;
}

} // namespace Xora2DEditor