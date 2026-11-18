#pragma once

#include "Xora2D/Tilemap/TilemapComponent.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Xora2D {

// Editing operations for tilemaps, independent of any UI. Every operation applies its change immediately and
// describes it as a TilemapCommand made of cell diffs (layer, cell, before, after), so undo/redo never copies whole
// maps. Locked or hidden layers refuse edits.

struct TileChange {
    std::int32_t Layer = 0;
    GridCoord Cell;
    TileCell Before;
    TileCell After;
};

enum class TilemapCommandKind : std::uint8_t { Tiles, AddLayer, RemoveLayer, RenameLayer, MoveLayer };

struct TilemapCommand {
    TilemapCommandKind Kind = TilemapCommandKind::Tiles;
    std::string Label;
    std::vector<TileChange> Changes;           // Tiles
    std::int32_t LayerIndex = -1;              // layer ops: affected index (MoveLayer: from)
    std::int32_t OtherIndex = -1;              // MoveLayer: to
    std::string OldName;                       // RenameLayer
    std::string NewName;                       // RenameLayer
    std::optional<TilemapLayer> LayerSnapshot; // AddLayer (redo) / RemoveLayer (undo) only

    [[nodiscard]] bool IsEmpty() const { return Kind == TilemapCommandKind::Tiles && Changes.empty(); }
    [[nodiscard]] std::size_t ApproximateBytes() const;
};

struct TilemapEditResult {
    bool Success = false;
    std::string Message;
    std::size_t ChangedCells = 0;
    TilemapCommand Command;
};

// Empty string when the layer accepts edits; otherwise the reason (missing, locked, hidden).
[[nodiscard]] std::string CheckLayerEditable(const TilemapComponent& tilemap, std::int32_t layer);

// Linear undo/redo of commands. Record() after an operation succeeded (it is already applied).
class TilemapEditHistory {
public:
    explicit TilemapEditHistory(std::size_t maxCommands = 256);

    void Record(TilemapCommand command);
    bool Undo(TilemapComponent& tilemap);
    bool Redo(TilemapComponent& tilemap);
    [[nodiscard]] bool CanUndo() const { return !m_undo.empty(); }
    [[nodiscard]] bool CanRedo() const { return !m_redo.empty(); }
    [[nodiscard]] std::string UndoLabel() const { return m_undo.empty() ? std::string{} : m_undo.back().Label; }
    [[nodiscard]] std::string RedoLabel() const { return m_redo.empty() ? std::string{} : m_redo.back().Label; }
    [[nodiscard]] std::size_t GetUndoCount() const { return m_undo.size(); }
    [[nodiscard]] std::size_t ApproximateBytes() const;
    void Clear();

private:
    std::size_t m_maxCommands;
    std::deque<TilemapCommand> m_undo;
    std::deque<TilemapCommand> m_redo;
};

// Applies a command forwards (redo) or backwards (undo). Used by the history; exposed for tests.
void ApplyTilemapCommand(TilemapComponent& tilemap, const TilemapCommand& command, bool forward);

// A brush / eraser drag: repeated events on a cell already painted with the same value are ignored, and the whole
// drag becomes one undo step.
class TilemapStroke {
public:
    TilemapStroke(TilemapComponent& tilemap, std::int32_t layer, std::string label);
    // Returns true when the cell changed. Fails silently (false) on locked / hidden layers.
    bool Paint(const GridCoord& cell, const TileCell& value);
    [[nodiscard]] std::size_t GetChangedCount() const { return m_command.Changes.size(); }
    [[nodiscard]] bool IsLayerEditable() const { return m_editable; }
    // Ends the stroke and returns its command (empty when nothing changed).
    TilemapCommand Finish();

private:
    TilemapComponent* m_tilemap;
    std::int32_t m_layer;
    bool m_editable;
    TilemapCommand m_command;
    std::unordered_map<GridCoord, std::size_t, GridCoordHash> m_touched; // cell -> index in Changes
};

// Inclusive cell rectangle.
struct TileRect {
    GridCoord Min;
    GridCoord Max;

    static TileRect FromCorners(const GridCoord& a, const GridCoord& b);
    [[nodiscard]] std::int32_t Width() const { return Max.X - Min.X + 1; }
    [[nodiscard]] std::int32_t Height() const { return Max.Y - Min.Y + 1; }
    [[nodiscard]] std::int64_t Area() const { return static_cast<std::int64_t>(Width()) * Height(); }
    [[nodiscard]] bool Contains(const GridCoord& cell) const {
        return cell.X >= Min.X && cell.X <= Max.X && cell.Y >= Min.Y && cell.Y <= Max.Y;
    }
};

inline constexpr std::int64_t kMaxTileOperationCells = 1 << 20;

TilemapEditResult PaintTiles(TilemapComponent& tilemap, std::int32_t layer, const std::vector<GridCoord>& cells,
                             const TileCell& value);
TilemapEditResult EraseTiles(TilemapComponent& tilemap, std::int32_t layer, const std::vector<GridCoord>& cells);
TilemapEditResult FillRect(TilemapComponent& tilemap, std::int32_t layer, const TileRect& rect, const TileCell& value);
// 4-connected flood fill of the region that has the same tile as `start`. Filling empty space is limited to the
// tilemap's current bounds (all layers); starting outside them fails instead of filling an unbounded area.
TilemapEditResult BucketFill(TilemapComponent& tilemap, std::int32_t layer, const GridCoord& start,
                             const TileCell& value, std::int64_t maxCells = 262'144);

// Eyedropper: the tile of one layer, or (layer < 0) of the topmost visible layer that has one.
[[nodiscard]] TileCell PickTile(const TilemapComponent& tilemap, std::int32_t layer, const GridCoord& cell,
                                std::int32_t* foundLayer = nullptr);

struct TileClipboard {
    std::int32_t Width = 0;
    std::int32_t Height = 0;
    std::vector<TileCell> Cells; // row-major from the bottom-left
    [[nodiscard]] bool IsEmpty() const { return Width <= 0 || Height <= 0; }
};

[[nodiscard]] TileClipboard CopyTiles(const TilemapComponent& tilemap, std::int32_t layer, const TileRect& rect);
// Pastes with the clipboard's bottom-left at `origin`. Empty clipboard cells keep what is underneath when
// `skipEmpty`.
TilemapEditResult PasteTiles(TilemapComponent& tilemap, std::int32_t layer, const GridCoord& origin,
                             const TileClipboard& clipboard, bool skipEmpty = true);
// Moves a region by `offset` (one undo step).
TilemapEditResult MoveTiles(TilemapComponent& tilemap, std::int32_t layer, const TileRect& rect,
                            const GridCoord& offset);

// Layer management (undoable).
TilemapEditResult AddLayer(TilemapComponent& tilemap, const std::string& name, std::int32_t index = -1);
TilemapEditResult RemoveLayer(TilemapComponent& tilemap, std::int32_t index);
TilemapEditResult RenameLayer(TilemapComponent& tilemap, std::int32_t index, const std::string& name);
TilemapEditResult MoveLayer(TilemapComponent& tilemap, std::int32_t from, std::int32_t to);
TilemapEditResult DuplicateLayer(TilemapComponent& tilemap, std::int32_t index);

} // namespace Xora2D
