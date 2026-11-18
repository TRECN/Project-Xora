#include "Xora2DEditor/Panels/TilemapEditorPanel.h"

#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/EditorSceneCommands.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Renderer/Texture2D.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Tilemap/TilemapComponent.h"
#include "Xora2D/Tilemap/Tileset.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace Xora2DEditor {

namespace {

const ImVec4 kAccent(0.40f, 0.78f, 1.00f, 1.0f);
const ImVec4 kError(0.95f, 0.45f, 0.40f, 1.0f);
const ImVec4 kOk(0.45f, 0.85f, 0.50f, 1.0f);

ImTextureID ToTextureId(std::uint32_t rendererId) {
    return static_cast<ImTextureID>(static_cast<std::uintptr_t>(rendererId));
}

bool ToolButton(const char* label, bool active, const char* tooltip) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    const bool pressed = ImGui::Button(label);
    if (active) {
        ImGui::PopStyleColor();
    }
    ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

} // namespace

void TilemapEditorPanel::OnImGui(EditorContext& context) {
    bool visible = IsVisible();
    ImGui::SetNextWindowSize(ImVec2(380.0f, 640.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(GetTitle(), &visible)) {
        ImGui::End();
        SetVisible(visible);
        return;
    }
    SetVisible(visible);
    TilemapEditorState& state = context.GetTilemapEditor();
    if (!visible) {
        state.Enabled = false;
    }

    if (context.IsPlayModeActive()) {
        ImGui::TextWrapped("Tile painting is available in Edit mode. Runtime tilemaps can be changed from scripts "
                           "(entity.tilemap).");
        ImGui::End();
        return;
    }

    Xora2D::Entity target = state.GetTarget(context);
    if (!target.IsValid()) {
        state.Enabled = false;
        DrawNoTarget(context);
        ImGui::End();
        return;
    }
    auto& tilemap = target.GetComponent<Xora2D::TilemapComponent>();

    ImGui::TextColored(kAccent, "%s", target.GetName().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%zu tiles, %zu layers", tilemap.GetTotalTileCount(), tilemap.Layers.size());
    ImGui::Checkbox("Paint in viewport", &state.Enabled);
    ImGui::SetItemTooltip("Route viewport clicks to the tile tools and use the editor camera (wheel: zoom, right / "
                          "middle drag: pan)");
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &state.ShowGrid);

    DrawTools(context, state);
    ImGui::Separator();
    DrawLayers(context, state, tilemap);
    ImGui::Separator();
    DrawPalette(context, state, tilemap);

    bool isError = false;
    const std::string& message = state.GetMessage(&isError);
    if (!message.empty()) {
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(isError ? kError : kOk, "%s", message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

void TilemapEditorPanel::DrawNoTarget(EditorContext& context) {
    ImGui::TextWrapped("Select an entity with a Tilemap component to paint tiles.");
    ImGui::Spacing();
    const std::shared_ptr<Xora2D::Scene> scene = context.GetScene();
    if (scene == nullptr) {
        return;
    }
    if (ImGui::Button("Create Tilemap Entity")) {
        Xora2D::Entity entity = scene->CreateEntity("Tilemap");
        auto& tilemap = entity.AddComponent<Xora2D::TilemapComponent>();
        tilemap.CreateDefaultLayers();
        // Use the first tileset of the project, if any.
        for (const Xora2D::AssetMetadata& meta : context.GetAssetRegistry().ListAssets()) {
            if (meta.Type == Xora2D::AssetType::Tileset) {
                tilemap.Tileset = meta.Handle;
                if (context.GetAssetManager().LoadTileset(meta.Handle).Success) {
                    const auto tileset = context.GetAssetManager().GetLoadedTileset(meta.Handle);
                    tilemap.TileSize = {tileset->GetSettings().TileWidth, tileset->GetSettings().TileHeight};
                }
                break;
            }
        }
        context.SelectEntity(entity);
        context.GetTilemapEditor().Enabled = true;
    }
    Xora2D::Entity selected = context.GetSelectedEntity();
    if (selected.IsValid()) {
        ImGui::SameLine();
        if (ImGui::Button("Add Tilemap to Selected")) {
            (void)AddComponentToEntity(*scene, selected, SceneComponentKind::Tilemap);
            context.GetTilemapEditor().Enabled = true;
        }
    }
}

void TilemapEditorPanel::DrawTools(EditorContext& context, TilemapEditorState& state) {
    const struct {
        TilemapTool Tool;
        const char* Label;
        const char* Tip;
    } tools[] = {
        {TilemapTool::Brush, "Brush", "Paint tiles (B). Drag to paint continuously."},
        {TilemapTool::Eraser, "Erase", "Erase tiles (E)."},
        {TilemapTool::Rectangle, "Rect", "Fill a rectangle (R). Shift: erase the rectangle."},
        {TilemapTool::Bucket, "Fill", "Bucket fill a connected area (G). Shift: erase it."},
        {TilemapTool::Eyedropper, "Pick", "Pick the tile under the cursor (I)."},
        {TilemapTool::Select, "Select", "Select a region (S): copy / cut / paste / delete, arrows move it."},
    };
    for (const auto& tool : tools) {
        if (ToolButton(tool.Label, state.Tool == tool.Tool, tool.Tip)) {
            state.Tool = tool.Tool;
            state.Enabled = true;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    ImGui::Checkbox("Flip X", &state.FlipX);
    ImGui::SameLine();
    ImGui::Checkbox("Flip Y", &state.FlipY);
    ImGui::SameLine();
    if (ImGui::Button("Rotate")) {
        state.Rotation = (state.Rotation + 1) % 4;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d deg", state.Rotation * 90);

    ImGui::BeginDisabled(!state.CanUndo(context));
    if (ImGui::Button("Undo")) {
        (void)state.Undo(context);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.CanRedo(context));
    if (ImGui::Button("Redo")) {
        (void)state.Redo(context);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Cmd/Ctrl+Z, Cmd/Ctrl+Shift+Z");

    if (state.Tool == TilemapTool::Select) {
        const bool hasSelection = state.GetSelection().has_value();
        ImGui::BeginDisabled(!hasSelection);
        if (ImGui::Button("Copy")) {
            state.CopySelection(context);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cut")) {
            state.CutSelection(context);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            state.DeleteSelection(context);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(state.GetClipboard().IsEmpty());
        if (ImGui::Button("Paste")) {
            state.BeginPaste();
        }
        ImGui::EndDisabled();
        if (hasSelection) {
            const auto& selection = *state.GetSelection();
            ImGui::TextDisabled("Selection (%d, %d) %d x %d - arrow keys move it", selection.Min.X, selection.Min.Y,
                                selection.Width(), selection.Height());
        }
    }
}

void TilemapEditorPanel::DrawLayers(EditorContext& context, TilemapEditorState& state,
                                    Xora2D::TilemapComponent& tilemap) {
    ImGui::TextUnformatted("Layers");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Add")) {
        state.Apply(context, Xora2D::AddLayer(tilemap, "Layer " + std::to_string(tilemap.Layers.size() + 1),
                                              tilemap.ActiveLayer + 1));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Duplicate")) {
        state.Apply(context, Xora2D::DuplicateLayer(tilemap, tilemap.ActiveLayer));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(tilemap.Layers.size() <= 1);
    if (ImGui::SmallButton("Remove")) {
        state.Apply(context, Xora2D::RemoveLayer(tilemap, tilemap.ActiveLayer));
    }
    ImGui::EndDisabled();

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##Layers", 6, flags)) {
        return;
    }
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Show");
    ImGui::TableSetupColumn("Lock");
    ImGui::TableSetupColumn("Solid");
    ImGui::TableSetupColumn("Opacity", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Order");
    ImGui::TableHeadersRow();
    // Top of the list = drawn last (topmost).
    std::optional<std::pair<std::int32_t, std::int32_t>> move;
    for (std::int32_t index = static_cast<std::int32_t>(tilemap.Layers.size()) - 1; index >= 0; --index) {
        Xora2D::TilemapLayer& layer = tilemap.Layers[static_cast<std::size_t>(index)];
        ImGui::PushID(index);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const bool active = tilemap.ActiveLayer == index;
        if (m_renaming == index) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::IsWindowAppearing() || m_focusRename) {
                ImGui::SetKeyboardFocusHere();
                m_focusRename = false;
            }
            if (ImGui::InputText("##Rename", m_renameBuffer.data(), m_renameBuffer.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                state.Apply(context, Xora2D::RenameLayer(tilemap, index, m_renameBuffer.data()));
                m_renaming = -1;
            }
            if (ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
                m_renaming = -1;
            }
        } else {
            std::string label = layer.Name;
            if (layer.AboveSprites) {
                label += "  (front)";
            }
            if (ImGui::Selectable(label.c_str(), active, ImGuiSelectableFlags_AllowDoubleClick)) {
                tilemap.ActiveLayer = index;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    m_renaming = index;
                    m_focusRename = true;
                    std::snprintf(m_renameBuffer.data(), m_renameBuffer.size(), "%s", layer.Name.c_str());
                }
            }
            ImGui::SetItemTooltip("%zu tiles. Double-click to rename.", tilemap.GetTileCount(index));
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::Checkbox("##Visible", &layer.Visible);
        ImGui::TableSetColumnIndex(2);
        ImGui::Checkbox("##Locked", &layer.Locked);
        ImGui::TableSetColumnIndex(3);
        if (ImGui::Checkbox("##Collision", &layer.Collision)) {
            tilemap.MarkCollisionDirty();
        }
        ImGui::SetItemTooltip("Tiles on this layer use their tileset collision");
        ImGui::TableSetColumnIndex(4);
        ImGui::SetNextItemWidth(70.0f);
        ImGui::SliderFloat("##Opacity", &layer.Opacity, 0.0f, 1.0f, "%.2f");
        ImGui::TableSetColumnIndex(5);
        ImGui::BeginDisabled(index + 1 >= static_cast<std::int32_t>(tilemap.Layers.size()));
        if (ImGui::ArrowButton("##Up", ImGuiDir_Up)) {
            move = std::make_pair(index, index + 1);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(index == 0);
        if (ImGui::ArrowButton("##Down", ImGuiDir_Down)) {
            move = std::make_pair(index, index - 1);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (move.has_value()) {
        state.Apply(context, Xora2D::MoveLayer(tilemap, move->first, move->second));
    }
    if (tilemap.IsValidLayer(tilemap.ActiveLayer)) {
        const std::string editable = Xora2D::CheckLayerEditable(tilemap, tilemap.ActiveLayer);
        if (!editable.empty()) {
            ImGui::TextColored(kError, "%s: painting is disabled", editable.c_str());
        }
        bool above = tilemap.Layers[static_cast<std::size_t>(tilemap.ActiveLayer)].AboveSprites;
        if (ImGui::Checkbox("Active layer draws in front of sprites", &above)) {
            tilemap.Layers[static_cast<std::size_t>(tilemap.ActiveLayer)].AboveSprites = above;
        }
    }
}

void TilemapEditorPanel::DrawPalette(EditorContext& context, TilemapEditorState& state,
                                     Xora2D::TilemapComponent& tilemap) {
    const Xora2D::AssetHandle handle = tilemap.GetLayerTileset(tilemap.ActiveLayer);
    Xora2D::AssetManager& assets = context.GetAssetManager();
    const Xora2D::AssetMetadata* meta = context.GetAssetRegistry().FindByHandle(handle);
    ImGui::TextUnformatted("Palette");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", meta != nullptr ? meta->Name.c_str() : "(no tileset)");
    if (meta == nullptr) {
        ImGui::TextWrapped("Assign a tileset in the Inspector (or drag one from the Content panel onto the Tileset "
                           "field). Create tilesets from a texture: right-click it > Create Tileset.");
        return;
    }
    if (!assets.LoadTileset(handle).Success) {
        ImGui::TextColored(kError, "Tileset could not be loaded");
        return;
    }
    const auto tileset = assets.GetLoadedTileset(handle);
    if (meta != nullptr && ImGui::SmallButton("Edit Tileset")) {
        context.RequestTilesetEditor({handle, {}});
    }
    const Xora2D::AssetHandle textureHandle = tileset->GetSettings().Texture;
    std::uint32_t textureId = 0;
    if (assets.LoadTexture(textureHandle).Success) {
        if (const auto texture = assets.GetLoadedTexture(textureHandle)) {
            textureId = texture->GetRendererId();
        }
    }
    if (textureId == 0) {
        ImGui::TextColored(kError, "The tileset's texture is missing");
    }
    ImGui::SliderFloat("Tile preview size", &m_paletteSize, 16.0f, 96.0f, "%.0f px");
    const Xora2D::TileDefinition* selected = tileset->FindTile(state.SelectedTile);
    ImGui::Text("Selected: tile %u", state.SelectedTile);
    if (selected != nullptr) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s", Xora2D::ToString(selected->Collision),
                            selected->Name.empty() ? "" : (" - " + selected->Name).c_str());
    }

    ImGui::BeginChild("##PaletteGrid", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    const float available = ImGui::GetContentRegionAvail().x;
    const float cell = m_paletteSize + ImGui::GetStyle().ItemSpacing.x;
    const int perRow = std::max(1, static_cast<int>(available / cell));
    int column = 0;
    for (const Xora2D::TileDefinition& tile : tileset->GetTiles()) {
        ImGui::PushID(static_cast<int>(tile.Id));
        const bool isSelected = tile.Id == state.SelectedTile;
        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
        } else {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
        }
        bool clicked = false;
        if (textureId != 0) {
            // The texture is stored bottom-up: ImGui's uv0 is the top-left corner.
            clicked = ImGui::ImageButton("##Tile", ToTextureId(textureId), ImVec2(m_paletteSize, m_paletteSize),
                                         ImVec2(tile.RenderUV.x, tile.RenderUV.w), ImVec2(tile.RenderUV.z, tile.RenderUV.y));
        } else {
            clicked = ImGui::Button(std::to_string(tile.Id).c_str(), ImVec2(m_paletteSize + 4.0f, m_paletteSize + 4.0f));
        }
        ImGui::PopStyleVar();
        if (isSelected) {
            ImGui::PopStyleColor();
        }
        if (clicked) {
            state.SelectedTile = tile.Id;
            if (state.Tool == TilemapTool::Eraser || state.Tool == TilemapTool::Eyedropper ||
                state.Tool == TilemapTool::Select) {
                state.Tool = TilemapTool::Brush;
            }
            state.Enabled = true;
        }
        ImGui::SetItemTooltip("Tile %u (%d, %d)  %s%s", tile.Id, tile.Region.X, tile.Region.Y,
                              Xora2D::ToString(tile.Collision), tile.Name.empty() ? "" : (" - " + tile.Name).c_str());
        if (++column < perRow) {
            ImGui::SameLine();
        } else {
            column = 0;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

} // namespace Xora2DEditor
