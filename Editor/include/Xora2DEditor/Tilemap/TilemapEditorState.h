#pragma once

#include "Xora2D/Core/UUID.h"
#include "Xora2D/Renderer/OrthographicCamera.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Tilemap/TilemapEditing.h"

#include <glm/vec2.hpp>

#include <optional>
#include <string>
#include <unordered_map>

namespace Xora2D {
class Scene;
struct TilemapComponent;
} // namespace Xora2D

namespace Xora2DEditor {

class EditorContext;

enum class TilemapTool : std::uint8_t { Brush, Eraser, Rectangle, Bucket, Eyedropper, Select };
[[nodiscard]] const char* ToString(TilemapTool tool);

// Mouse state of the viewport for one frame, already converted to world coordinates.
struct TilemapViewportInput {
    bool Hovered = false;
    glm::vec2 World{0.0f};
    bool LeftPressed = false;
    bool LeftDown = false;
    bool LeftReleased = false;
    bool Shift = false;
};

// Tilemap editing session: tools, brush, selection / clipboard, per-tilemap undo history and the editor camera
// used by the viewport while painting. The tile operations themselves live in the engine (TilemapEditing).
class TilemapEditorState {
public:
    // Painting mode: the viewport routes the mouse to the tilemap tools and uses the editor camera.
    bool Enabled = false;
    TilemapTool Tool = TilemapTool::Brush;
    Xora2D::TileId SelectedTile = 1;
    bool FlipX = false;
    bool FlipY = false;
    int Rotation = 0;
    bool ShowGrid = true;

    // Editor camera (bottom-left position + zoom), independent of the scene camera.
    glm::vec2 CameraPosition{0.0f};
    float CameraZoom = 1.0f;

    // The selected entity's tilemap in the editable scene (or the prefab being edited). Invalid while playing.
    [[nodiscard]] Xora2D::Entity GetTarget(EditorContext& context) const;
    [[nodiscard]] bool IsPainting(EditorContext& context) const { return Enabled && GetTarget(context).IsValid(); }
    [[nodiscard]] Xora2D::TileCell BrushCell() const {
        return Xora2D::TileCell::Make(SelectedTile, FlipX, FlipY, Rotation);
    }

    // Camera used to render the viewport while painting (nullopt = the scene's own camera).
    [[nodiscard]] std::optional<Xora2D::OrthographicCamera> GetViewCamera(EditorContext& context, float width,
                                                                         float height) const;
    // Centers the editor camera on the target tilemap (or the scene camera when it is empty).
    void FrameTarget(EditorContext& context, float viewWidth, float viewHeight);
    void Pan(const glm::vec2& screenDelta);
    void ZoomAt(float factor, const glm::vec2& viewPixel);

    void HandleViewportInput(EditorContext& context, const TilemapViewportInput& input);

    // Cell under the mouse / current rectangle drag / selection (for the viewport overlay).
    [[nodiscard]] const std::optional<Xora2D::GridCoord>& GetHoveredCell() const { return m_hovered; }
    [[nodiscard]] std::optional<Xora2D::TileRect> GetDragRect() const;
    [[nodiscard]] const std::optional<Xora2D::TileRect>& GetSelection() const { return m_selection; }
    [[nodiscard]] const Xora2D::TileClipboard& GetClipboard() const { return m_clipboard; }
    [[nodiscard]] bool IsPasteMode() const { return m_pasteMode; }

    bool Undo(EditorContext& context);
    bool Redo(EditorContext& context);
    [[nodiscard]] bool CanUndo(EditorContext& context);
    [[nodiscard]] bool CanRedo(EditorContext& context);
    void Record(EditorContext& context, Xora2D::TilemapCommand command);

    void CopySelection(EditorContext& context);
    void CutSelection(EditorContext& context);
    void DeleteSelection(EditorContext& context);
    void BeginPaste();
    void NudgeSelection(EditorContext& context, const Xora2D::GridCoord& offset);
    void ClearSelection() { m_selection.reset(); }

    // Applies a layer/tile operation result: records it and reports the outcome.
    void Apply(EditorContext& context, Xora2D::TilemapEditResult result);
    void Report(bool success, std::string message);
    [[nodiscard]] const std::string& GetMessage(bool* isError = nullptr) const;

    // Called when the scene / project changes: drops histories and in-flight strokes.
    void Reset();

private:
    Xora2D::TilemapEditHistory* History(EditorContext& context);
    void FinishStroke(EditorContext& context);
    void MarkPrefabOverride(Xora2D::Entity entity);

    std::optional<Xora2D::GridCoord> m_hovered;
    std::optional<Xora2D::GridCoord> m_dragStart;
    std::optional<Xora2D::GridCoord> m_lastStrokeCell;
    std::optional<Xora2D::TileRect> m_selection;
    Xora2D::TileClipboard m_clipboard;
    bool m_pasteMode = false;
    std::optional<Xora2D::TilemapStroke> m_stroke;
    Xora2D::UUID m_strokeEntity{};

    const Xora2D::Scene* m_historyScene = nullptr;
    std::unordered_map<Xora2D::UUID, Xora2D::TilemapEditHistory> m_histories;
    std::string m_message;
    bool m_messageIsError = false;
};

} // namespace Xora2DEditor
