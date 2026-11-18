#pragma once

#include "Xora2DEditor/EditorUI.h"
#include "Xora2DEditor/EditorWorkspace.h"

namespace Xora2DEditor {

class EditorApplication;
class EditorContext;
class ImGuiIntegration;

// Coordinates editor workspace + chrome for each frame.
class EditorLayer {
public:
    EditorLayer();

    void Attach(EditorContext& context);
    void Detach();

    void OnUpdate(float deltaSeconds, EditorContext& context);
    void OnImGui(EditorContext& context, EditorApplication& application);
    void RenderViewport(EditorApplication& application, EditorContext& context);

    EditorWorkspace& GetWorkspace() { return m_workspace; }
    EditorUI& GetUI() { return m_ui; }

private:
    void BuildDefaultPanels();

    EditorWorkspace m_workspace;
    EditorUI m_ui{m_workspace};
    float m_sampleAnimationTime = 0.0f;
};

} // namespace Xora2DEditor