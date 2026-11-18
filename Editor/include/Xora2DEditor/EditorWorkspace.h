#pragma once

#include "Xora2DEditor/EditorPanel.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Xora2DEditor {

class EditorApplication;
class EditorContext;
class ViewportPanel;

class EditorWorkspace {
public:
    void RegisterPanel(std::unique_ptr<EditorPanel> panel);
    void Attach(EditorContext& context);
    void Detach();

    void DrawPanels(EditorContext& context);
    void RenderSceneToViewport(EditorApplication& application, EditorContext& context);

    [[nodiscard]] std::size_t GetPanelCount() const { return m_panels.size(); }
    [[nodiscard]] EditorPanel* FindPanel(const std::string& id) const;
    [[nodiscard]] ViewportPanel* GetViewportPanel() const;

private:
    std::vector<std::unique_ptr<EditorPanel>> m_panels;
    std::unordered_map<std::string, EditorPanel*> m_panelIndex;
    ViewportPanel* m_viewportPanel = nullptr;
};

} // namespace Xora2DEditor