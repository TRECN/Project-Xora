#include "Xora2DEditor/EditorLayer.h"

#include "Xora2DEditor/EditorApplication.h"
#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/EditorShortcuts.h"
#include "Xora2DEditor/Panels/ConsolePanel.h"
#include "Xora2DEditor/Panels/ContentBrowserPanel.h"
#include "Xora2DEditor/Panels/InspectorPanel.h"
#include "Xora2DEditor/Panels/ProjectSettingsPanel.h"
#include "Xora2DEditor/Panels/SceneHierarchyPanel.h"
#include "Xora2DEditor/Panels/TilemapEditorPanel.h"
#include "Xora2DEditor/Panels/ParticleEditorPanel.h"
#include "Xora2DEditor/Panels/TilesetEditorPanel.h"
#include "Xora2DEditor/Panels/ViewportPanel.h"

#include <cmath>

namespace Xora2DEditor {

EditorLayer::EditorLayer() {
    BuildDefaultPanels();
}

void EditorLayer::BuildDefaultPanels() {
    m_workspace.RegisterPanel(std::make_unique<ViewportPanel>());
    m_workspace.RegisterPanel(CreateSceneHierarchyPanel());
    m_workspace.RegisterPanel(CreateInspectorPanel());
    m_workspace.RegisterPanel(CreateContentBrowserPanel());
    m_workspace.RegisterPanel(std::make_unique<ConsolePanel>());
    m_workspace.RegisterPanel(std::make_unique<ProjectSettingsPanel>());
    m_workspace.RegisterPanel(std::make_unique<TilemapEditorPanel>());
    m_workspace.RegisterPanel(std::make_unique<TilesetEditorPanel>());
    m_workspace.RegisterPanel(std::make_unique<ParticleEditorPanel>());
}

void EditorLayer::Attach(EditorContext& context) {
    m_workspace.Attach(context);
}

void EditorLayer::Detach() {
    m_workspace.Detach();
}

void EditorLayer::OnUpdate(float deltaSeconds, EditorContext& context) {
    m_sampleAnimationTime += deltaSeconds;
    // Edit mode never simulates; only the runtime copy advances while playing (see EditorPlaySession).
    context.UpdatePlayMode(deltaSeconds);
}

void EditorLayer::OnImGui(EditorContext& context, EditorApplication& application) {
    // The viewport panel reports focus/hover again if it is drawn this frame (hidden viewport => no game input).
    context.SetViewportInputState(false, false);
    EditorShortcuts::HandleGlobalShortcuts(context, m_ui);
    m_ui.OnImGui(context, application);
}

void EditorLayer::RenderViewport(EditorApplication& application, EditorContext& context) {
    m_workspace.RenderSceneToViewport(application, context);
}

} // namespace Xora2DEditor