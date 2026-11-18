#include "Xora2DEditor/Panels/InspectorPanel.h"

#include "Xora2DEditor/AssetInspectorUI.h"
#include "Xora2DEditor/ComponentInspectorUI.h"
#include "Xora2DEditor/EditorContext.h"

#include "Xora2D/Scene/Entity.h"

#include <imgui.h>

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
                    DrawEntityInspector(selected, context);
                }
            }
        }
        ImGui::End();
    }
};

} // namespace

std::unique_ptr<EditorPanel> CreateInspectorPanel() {
    return std::make_unique<InspectorPanel>();
}

} // namespace Xora2DEditor