#include "Xora2DEditor/Panels/ViewportPanel.h"

#include "Xora2DEditor/EditorApplication.h"
#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/PrefabEditorUI.h"

#include "Xora2D/Scene/Components.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Renderer/Texture2D.h"
#include "Xora2D/Tilemap/TilemapComponent.h"
#include "Xora2D/Tilemap/Tileset.h"

#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include "Xora2D/Scene/Entity.h"

#include "Xora2D/Renderer/Renderer2D.h"
#include "Xora2D/Renderer/RendererAPI.h"
#include "Xora2D/Scene/Scene.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Xora2DEditor {

void ViewportPanel::OnImGui(EditorContext& context) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const EditorPlayState playState = context.GetPlayState();
    if (m_lastPlayState == EditorPlayState::Edit && playState == EditorPlayState::Playing) {
        // Entering Play: give the game view keyboard focus so gameplay input works without an extra click.
        ImGui::SetNextWindowFocus();
    }
    m_lastPlayState = playState;

    if (ImGui::Begin(GetTitle(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const int width = std::max(1, static_cast<int>(std::floor(available.x)));
        const int height = std::max(1, static_cast<int>(std::floor(available.y)));
        m_lastWidth = width;
        m_lastHeight = height;

        TilemapEditorState& tilemapEditor = context.GetTilemapEditor();
        const bool painting = playState == EditorPlayState::Edit && tilemapEditor.IsPainting(context);
        if (painting) {
            const auto& cell = tilemapEditor.GetHoveredCell();
            const Xora2D::Entity target = tilemapEditor.GetTarget(context);
            const auto& tilemap = target.GetComponent<Xora2D::TilemapComponent>();
            const std::string layer = tilemap.IsValidLayer(tilemap.ActiveLayer)
                                          ? tilemap.Layers[static_cast<std::size_t>(tilemap.ActiveLayer)].Name
                                          : std::string("-");
            ImGui::TextColored(ImVec4(0.40f, 0.78f, 1.00f, 1.0f), "TILE PAINT");
            ImGui::SameLine();
            if (cell.has_value()) {
                ImGui::TextDisabled("%s | layer %s | cell (%d, %d) | zoom %.2f | wheel zoom, right-drag pan",
                                    ToString(tilemapEditor.Tool), layer.c_str(), cell->X, cell->Y,
                                    tilemapEditor.CameraZoom);
            } else {
                ImGui::TextDisabled("%s | layer %s | zoom %.2f | wheel zoom, right-drag pan", ToString(tilemapEditor.Tool),
                                    layer.c_str(), tilemapEditor.CameraZoom);
            }
        } else if (playState == EditorPlayState::Edit) {
            ImGui::TextDisabled("Viewport %dx%d  |  Edit Mode", width, height);
        } else {
            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            const ImVec4 color = playState == EditorPlayState::Playing ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f)
                                                                      : ImVec4(0.95f, 0.75f, 0.25f, 1.0f);
            ImGui::TextColored(color, "%s", playState == EditorPlayState::Playing ? "PLAYING" : "PAUSED");
            ImGui::SameLine();
            ImGui::TextDisabled("Viewport %dx%d  |  %s", width, height,
                                focused ? "game input active" : "click the viewport to send input to the game");
        }
        ImGui::Separator();

        const ImVec2 imageSize(static_cast<float>(width), static_cast<float>(height - 4.0f));
        if (m_renderTarget != nullptr && m_renderTarget->IsValid()) {
            const Xora2D::Texture2D& texture = m_renderTarget->GetColorTexture();
            const ImTextureID textureId =
                static_cast<ImTextureID>(static_cast<std::uintptr_t>(texture.GetRendererId()));
            ImGui::Image(textureId, imageSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            const bool imageHovered = ImGui::IsItemHovered();
            // Drop a prefab from the Content panel onto the game view: the instance lands under the cursor.
            if (!context.IsEditingPrefab() && ImGui::BeginDragDropTarget()) {
                const ImVec2 imageMin = ImGui::GetItemRectMin();
                const ImVec2 mouse = ImGui::GetMousePos();
                if (const auto prefab = AcceptPrefabDragDrop(context)) {
                    const glm::vec2 local{mouse.x - imageMin.x, imageSize.y - (mouse.y - imageMin.y)};
                    const auto result = context.InstantiatePrefab(*prefab, ViewToWorld(context, local));
                    ReportPrefabResult(result.Success, result.Message);
                }
                ImGui::EndDragDropTarget();
            }
            if (playState != EditorPlayState::Edit) {
                const ImU32 border = playState == EditorPlayState::Playing ? IM_COL32(90, 215, 115, 255)
                                                                          : IM_COL32(242, 190, 64, 255);
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), border, 0.0f, 0,
                                                    2.0f);
            }
            context.SetViewportInputState(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), imageHovered);
            if (painting) {
                DrawTilemapTools(context, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            } else {
                m_tilemapTarget = {};
            }
        } else {
            ImGui::Dummy(imageSize);
            ImGui::SetCursorScreenPos(ImGui::GetItemRectMin());
            ImGui::TextUnformatted("No viewport target");
        }

        if (auto scene = context.GetScene()) {
            ImGui::TextDisabled("Scene: %s (%zu entities)", scene->GetSceneName().c_str(), scene->GetEntityCount());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// Tile painting: an invisible button over the image captures the mouse (so drags never move / undock the window),
// the editor camera zooms and pans, and the overlay shows the grid, the hovered cell, drags and the selection.
void ViewportPanel::DrawTilemapTools(EditorContext& context, const ImVec2& imageMin, const ImVec2& imageMax) {
    TilemapEditorState& state = context.GetTilemapEditor();
    Xora2D::Entity target = state.GetTarget(context);
    const ImVec2 size(imageMax.x - imageMin.x, imageMax.y - imageMin.y);
    if (target.GetUUID() != m_tilemapTarget) {
        m_tilemapTarget = target.GetUUID();
        state.FrameTarget(context, size.x, size.y);
    }

    ImGui::SetCursorScreenPos(imageMin);
    ImGui::InvisibleButton("##TileCanvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    // Prefabs can still be dropped while painting.
    if (ImGui::BeginDragDropTarget()) {
        if (const auto prefab = AcceptPrefabDragDrop(context)) {
            const ImVec2 mouse = ImGui::GetMousePos();
            const auto result = context.InstantiatePrefab(
                *prefab, ViewToWorld(context, {mouse.x - imageMin.x, imageMax.y - mouse.y}));
            ReportPrefabResult(result.Success, result.Message);
        }
        ImGui::EndDragDropTarget();
    }
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const glm::vec2 viewPixel{mouse.x - imageMin.x, imageMax.y - mouse.y};
    if (hovered && io.MouseWheel != 0.0f) {
        state.ZoomAt(io.MouseWheel > 0.0f ? 1.15f : 1.0f / 1.15f, viewPixel);
    }
    if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
        state.Pan({io.MouseDelta.x, io.MouseDelta.y});
    }
    TilemapViewportInput input{};
    input.Hovered = hovered;
    input.World = state.CameraPosition + viewPixel / state.CameraZoom;
    input.LeftPressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    input.LeftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left) && (active || hovered);
    input.LeftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    input.Shift = io.KeyShift;
    state.HandleViewportInput(context, input);

    // ---- Overlay -----------------------------------------------------------------------------------------------
    target = state.GetTarget(context);
    if (!target.IsValid()) {
        return;
    }
    const auto& tilemap = target.GetComponent<Xora2D::TilemapComponent>();
    const glm::mat4 model = target.GetComponent<Xora2D::TransformComponent>().ComputeMatrix();
    const auto toScreen = [&](const glm::vec2& local) {
        const glm::vec4 world = model * glm::vec4(local, 0.0f, 1.0f);
        const glm::vec2 view = (glm::vec2(world) - state.CameraPosition) * state.CameraZoom;
        return ImVec2(imageMin.x + view.x, imageMax.y - view.y);
    };
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(imageMin, imageMax, true);
    const glm::vec2 cellSize(tilemap.TileSize);
    const float cellScreen = std::min(cellSize.x, cellSize.y) * state.CameraZoom * glm::length(glm::vec2(model[0]));
    if (state.ShowGrid && cellScreen >= 4.0f) {
        // Only the cells inside the view are drawn (no full-map scan).
        const glm::mat4 inverse = glm::inverse(model);
        glm::vec2 lo{1e30f};
        glm::vec2 hi{-1e30f};
        for (const glm::vec2 corner : {glm::vec2{0.0f, 0.0f}, glm::vec2{size.x, 0.0f}, glm::vec2{0.0f, size.y},
                                       glm::vec2{size.x, size.y}}) {
            const glm::vec2 world = state.CameraPosition + corner / state.CameraZoom;
            const glm::vec2 local = glm::vec2(inverse * glm::vec4(world, 0.0f, 1.0f));
            lo = glm::min(lo, local);
            hi = glm::max(hi, local);
        }
        const Xora2D::GridCoord first = tilemap.LocalToCell(lo);
        const Xora2D::GridCoord last = tilemap.LocalToCell(hi);
        const ImU32 gridColor = IM_COL32(255, 255, 255, 38);
        for (std::int32_t x = first.X; x <= last.X + 1 && x - first.X < 2000; ++x) {
            draw->AddLine(toScreen({static_cast<float>(x) * cellSize.x, lo.y}), toScreen({static_cast<float>(x) * cellSize.x, hi.y}),
                          gridColor);
        }
        for (std::int32_t y = first.Y; y <= last.Y + 1 && y - first.Y < 2000; ++y) {
            draw->AddLine(toScreen({lo.x, static_cast<float>(y) * cellSize.y}), toScreen({hi.x, static_cast<float>(y) * cellSize.y}),
                          gridColor);
        }
        // Tilemap origin axes.
        draw->AddLine(toScreen({0.0f, lo.y}), toScreen({0.0f, hi.y}), IM_COL32(90, 200, 255, 90), 1.5f);
        draw->AddLine(toScreen({lo.x, 0.0f}), toScreen({hi.x, 0.0f}), IM_COL32(90, 200, 255, 90), 1.5f);
    }
    const auto cellRect = [&](const Xora2D::GridCoord& minCell, const Xora2D::GridCoord& maxCell, ImU32 color, float thickness) {
        const ImVec2 a = toScreen(tilemap.CellToLocal(minCell));
        const ImVec2 b = toScreen(tilemap.CellToLocal({maxCell.X + 1, maxCell.Y + 1}));
        draw->AddRect(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), color,
                      0.0f, 0, thickness);
    };
    if (const auto selection = state.GetSelection()) {
        cellRect(selection->Min, selection->Max, IM_COL32(255, 210, 80, 255), 2.0f);
    }
    if (const auto drag = state.GetDragRect()) {
        cellRect(drag->Min, drag->Max, IM_COL32(120, 230, 140, 255), 2.0f);
    }
    if (const auto& cell = state.GetHoveredCell(); cell.has_value() && hovered) {
        if (state.IsPasteMode()) {
            const auto& clipboard = state.GetClipboard();
            cellRect(*cell, {cell->X + clipboard.Width - 1, cell->Y + clipboard.Height - 1}, IM_COL32(255, 210, 80, 200), 2.0f);
        } else {
            // Brush preview: the selected tile, translucent, under the cursor.
            if (state.Tool == TilemapTool::Brush) {
                Xora2D::AssetManager& assets = context.GetAssetManager();
                const Xora2D::AssetHandle tilesetHandle = tilemap.GetLayerTileset(tilemap.ActiveLayer);
                if (assets.LoadTileset(tilesetHandle).Success) {
                    const auto tileset = assets.GetLoadedTileset(tilesetHandle);
                    const Xora2D::TileDefinition* tile = tileset->FindTile(state.SelectedTile);
                    const Xora2D::AssetHandle textureHandle = tileset->GetSettings().Texture;
                    if (tile != nullptr && assets.LoadTexture(textureHandle).Success) {
                        const ImVec2 a = toScreen(tilemap.CellToLocal(*cell));
                        const ImVec2 b = toScreen(tilemap.CellToLocal({cell->X + 1, cell->Y + 1}));
                        draw->AddImage(static_cast<ImTextureID>(static_cast<std::uintptr_t>(
                                           assets.GetLoadedTexture(textureHandle)->GetRendererId())),
                                       ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)),
                                       ImVec2(tile->RenderUV.x, tile->RenderUV.w), ImVec2(tile->RenderUV.z, tile->RenderUV.y), IM_COL32(255, 255, 255, 150));
                    }
                }
            }
            const ImU32 color = state.Tool == TilemapTool::Eraser ? IM_COL32(255, 90, 80, 255) : IM_COL32(90, 200, 255, 255);
            cellRect(*cell, *cell, color, 2.0f);
        }
    }
    draw->PopClipRect();
}

