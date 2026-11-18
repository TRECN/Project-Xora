#include "Xora2DEditor/Tilemap/TilemapEditorState.h"

#include "Xora2DEditor/EditorContext.h"

#include "Xora2D/Core/Logger.h"
#include "Xora2D/Prefab/PrefabComponents.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Tilemap/TilemapComponent.h"

#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Xora2DEditor {

namespace {

Xora2D::GridCoord WorldToCell(const Xora2D::Entity& entity, const glm::vec2& world) {
    const auto& tilemap = entity.GetComponent<Xora2D::TilemapComponent>();
    const glm::mat4 inverse = glm::inverse(entity.GetComponent<Xora2D::TransformComponent>().ComputeMatrix());
    const glm::vec4 local = inverse * glm::vec4(world, 0.0f, 1.0f);
    return tilemap.LocalToCell({local.x, local.y});
}

} // namespace

const char* ToString(TilemapTool tool) {
    switch (tool) {
    case TilemapTool::Brush:
        return "Brush";
    case TilemapTool::Eraser:
        return "Eraser";
    case TilemapTool::Rectangle:
        return "Rectangle";
    case TilemapTool::Bucket:
        return "Bucket";
    case TilemapTool::Eyedropper:
        return "Eyedropper";
    case TilemapTool::Select:
        return "Select";
    }
    return "?";
}

Xora2D::Entity TilemapEditorState::GetTarget(EditorContext& context) const {
    if (context.IsPlayModeActive()) {
        return {};
    }
    const Xora2D::Entity entity = context.GetSelectedEntity();
    return entity.IsValid() && entity.HasComponent<Xora2D::TilemapComponent>() ? entity : Xora2D::Entity{};
}

std::optional<Xora2D::OrthographicCamera> TilemapEditorState::GetViewCamera(EditorContext& context, float width,
                                                                           float height) const {
    if (!IsPainting(context)) {
        return std::nullopt;
    }
    Xora2D::OrthographicCamera camera;
    camera.SetViewportSize(width, height);
    camera.SetZoom(CameraZoom);
    camera.SetPosition(CameraPosition);
    return camera;
}

void TilemapEditorState::FrameTarget(EditorContext& context, float viewWidth, float viewHeight) {
    const Xora2D::Entity target = GetTarget(context);
    if (!target.IsValid()) {
        return;
    }
    const auto& tilemap = target.GetComponent<Xora2D::TilemapComponent>();
    const glm::mat4 model = target.GetComponent<Xora2D::TransformComponent>().ComputeMatrix();
    glm::vec2 minimum{0.0f};
    glm::vec2 maximum{glm::vec2(tilemap.TileSize) * 20.0f};
    if (const auto bounds = tilemap.GetBounds(-1)) {
        minimum = tilemap.CellToLocal(bounds->first);
        maximum = tilemap.CellToLocal({bounds->second.X + 1, bounds->second.Y + 1});
    }
    const glm::vec2 worldMin = glm::vec2(model * glm::vec4(minimum, 0.0f, 1.0f));
    const glm::vec2 worldMax = glm::vec2(model * glm::vec4(maximum, 0.0f, 1.0f));
    const glm::vec2 size = glm::max(glm::abs(worldMax - worldMin), glm::vec2(1.0f)) * 1.15f;
    CameraZoom = std::clamp(std::min(viewWidth / size.x, viewHeight / size.y), 0.05f, 8.0f);
    CameraPosition = (worldMin + worldMax) * 0.5f - glm::vec2(viewWidth, viewHeight) * (0.5f / CameraZoom);
}

void TilemapEditorState::Pan(const glm::vec2& screenDelta) {
    // Dragging right moves the world right (camera left); screen Y is down, world Y is up.
    CameraPosition -= glm::vec2(screenDelta.x, -screenDelta.y) / CameraZoom;
}

void TilemapEditorState::ZoomAt(float factor, const glm::vec2& viewPixel) {
    const glm::vec2 worldBefore = CameraPosition + viewPixel / CameraZoom;
    CameraZoom = std::clamp(CameraZoom * factor, 0.05f, 16.0f);
    CameraPosition = worldBefore - viewPixel / CameraZoom;
}

Xora2D::TilemapEditHistory* TilemapEditorState::History(EditorContext& context) {
    const Xora2D::Entity target = GetTarget(context);
    if (!target.IsValid()) {
        return nullptr;
    }
    const Xora2D::Scene* scene = context.GetScene().get();
    if (scene != m_historyScene) {
        // Another scene (opened scene, prefab editing, project switch): old commands would refer to other data.
        m_histories.clear();
        m_historyScene = scene;
    }
    return &m_histories[target.GetUUID()];
}

void TilemapEditorState::MarkPrefabOverride(Xora2D::Entity entity) {
    if (!entity.HasComponent<Xora2D::PrefabInstanceComponent>()) {
        return;
    }
    auto& overrides = entity.GetComponent<Xora2D::PrefabInstanceComponent>().Overrides;
    if (std::find(overrides.begin(), overrides.end(), "Tilemap") == overrides.end()) {
        overrides.push_back("Tilemap");
        std::sort(overrides.begin(), overrides.end());
    }
}

