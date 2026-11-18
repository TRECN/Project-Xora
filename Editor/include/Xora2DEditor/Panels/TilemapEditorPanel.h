#pragma once

#include "Xora2DEditor/EditorPanel.h"

#include <array>
#include <string>

namespace Xora2D {
struct TilemapComponent;
}

namespace Xora2DEditor {

class TilemapEditorState;

// "Tilemap" panel: tools, brush options, layers and the tile palette of the selected tilemap. Painting itself happens
// in the Viewport (see ViewportPanel + TilemapEditorState).
class TilemapEditorPanel final : public EditorPanel {
public:
    TilemapEditorPanel() { SetVisible(false); }

    [[nodiscard]] const char* GetTitle() const override { return "Tilemap"; }
    [[nodiscard]] std::string GetId() const override { return "Tilemap"; }

    void OnImGui(EditorContext& context) override;

private:
    void DrawNoTarget(EditorContext& context);
    void DrawTools(EditorContext& context, TilemapEditorState& state);
    void DrawLayers(EditorContext& context, TilemapEditorState& state, Xora2D::TilemapComponent& tilemap);
    void DrawPalette(EditorContext& context, TilemapEditorState& state, Xora2D::TilemapComponent& tilemap);

    float m_paletteSize = 32.0f;
    std::int32_t m_renaming = -1;
    bool m_focusRename = false;
    std::array<char, 64> m_renameBuffer{};
};

} // namespace Xora2DEditor