// Pixel position inside the game view (origin bottom-left) -> world position through the active camera: the tile
// editor camera while painting, otherwise the scene's primary camera (its position is the view's bottom-left).
glm::vec2 ViewportPanel::ViewToWorld(EditorContext& context, const glm::vec2& viewPixels) {
    if (context.GetPlayState() == EditorPlayState::Edit && context.GetTilemapEditor().IsPainting(context)) {
        const TilemapEditorState& state = context.GetTilemapEditor();
        return state.CameraPosition + viewPixels / state.CameraZoom;
    }
    const auto scene = context.GetScene();
    glm::vec2 cameraPosition{0.0f};
    float zoom = 1.0f;
    if (scene != nullptr) {
        for (const Xora2D::UUID& id : scene->GetEntityUUIDs()) {
            const Xora2D::Entity entity = scene->FindEntityByUUID(id);
            if (entity.HasComponent<Xora2D::CameraComponent>()) {
                const auto& camera = entity.GetComponent<Xora2D::CameraComponent>();
                if (camera.Enabled && camera.Primary) {
                    cameraPosition = entity.GetComponent<Xora2D::TransformComponent>().Translation;
                    zoom = std::max(camera.Zoom, 0.01f);
                    break;
                }
            }
        }
    }
    return cameraPosition + viewPixels / zoom;
}

void ViewportPanel::RenderScene(EditorApplication& application, EditorContext& context) {
    if (m_lastWidth <= 0 || m_lastHeight <= 20) {
        return;
    }

    constexpr int kViewportHeaderPx = 28;
    const int renderHeight = std::max(1, m_lastHeight - kViewportHeaderPx);
    if (m_renderTarget == nullptr) {
        m_renderTarget = Xora2D::ColorRenderTarget::Create();
    }
    (void)m_renderTarget->Resize(m_lastWidth, renderHeight);

    auto scene = context.GetScene();
    if (scene == nullptr || !m_renderTarget->IsValid()) {
        return;
    }

    application.RenderSceneIntoTarget(*m_renderTarget, m_clearColor);
}

int ViewportPanel::GetRenderWidth() const {
    return m_renderTarget != nullptr ? m_renderTarget->GetWidth() : 0;
}

int ViewportPanel::GetRenderHeight() const {
    return m_renderTarget != nullptr ? m_renderTarget->GetHeight() : 0;
}

bool ViewportPanel::HasValidTarget() const {
    return m_renderTarget != nullptr && m_renderTarget->IsValid();
}

} // namespace Xora2DEditor