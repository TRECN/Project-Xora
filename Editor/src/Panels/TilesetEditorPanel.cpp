#include "Xora2DEditor/Panels/TilesetEditorPanel.h"

#include "Xora2DEditor/Content/ContentBrowserPaths.h"
#include "Xora2DEditor/EditorContext.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Renderer/Texture2D.h"
#include "Xora2D/Tilemap/TilemapComponent.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace Xora2DEditor {

namespace {

const ImVec4 kError(0.95f, 0.45f, 0.40f, 1.0f);
const ImVec4 kOk(0.45f, 0.85f, 0.50f, 1.0f);

template <std::size_t N>
void Copy(std::array<char, N>& buffer, const std::string& text) {
    std::snprintf(buffer.data(), buffer.size(), "%s", text.c_str());
}

} // namespace

void TilesetEditorPanel::LoadExisting(EditorContext& context, Xora2D::AssetHandle handle) {
    Xora2D::AssetManager& assets = context.GetAssetManager();
    assets.UnloadAsset(handle); // read the file as it is now
    if (!assets.LoadTileset(handle).Success) {
        m_status = "Cannot load tileset " + handle.ToString();
        m_statusIsError = true;
        return;
    }
    const auto tileset = assets.GetLoadedTileset(handle);
    m_handle = handle;
    Copy(m_name, tileset->GetName());
    m_settings = tileset->GetSettings();
    m_properties.clear();
    for (const Xora2D::TileProperties& property : tileset->GetProperties()) {
        m_properties[property.Id] = property;
    }
    m_selectedTile = 0;
    m_active = true;
    m_status.clear();
    Rebuild();
}

void TilesetEditorPanel::StartNew(EditorContext& context, Xora2D::AssetHandle texture) {
    m_handle = {};
    m_settings = Xora2D::TilesetSettings{};
    m_properties.clear();
    m_selectedTile = 0;
    Copy(m_folder, "Tilesets");
    SetTexture(context, texture);
    const Xora2D::AssetMetadata* meta = context.GetAssetRegistry().FindByHandle(texture);
    Copy(m_name, meta != nullptr ? meta->Name : std::string("NewTileset"));
    // Sensible default: 16 px tiles, or the whole image if it is smaller.
    m_settings.TileWidth = std::min(16, std::max(1, m_settings.TextureWidth));
    m_settings.TileHeight = std::min(16, std::max(1, m_settings.TextureHeight));
    m_active = true;
    m_status.clear();
    Rebuild();
}

void TilesetEditorPanel::SetTexture(EditorContext& context, Xora2D::AssetHandle texture) {
    m_settings.Texture = texture;
    m_settings.TextureWidth = 0;
    m_settings.TextureHeight = 0;
    if (const Xora2D::AssetMetadata* meta = context.GetAssetRegistry().FindByHandle(texture)) {
        std::int32_t width = 0;
        std::int32_t height = 0;
        if (Xora2D::ReadImageSize(Xora2D::ResolveAssetPath(meta->RelativePath), width, height)) {
            m_settings.TextureWidth = width;
            m_settings.TextureHeight = height;
        }
    }
}

void TilesetEditorPanel::Rebuild() {
    std::vector<Xora2D::TileProperties> properties;
    for (const auto& [id, property] : m_properties) {
        (void)id;
        properties.push_back(property);
    }
    const Xora2D::TilesetBuildResult build = m_preview.Build(m_name[0] ? m_name.data() : "Tileset", m_settings, properties);
    m_previewError = build.Success ? std::string{} : build.Message;
}

