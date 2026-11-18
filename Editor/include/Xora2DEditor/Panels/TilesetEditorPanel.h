#pragma once

#include "Xora2DEditor/EditorPanel.h"

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Tilemap/Tileset.h"

#include <array>
#include <map>
#include <string>

namespace Xora2DEditor {

// "Tileset" panel: create a tileset from a spritesheet texture (tile size, margin, spacing, grid or manual regions),
// edit per-tile collision / names, preview the cut with a grid overlay, and save it as a `.tileset.yaml` asset.
class TilesetEditorPanel final : public EditorPanel {
public:
    TilesetEditorPanel() { SetVisible(false); }

    [[nodiscard]] const char* GetTitle() const override { return "Tileset"; }
    [[nodiscard]] std::string GetId() const override { return "Tileset"; }

    void OnImGui(EditorContext& context) override;

private:
    void LoadExisting(EditorContext& context, Xora2D::AssetHandle handle);
    void StartNew(EditorContext& context, Xora2D::AssetHandle texture);
    void SetTexture(EditorContext& context, Xora2D::AssetHandle texture);
    void Rebuild();
    void DrawPreview(EditorContext& context);
    void DrawSelectedTile();
    void Save(EditorContext& context);

    Xora2D::AssetHandle m_handle{}; // invalid = new tileset
    std::array<char, 64> m_name{};
    std::array<char, 128> m_folder{};
    Xora2D::TilesetSettings m_settings;
    std::map<Xora2D::TileId, Xora2D::TileProperties> m_properties;
    Xora2D::Tileset m_preview;
    std::string m_previewError;
    Xora2D::TileId m_selectedTile = 0;
    std::array<int, 4> m_newRegion{0, 0, 16, 16};
    std::string m_status;
    bool m_statusIsError = false;
    bool m_active = false;
};

} // namespace Xora2DEditor