void TilemapEditorState::Record(EditorContext& context, Xora2D::TilemapCommand command) {
    if (command.IsEmpty()) {
        return;
    }
    if (Xora2D::TilemapEditHistory* history = History(context)) {
        MarkPrefabOverride(GetTarget(context));
        history->Record(std::move(command));
    }
}

void TilemapEditorState::Apply(EditorContext& context, Xora2D::TilemapEditResult result) {
    Report(result.Success, result.Message);
    if (result.Success) {
        Record(context, std::move(result.Command));
    }
}

bool TilemapEditorState::Undo(EditorContext& context) {
    FinishStroke(context);
    Xora2D::TilemapEditHistory* history = History(context);
    if (history == nullptr || !history->CanUndo()) {
        return false;
    }
    const std::string label = history->UndoLabel();
    const bool done = history->Undo(GetTarget(context).GetComponent<Xora2D::TilemapComponent>());
    Report(done, "Undo " + label);
    return done;
}

bool TilemapEditorState::Redo(EditorContext& context) {
    FinishStroke(context);
    Xora2D::TilemapEditHistory* history = History(context);
    if (history == nullptr || !history->CanRedo()) {
        return false;
    }
    const std::string label = history->RedoLabel();
    const bool done = history->Redo(GetTarget(context).GetComponent<Xora2D::TilemapComponent>());
    Report(done, "Redo " + label);
    return done;
}

bool TilemapEditorState::CanUndo(EditorContext& context) {
    Xora2D::TilemapEditHistory* history = History(context);
    return history != nullptr && history->CanUndo();
}

bool TilemapEditorState::CanRedo(EditorContext& context) {
    Xora2D::TilemapEditHistory* history = History(context);
    return history != nullptr && history->CanRedo();
}

void TilemapEditorState::FinishStroke(EditorContext& context) {
    if (!m_stroke.has_value()) {
        return;
    }
    Xora2D::TilemapCommand command = m_stroke->Finish();
    m_stroke.reset();
    m_lastStrokeCell.reset();
    const Xora2D::Entity target = GetTarget(context);
    if (target.IsValid() && target.GetUUID() == m_strokeEntity && !command.IsEmpty()) {
        const std::size_t count = command.Changes.size();
        Record(context, std::move(command));
        Report(true, "Painted " + std::to_string(count) + " tile(s)");
    }
}

std::optional<Xora2D::TileRect> TilemapEditorState::GetDragRect() const {
    if (!m_dragStart.has_value() || !m_hovered.has_value()) {
        return std::nullopt;
    }
    return Xora2D::TileRect::FromCorners(*m_dragStart, *m_hovered);
}

