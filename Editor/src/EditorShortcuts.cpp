#include "Xora2DEditor/EditorShortcuts.h"

#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/EditorUI.h"

#include <imgui.h>

namespace Xora2DEditor {

void EditorShortcuts::HandleGlobalShortcuts(EditorContext& context, EditorUI& ui) {
    // Play-mode shortcuts work whenever no text field is being edited (including while the game view has focus).
    {
        const ImGuiIO& io = ImGui::GetIO();
        const bool primary = io.KeySuper || io.KeyCtrl;
        if (!io.WantTextInput && primary && ImGui::IsKeyPressed(ImGuiKey_P, false) && !context.IsEditingPrefab()) {
            if (io.KeyShift) {
                if (!context.PausePlayMode()) {
                    (void)context.ResumePlayMode();
                }
            } else if (!context.StartPlayMode()) {
                (void)context.StopPlayMode();
            }
        }
        if (!io.WantTextInput && primary && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            ui.RequestSaveScene(context, io.KeyShift);
        }
    }

    if (ImGui::GetIO().WantCaptureKeyboard) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const bool primary = io.KeySuper || io.KeyCtrl;

    if (primary && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (io.KeyShift) {
            ui.ShowStatusMessage("Redo (placeholder).");
        } else {
            ui.ShowStatusMessage("Undo (placeholder).");
        }
    }
}

} // namespace Xora2DEditor