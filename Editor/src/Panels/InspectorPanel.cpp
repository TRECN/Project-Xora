#include "Xora2DEditor/Panels/InspectorPanel.h"

#include "Xora2DEditor/AssetInspectorUI.h"
#include "Xora2DEditor/ComponentInspectorUI.h"
#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/PrefabEditorUI.h"

#include "Xora2D/Scene/Entity.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Xora2DEditor {
namespace {

class InspectorPanel final : public EditorPanel {
public:
    [[nodiscard]] const char* GetTitle() const override { return "Inspector"; }
    [[nodiscard]] std::string GetId() const override { return "Inspector"; }

    void OnImGui(EditorContext& context) override {
        if (ImGui::Begin(GetTitle())) {
            const auto scene = context.GetScene();
            if (context.GetInspectorFocus() == InspectorFocus::Asset) {
                if (scene != nullptr) {
                    context.GetSelection().Sanitize(*scene);
                }
                if (context.GetSelectedEntity().IsValid() && ImGui::SmallButton("< Back to selected entity")) {
                    context.SetInspectorFocus(InspectorFocus::Entity);
                }
                DrawAssetInspector(context);
            } else if (scene == nullptr) {
                ImGui::TextWrapped("No active scene.");
            } else {
                context.GetSelection().Sanitize(*scene);
                const Xora2D::Entity selected = context.GetSelectedEntity();
                if (!selected.IsValid()) {
                    ImGui::TextWrapped("Select an entity in the Hierarchy to inspect its components.");
                } else {
                    if (context.IsPlayModeActive()) {
                        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
                                           "Runtime values - changes are discarded on Stop");
                    }
                    if (context.IsEditingPrefab()) {
                        ImGui::TextColored(ImVec4(0.40f, 0.78f, 1.00f, 1.0f), "Prefab '%s' (isolated edit)",
                                           context.GetEditedPrefabName().c_str());
                    }
                    if (DrawPrefabInstanceSection(selected, context)) {
                        m_tracker.Invalidate();
                    }
                    const Xora2D::Entity current = context.GetSelectedEntity();
                    if (current.IsValid()) {
                        // Overrides are recorded only when the Inspector received input this frame.
                        m_tracker.Begin(scene.get(), current);
                        const bool editedBefore = GImGui->ActiveIdHasBeenEditedThisFrame;
                        DrawEntityInspector(current, context);
                        const bool widgetEdited = !editedBefore && GImGui->ActiveIdHasBeenEditedThisFrame;
                        const bool inspectorInput =
                            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                            (ImGui::IsMouseReleased(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                             ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) || ImGui::IsKeyPressed(ImGuiKey_Tab));
                        const bool dropped =
                            ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                                   ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                        m_tracker.End(context.GetSelectedEntity(), context, widgetEdited || inspectorInput || dropped);
                    }
                }
            }
        }
        ImGui::End();
    }

private:
    PrefabEditTracker m_tracker;
};

} // namespace

std::unique_ptr<EditorPanel> CreateInspectorPanel() {
    return std::make_unique<InspectorPanel>();
}

} // namespace Xora2DEditor