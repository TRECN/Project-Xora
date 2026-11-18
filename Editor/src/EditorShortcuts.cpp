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

        // Tile painting: undo / redo / clipboard, tool keys and selection moves (only while painting).
        TilemapEditorState& tiles = context.GetTilemapEditor();
        if (!io.WantTextInput && tiles.IsPainting(context)) {
            if (primary && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                (void)(io.KeyShift ? tiles.Redo(context) : tiles.Undo(context));
                return;
            }
            if (primary && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
                (void)tiles.Redo(context);
                return;
            }
            if (primary && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
                tiles.CopySelection(context);
            }
            if (primary && ImGui::IsKeyPressed(ImGuiKey_X, false)) {
                tiles.CutSelection(context);
            }
            if (primary && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
                tiles.BeginPaste();
            }
            if (!primary && context.IsViewportHovered()) {
                const std::pair<ImGuiKey, TilemapTool> toolKeys[] = {
                    {ImGuiKey_B, TilemapTool::Brush},      {ImGuiKey_E, TilemapTool::Eraser},
                    {ImGuiKey_R, TilemapTool::Rectangle},  {ImGuiKey_G, TilemapTool::Bucket},
                    {ImGuiKey_I, TilemapTool::Eyedropper}, {ImGuiKey_S, TilemapTool::Select}};
                for (const auto& [key, tool] : toolKeys) {
                    if (ImGui::IsKeyPressed(key, false)) {
                        tiles.Tool = tool;
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
                    tiles.FlipX = !tiles.FlipX;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
                    tiles.FlipY = !tiles.FlipY;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_T, false)) {
                    tiles.Rotation = (tiles.Rotation + 1) % 4;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
                    tiles.DeleteSelection(context);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                    tiles.ClearSelection();
                }
                const std::pair<ImGuiKey, Xora2D::GridCoord> arrows[] = {{ImGuiKey_LeftArrow, {-1, 0}},
                                                                          {ImGuiKey_RightArrow, {1, 0}},
                                                                          {ImGuiKey_UpArrow, {0, 1}},
                                                                          {ImGuiKey_DownArrow, {0, -1}}};
                for (const auto& [key, offset] : arrows) {
                    if (ImGui::IsKeyPressed(key) && tiles.GetSelection().has_value()) {
                        tiles.NudgeSelection(context, offset);
                    }
                }
            }
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