void TilesetEditorPanel::OnImGui(EditorContext& context) {
    if (const auto request = context.ConsumeTilesetEditorRequest()) {
        SetVisible(true);
        ImGui::SetWindowFocus(GetTitle());
        if (request->Tileset.IsValid()) {
            LoadExisting(context, request->Tileset);
        } else {
            StartNew(context, request->Texture);
        }
    }
    bool visible = IsVisible();
    ImGui::SetNextWindowSize(ImVec2(520.0f, 640.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(GetTitle(), &visible)) {
        ImGui::End();
        SetVisible(visible);
        return;
    }
    SetVisible(visible);
    if (!m_active) {
        ImGui::TextWrapped("Right-click a texture in the Content panel and choose Create Tileset, or double-click a "
                           ".tileset.yaml asset to edit it.");
        ImGui::End();
        return;
    }

    bool changed = false;
    ImGui::TextDisabled("%s", m_handle.IsValid() ? "Editing tileset" : "New tileset");
    changed |= ImGui::InputText("Name", m_name.data(), m_name.size());
    if (!m_handle.IsValid()) {
        ImGui::InputText("Folder", m_folder.data(), m_folder.size());
        ImGui::SetItemTooltip("Folder inside Assets/");
    }

    // Texture.
    const Xora2D::AssetMetadata* textureMeta = context.GetAssetRegistry().FindByHandle(m_settings.Texture);
    if (ImGui::BeginCombo("Texture", textureMeta != nullptr ? textureMeta->Name.c_str() : "(missing)")) {
        for (const Xora2D::AssetMetadata& meta : context.GetAssetRegistry().ListAssets()) {
            if (meta.Type == Xora2D::AssetType::Texture2D &&
                ImGui::Selectable((meta.Name + "##" + meta.Handle.ToString()).c_str(), meta.Handle == m_settings.Texture)) {
                SetTexture(context, meta.Handle);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("Image: %d x %d px", m_settings.TextureWidth, m_settings.TextureHeight);

    int mode = static_cast<int>(m_settings.Mode);
    changed |= ImGui::RadioButton("Grid", &mode, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Manual regions", &mode, 1);
    m_settings.Mode = static_cast<Xora2D::TilesetMode>(mode);
    int tileSize[2] = {m_settings.TileWidth, m_settings.TileHeight};
    if (ImGui::InputInt2("Tile size (px)", tileSize)) {
        m_settings.TileWidth = tileSize[0];
        m_settings.TileHeight = tileSize[1];
        changed = true;
    }
    if (m_settings.Mode == Xora2D::TilesetMode::Grid) {
        int spacing[2] = {m_settings.Margin, m_settings.Spacing};
        if (ImGui::InputInt2("Margin / spacing", spacing)) {
            m_settings.Margin = spacing[0];
            m_settings.Spacing = spacing[1];
            changed = true;
        }
        ImGui::SetItemTooltip("Margin: pixels around the sheet. Spacing: pixels between neighbouring tiles.");
    } else {
        ImGui::InputInt4("New region x, y, w, h", m_newRegion.data());
        if (ImGui::Button("Add region")) {
            Xora2D::TileId next = 1;
            if (!m_properties.empty()) {
                next = m_properties.rbegin()->first + 1;
            }
            Xora2D::TileProperties property{};
            property.Id = next;
            property.Region = {m_newRegion[0], m_newRegion[1], m_newRegion[2], m_newRegion[3]};
            m_properties[next] = property;
            m_selectedTile = next;
            changed = true;
        }
    }
    if (changed) {
        Rebuild();
    }

    if (!m_previewError.empty()) {
        ImGui::TextColored(kError, "%s", m_previewError.c_str());
    } else {
        ImGui::TextColored(kOk, "%zu tiles (%d x %d grid)", m_preview.GetTiles().size(), m_preview.GetColumns(),
                           m_preview.GetRows());
    }

    ImGui::BeginDisabled(!m_previewError.empty() || m_name[0] == '\0');
    if (ImGui::Button(m_handle.IsValid() ? "Save Tileset" : "Create Tileset")) {
        Save(context);
    }
    ImGui::EndDisabled();
    Xora2D::Entity selectedEntity = context.GetSelectedEntity();
    if (m_handle.IsValid() && selectedEntity.IsValid() && selectedEntity.HasComponent<Xora2D::TilemapComponent>()) {
        ImGui::SameLine();
        if (ImGui::Button("Use on Selected Tilemap")) {
            auto& tilemap = selectedEntity.GetComponent<Xora2D::TilemapComponent>();
            tilemap.Tileset = m_handle;
            tilemap.TileSize = {m_settings.TileWidth, m_settings.TileHeight};
            tilemap.MarkCollisionDirty();
            m_status = "Assigned to '" + selectedEntity.GetName() + "'";
            m_statusIsError = false;
        }
    }
    if (!m_status.empty()) {
        ImGui::TextColored(m_statusIsError ? kError : kOk, "%s", m_status.c_str());
    }
    ImGui::Separator();
    DrawSelectedTile();
    DrawPreview(context);
    ImGui::End();
}

void TilesetEditorPanel::DrawSelectedTile() {
    const Xora2D::TileDefinition* tile = m_previewError.empty() ? m_preview.FindTile(m_selectedTile) : nullptr;
    if (tile == nullptr) {
        ImGui::TextDisabled("Click a tile in the preview to edit its collision and name.");
        return;
    }
    Xora2D::TileProperties& property = m_properties[m_selectedTile];
    property.Id = m_selectedTile;
    if (m_settings.Mode == Xora2D::TilesetMode::Manual && property.Region.Width == 0) {
        property.Region = tile->Region;
    }
    bool changed = false;
    ImGui::Text("Tile %u", tile->Id);
    ImGui::SameLine();
    ImGui::TextDisabled("region (%d, %d) %d x %d", tile->Region.X, tile->Region.Y, tile->Region.Width, tile->Region.Height);
    int collision = static_cast<int>(property.Collision);
    const char* kinds[] = {"None (decoration)", "Solid", "One-way platform"};
    if (ImGui::Combo("Collision", &collision, kinds, 3)) {
        property.Collision = static_cast<Xora2D::TileCollision>(collision);
        changed = true;
    }
    std::array<char, 64> name{};
    Copy(name, property.Name);
    if (ImGui::InputText("Tile name", name.data(), name.size())) {
        property.Name = name.data();
        changed = true;
    }
    changed |= ImGui::ColorEdit4("Tint", &property.Tint.x, ImGuiColorEditFlags_NoInputs);
    if (m_settings.Mode == Xora2D::TilesetMode::Manual) {
        int region[4] = {property.Region.X, property.Region.Y, property.Region.Width, property.Region.Height};
        if (ImGui::InputInt4("Region", region)) {
            property.Region = {region[0], region[1], region[2], region[3]};
            changed = true;
        }
        if (ImGui::Button("Remove tile")) {
            m_properties.erase(m_selectedTile);
            m_selectedTile = 0;
            changed = true;
        }
    }
    if (changed) {
        Rebuild();
    }
}

void TilesetEditorPanel::DrawPreview(EditorContext& context) {
    Xora2D::AssetManager& assets = context.GetAssetManager();
    std::uint32_t textureId = 0;
    if (m_settings.Texture.IsValid() && assets.LoadTexture(m_settings.Texture).Success) {
        if (const auto texture = assets.GetLoadedTexture(m_settings.Texture)) {
            textureId = texture->GetRendererId();
        }
    }
    if (textureId == 0 || m_settings.TextureWidth <= 0) {
        ImGui::TextColored(kError, "Texture missing or unreadable: the tileset cannot be previewed.");
        return;
    }
    ImGui::BeginChild("##TilesetPreview", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    const float available = std::max(ImGui::GetContentRegionAvail().x - 8.0f, 64.0f);
    const float scale = std::clamp(available / static_cast<float>(m_settings.TextureWidth), 0.25f, 8.0f);
    const ImVec2 size(static_cast<float>(m_settings.TextureWidth) * scale, static_cast<float>(m_settings.TextureHeight) * scale);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    // Bottom-up texture: uv0 (top-left) = (0, 1).
    ImGui::Image(static_cast<ImTextureID>(static_cast<std::uintptr_t>(textureId)), size, ImVec2(0.0f, 1.0f),
                 ImVec2(1.0f, 0.0f));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (m_previewError.empty()) {
        for (const Xora2D::TileDefinition& tile : m_preview.GetTiles()) {
            const ImVec2 min(origin.x + static_cast<float>(tile.Region.X) * scale,
                             origin.y + static_cast<float>(tile.Region.Y) * scale);
            const ImVec2 max(min.x + static_cast<float>(tile.Region.Width) * scale,
                             min.y + static_cast<float>(tile.Region.Height) * scale);
            if (tile.Collision == Xora2D::TileCollision::Solid) {
                draw->AddRectFilled(min, max, IM_COL32(230, 70, 60, 70));
            } else if (tile.Collision == Xora2D::TileCollision::OneWay) {
                draw->AddRectFilled(min, ImVec2(max.x, min.y + (max.y - min.y) * 0.25f), IM_COL32(240, 200, 60, 110));
            }
            const bool selected = tile.Id == m_selectedTile;
            draw->AddRect(min, max, selected ? IM_COL32(90, 200, 255, 255) : IM_COL32(255, 255, 255, 70), 0.0f, 0,
                          selected ? 2.0f : 1.0f);
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                ImGui::IsMouseHoveringRect(min, max, false)) {
                m_selectedTile = tile.Id;
            }
            if (hovered && ImGui::IsMouseHoveringRect(min, max, false)) {
                ImGui::SetTooltip("Tile %u  %s", tile.Id, Xora2D::ToString(tile.Collision));
            }
        }
    }
    ImGui::EndChild();
}

void TilesetEditorPanel::Save(EditorContext& context) {
    Rebuild();
    if (!m_previewError.empty()) {
        return;
    }
    Xora2D::AssetManager& assets = context.GetAssetManager();
    std::string relative;
    bool overwrite = false;
    if (m_handle.IsValid()) {
        const Xora2D::AssetMetadata* meta = context.GetAssetRegistry().FindByHandle(m_handle);
        if (meta == nullptr) {
            m_status = "The tileset asset is no longer registered";
            m_statusIsError = true;
            return;
        }
        relative = meta->RelativePath;
        overwrite = true;
    } else {
        const std::string folder = m_folder.data();
        relative = (folder.empty() ? std::string() : folder + "/") + m_name.data() + ".tileset.yaml";
        if (!IsValidEntryName(m_name.data()) || !Xora2D::NormalizeAssetRelativePath(relative).has_value()) {
            m_status = "Invalid tileset name or folder";
            m_statusIsError = true;
            return;
        }
    }
    const Xora2D::TilesetBuildResult saved =
        Xora2D::TilesetSerializer::Save(m_preview, Xora2D::ResolveAssetPath(relative), overwrite);
    if (!saved.Success) {
        m_status = saved.Message;
        m_statusIsError = true;
        return;
    }
    if (!m_handle.IsValid()) {
        const Xora2D::AssetResult registered = assets.RegisterTilesetAtRelativePath(relative, m_name.data());
        if (!registered.Success) {
            m_status = registered.Message;
            m_statusIsError = true;
            return;
        }
        m_handle = registered.Handle;
        (void)context.PersistAssetRegistry();
    }
    (void)assets.ReloadAsset(m_handle); // tilemaps using it rebuild their geometry on the next frame
    context.NotifyContentChanged(ResourcePathFromAssetRelativePath(ParentResourceDirectory(relative)));
    m_status = "Saved Assets/" + relative + " (" + std::to_string(m_preview.GetTiles().size()) + " tiles)";
    m_statusIsError = false;
}

} // namespace Xora2DEditor