void TilemapEditorState::HandleViewportInput(EditorContext& context, const TilemapViewportInput& input) {
    Xora2D::Entity target = GetTarget(context);
    if (!target.IsValid()) {
        FinishStroke(context);
        m_dragStart.reset();
        m_hovered.reset();
        return;
    }
    auto& tilemap = target.GetComponent<Xora2D::TilemapComponent>();
    const std::int32_t layer = tilemap.ActiveLayer;
    if (input.Hovered || m_stroke.has_value() || m_dragStart.has_value()) {
        m_hovered = WorldToCell(target, input.World);
    } else {
        m_hovered.reset();
    }
    if (!m_hovered.has_value()) {
        return;
    }
    const Xora2D::GridCoord cell = *m_hovered;

    if (input.LeftPressed && input.Hovered) {
        if (m_pasteMode) {
            Apply(context, Xora2D::PasteTiles(tilemap, layer, cell, m_clipboard));
            m_pasteMode = false;
            return;
        }
        switch (Tool) {
        case TilemapTool::Brush:
        case TilemapTool::Eraser:
            if (std::string reason = Xora2D::CheckLayerEditable(tilemap, layer); !reason.empty()) {
                Report(false, reason);
                return;
            }
            m_stroke.emplace(tilemap, layer, Tool == TilemapTool::Brush ? "Brush" : "Erase");
            m_strokeEntity = target.GetUUID();
            m_lastStrokeCell.reset();
            break;
        case TilemapTool::Rectangle:
        case TilemapTool::Select:
            m_dragStart = cell;
            break;
        case TilemapTool::Bucket:
            Apply(context, Xora2D::BucketFill(tilemap, layer, cell, input.Shift ? Xora2D::TileCell{} : BrushCell()));
            return;
        case TilemapTool::Eyedropper: {
            std::int32_t found = -1;
            const Xora2D::TileCell picked = Xora2D::PickTile(tilemap, -1, cell, &found);
            if (picked.IsEmpty()) {
                Report(false, "No tile at (" + std::to_string(cell.X) + ", " + std::to_string(cell.Y) + ")");
            } else {
                SelectedTile = picked.Id;
                FlipX = picked.FlipX();
                FlipY = picked.FlipY();
                Rotation = picked.Rotation();
                tilemap.ActiveLayer = found;
                Tool = TilemapTool::Brush;
                Report(true, "Picked tile " + std::to_string(picked.Id) + " from layer '" +
                                 tilemap.Layers[static_cast<std::size_t>(found)].Name + "'");
            }
            return;
        }
        }
    }

    if (m_stroke.has_value()) {
        if (input.LeftDown) {
            const Xora2D::TileCell value = Tool == TilemapTool::Eraser ? Xora2D::TileCell{} : BrushCell();
            // Fill the gap between two mouse samples (fast drags skip cells otherwise).
            const Xora2D::GridCoord from = m_lastStrokeCell.value_or(cell);
            const int steps = std::max(std::abs(cell.X - from.X), std::abs(cell.Y - from.Y));
            for (int step = 0; step <= steps; ++step) {
                const float t = steps == 0 ? 1.0f : static_cast<float>(step) / static_cast<float>(steps);
                const Xora2D::GridCoord sample{
                    from.X + static_cast<std::int32_t>(std::lround(static_cast<float>(cell.X - from.X) * t)),
                    from.Y + static_cast<std::int32_t>(std::lround(static_cast<float>(cell.Y - from.Y) * t))};
                (void)m_stroke->Paint(sample, value);
            }
            m_lastStrokeCell = cell;
        }
        if (input.LeftReleased || !input.LeftDown) {
            FinishStroke(context);
        }
        return;
    }

    if (m_dragStart.has_value() && (input.LeftReleased || !input.LeftDown)) {
        const Xora2D::TileRect rect = Xora2D::TileRect::FromCorners(*m_dragStart, cell);
        m_dragStart.reset();
        if (Tool == TilemapTool::Rectangle) {
            Apply(context, Xora2D::FillRect(tilemap, layer, rect, input.Shift ? Xora2D::TileCell{} : BrushCell()));
        } else {
            m_selection = rect;
            Report(true, "Selected " + std::to_string(rect.Width()) + " x " + std::to_string(rect.Height()) + " cells");
        }
    }
}

void TilemapEditorState::CopySelection(EditorContext& context) {
    const Xora2D::Entity target = GetTarget(context);
    if (!target.IsValid() || !m_selection.has_value()) {
        Report(false, "Select a region first (Select tool)");
        return;
    }
    const auto& tilemap = target.GetComponent<Xora2D::TilemapComponent>();
    m_clipboard = Xora2D::CopyTiles(tilemap, tilemap.ActiveLayer, *m_selection);
    Report(true, "Copied " + std::to_string(m_clipboard.Width) + " x " + std::to_string(m_clipboard.Height) + " cells");
}

void TilemapEditorState::DeleteSelection(EditorContext& context) {
    Xora2D::Entity target = GetTarget(context);
    if (!target.IsValid() || !m_selection.has_value()) {
        return;
    }
    auto& tilemap = target.GetComponent<Xora2D::TilemapComponent>();
    Apply(context, Xora2D::FillRect(tilemap, tilemap.ActiveLayer, *m_selection, Xora2D::TileCell{}));
}

void TilemapEditorState::CutSelection(EditorContext& context) {
    CopySelection(context);
    if (!m_clipboard.IsEmpty()) {
        DeleteSelection(context);
    }
}

void TilemapEditorState::BeginPaste() {
    if (m_clipboard.IsEmpty()) {
        Report(false, "The tile clipboard is empty");
        return;
    }
    m_pasteMode = true;
    Report(true, "Click in the viewport to paste");
}

void TilemapEditorState::NudgeSelection(EditorContext& context, const Xora2D::GridCoord& offset) {
    Xora2D::Entity target = GetTarget(context);
    if (!target.IsValid() || !m_selection.has_value()) {
        return;
    }
    auto& tilemap = target.GetComponent<Xora2D::TilemapComponent>();
    Xora2D::TilemapEditResult result = Xora2D::MoveTiles(tilemap, tilemap.ActiveLayer, *m_selection, offset);
    if (result.Success) {
        m_selection = Xora2D::TileRect{{m_selection->Min.X + offset.X, m_selection->Min.Y + offset.Y},
                                       {m_selection->Max.X + offset.X, m_selection->Max.Y + offset.Y}};
    }
    Apply(context, std::move(result));
}

void TilemapEditorState::Report(bool success, std::string message) {
    m_messageIsError = !success;
    m_message = std::move(message);
}

const std::string& TilemapEditorState::GetMessage(bool* isError) const {
    if (isError != nullptr) {
        *isError = m_messageIsError;
    }
    return m_message;
}

void TilemapEditorState::Reset() {
    m_stroke.reset();
    m_histories.clear();
    m_historyScene = nullptr;
    m_dragStart.reset();
    m_hovered.reset();
    m_selection.reset();
    m_pasteMode = false;
}

} // namespace Xora2DEditor
