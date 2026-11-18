#include "Xora2D/Tilemap/TilemapEditing.h"

#include <algorithm>
#include <unordered_set>

namespace Xora2D {

namespace {

TilemapEditResult Fail(std::string message) {
    TilemapEditResult result{};
    result.Message = std::move(message);
    return result;
}

// Applies one cell change and records it.
void ChangeCell(TilemapComponent& tilemap, std::int32_t layer, const GridCoord& cell, const TileCell& value,
                TilemapCommand& command) {
    const TileCell before = tilemap.GetTile(layer, cell);
    if (tilemap.SetTile(layer, cell, value)) {
        command.Changes.push_back(TileChange{layer, cell, before, tilemap.GetTile(layer, cell)});
    }
}

TilemapEditResult Finish(TilemapCommand command, const std::string& verb) {
    TilemapEditResult result{};
    result.Success = true;
    result.ChangedCells = command.Changes.size();
    result.Message = verb + " " + std::to_string(result.ChangedCells) + " tile(s)";
    result.Command = std::move(command);
    return result;
}

void ClampActiveLayer(TilemapComponent& tilemap) {
    tilemap.ActiveLayer = std::clamp(tilemap.ActiveLayer, 0, std::max(0, static_cast<std::int32_t>(tilemap.Layers.size()) - 1));
}

} // namespace

std::size_t TilemapCommand::ApproximateBytes() const {
    std::size_t bytes = sizeof(TilemapCommand) + Changes.size() * sizeof(TileChange);
    if (LayerSnapshot.has_value()) {
        bytes += LayerSnapshot->Chunks.size() * sizeof(TileChunk);
    }
    return bytes;
}

std::string CheckLayerEditable(const TilemapComponent& tilemap, std::int32_t layer) {
    if (!tilemap.IsValidLayer(layer)) {
        return "No such layer";
    }
    const TilemapLayer& data = tilemap.Layers[static_cast<std::size_t>(layer)];
    if (data.Locked) {
        return "Layer '" + data.Name + "' is locked";
    }
    if (!data.Visible) {
        return "Layer '" + data.Name + "' is hidden";
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------

TilemapEditHistory::TilemapEditHistory(std::size_t maxCommands)
    : m_maxCommands(std::max<std::size_t>(maxCommands, 1)) {}

void TilemapEditHistory::Record(TilemapCommand command) {
    if (command.IsEmpty()) {
        return;
    }
    m_redo.clear();
    m_undo.push_back(std::move(command));
    while (m_undo.size() > m_maxCommands) {
        m_undo.pop_front();
    }
}

bool TilemapEditHistory::Undo(TilemapComponent& tilemap) {
    if (m_undo.empty()) {
        return false;
    }
    ApplyTilemapCommand(tilemap, m_undo.back(), false);
    m_redo.push_back(std::move(m_undo.back()));
    m_undo.pop_back();
    return true;
}

bool TilemapEditHistory::Redo(TilemapComponent& tilemap) {
    if (m_redo.empty()) {
        return false;
    }
    ApplyTilemapCommand(tilemap, m_redo.back(), true);
    m_undo.push_back(std::move(m_redo.back()));
    m_redo.pop_back();
    return true;
}

std::size_t TilemapEditHistory::ApproximateBytes() const {
    std::size_t bytes = 0;
    for (const auto* list : {&m_undo, &m_redo}) {
        for (const TilemapCommand& command : *list) {
            bytes += command.ApproximateBytes();
        }
    }
    return bytes;
}

void TilemapEditHistory::Clear() {
    m_undo.clear();
    m_redo.clear();
}

void ApplyTilemapCommand(TilemapComponent& tilemap, const TilemapCommand& command, bool forward) {
    switch (command.Kind) {
    case TilemapCommandKind::Tiles:
        if (forward) {
            for (const TileChange& change : command.Changes) {
                tilemap.SetTile(change.Layer, change.Cell, change.After);
            }
        } else {
            for (auto it = command.Changes.rbegin(); it != command.Changes.rend(); ++it) {
                tilemap.SetTile(it->Layer, it->Cell, it->Before);
            }
        }
        break;
    case TilemapCommandKind::AddLayer:
    case TilemapCommandKind::RemoveLayer: {
        const bool insert = (command.Kind == TilemapCommandKind::AddLayer) == forward;
        if (insert) {
            const auto index = static_cast<std::size_t>(std::clamp<std::int32_t>(
                command.LayerIndex, 0, static_cast<std::int32_t>(tilemap.Layers.size())));
            TilemapLayer layer = command.LayerSnapshot.value_or(TilemapLayer{});
            layer.RenderCache.clear();
            tilemap.Layers.insert(tilemap.Layers.begin() + static_cast<std::ptrdiff_t>(index), std::move(layer));
        } else if (tilemap.IsValidLayer(command.LayerIndex)) {
            tilemap.Layers.erase(tilemap.Layers.begin() + command.LayerIndex);
        }
        ClampActiveLayer(tilemap);
        tilemap.MarkCollisionDirty();
        break;
    }
    case TilemapCommandKind::RenameLayer:
        if (tilemap.IsValidLayer(command.LayerIndex)) {
            tilemap.Layers[static_cast<std::size_t>(command.LayerIndex)].Name = forward ? command.NewName : command.OldName;
        }
        break;
    case TilemapCommandKind::MoveLayer: {
        const std::int32_t from = forward ? command.LayerIndex : command.OtherIndex;
        const std::int32_t to = forward ? command.OtherIndex : command.LayerIndex;
        if (tilemap.IsValidLayer(from) && tilemap.IsValidLayer(to)) {
            TilemapLayer layer = std::move(tilemap.Layers[static_cast<std::size_t>(from)]);
            tilemap.Layers.erase(tilemap.Layers.begin() + from);
            tilemap.Layers.insert(tilemap.Layers.begin() + to, std::move(layer));
            tilemap.MarkCollisionDirty();
        }
        break;
    }
    }
}

// ---------------------------------------------------------------------------------------------------------------

TilemapStroke::TilemapStroke(TilemapComponent& tilemap, std::int32_t layer, std::string label)
    : m_tilemap(&tilemap), m_layer(layer), m_editable(CheckLayerEditable(tilemap, layer).empty()) {
    m_command.Label = std::move(label);
}

bool TilemapStroke::Paint(const GridCoord& cell, const TileCell& value) {
    if (!m_editable) {
        return false;
    }
    const auto touched = m_touched.find(cell);
    if (touched != m_touched.end() && m_command.Changes[touched->second].After == value) {
        return false; // duplicate event for a cell this drag already painted
    }
    const TileCell before = m_tilemap->GetTile(m_layer, cell);
    if (!m_tilemap->SetTile(m_layer, cell, value)) {
        return false;
    }
    if (touched != m_touched.end()) {
        m_command.Changes[touched->second].After = value; // keep the original Before
    } else {
        m_touched.emplace(cell, m_command.Changes.size());
        m_command.Changes.push_back(TileChange{m_layer, cell, before, value});
    }
    return true;
}

TilemapCommand TilemapStroke::Finish() {
    m_touched.clear();
    // Cells painted and then restored within the same drag are no-ops.
    std::erase_if(m_command.Changes, [](const TileChange& change) { return change.Before == change.After; });
    return std::move(m_command);
}

TileRect TileRect::FromCorners(const GridCoord& a, const GridCoord& b) {
    return {{std::min(a.X, b.X), std::min(a.Y, b.Y)}, {std::max(a.X, b.X), std::max(a.Y, b.Y)}};
}

TilemapEditResult PaintTiles(TilemapComponent& tilemap, std::int32_t layer, const std::vector<GridCoord>& cells,
                             const TileCell& value) {
    if (std::string error = CheckLayerEditable(tilemap, layer); !error.empty()) {
        return Fail(error);
    }
    TilemapCommand command;
    command.Label = value.IsEmpty() ? "Erase" : "Paint";
    std::unordered_set<GridCoord, GridCoordHash> seen;
    for (const GridCoord& cell : cells) {
        if (seen.insert(cell).second) {
            ChangeCell(tilemap, layer, cell, value, command);
        }
    }
    return Finish(std::move(command), value.IsEmpty() ? "Erased" : "Painted");
}

TilemapEditResult EraseTiles(TilemapComponent& tilemap, std::int32_t layer, const std::vector<GridCoord>& cells) {
    return PaintTiles(tilemap, layer, cells, TileCell{});
}

TilemapEditResult FillRect(TilemapComponent& tilemap, std::int32_t layer, const TileRect& rect, const TileCell& value) {
    if (std::string error = CheckLayerEditable(tilemap, layer); !error.empty()) {
        return Fail(error);
    }
    if (rect.Width() <= 0 || rect.Height() <= 0 || rect.Area() > kMaxTileOperationCells) {
        return Fail("Rectangle is empty or larger than " + std::to_string(kMaxTileOperationCells) + " cells");
    }
    TilemapCommand command;
    command.Label = value.IsEmpty() ? "Erase Rectangle" : "Fill Rectangle";
    for (std::int32_t y = rect.Min.Y; y <= rect.Max.Y; ++y) {
        for (std::int32_t x = rect.Min.X; x <= rect.Max.X; ++x) {
            ChangeCell(tilemap, layer, {x, y}, value, command);
        }
    }
    return Finish(std::move(command), "Filled");
}

TilemapEditResult BucketFill(TilemapComponent& tilemap, std::int32_t layer, const GridCoord& start,
                             const TileCell& value, std::int64_t maxCells) {
    if (std::string error = CheckLayerEditable(tilemap, layer); !error.empty()) {
        return Fail(error);
    }
    const TileCell target = tilemap.GetTile(layer, start);
    if (target == value) {
        return Fail("The region already has this tile");
    }
    std::optional<TileRect> limit;
    if (target.IsEmpty()) {
        const auto bounds = tilemap.GetBounds(-1);
        if (!bounds.has_value() || !TileRect{bounds->first, bounds->second}.Contains(start)) {
            return Fail("Bucket fill on empty space only works inside the painted area (draw a border first)");
        }
        limit = TileRect{bounds->first, bounds->second};
    }

    std::vector<GridCoord> region;
    std::vector<GridCoord> stack{start};
    std::unordered_set<GridCoord, GridCoordHash> visited{start};
    while (!stack.empty()) {
        const GridCoord cell = stack.back();
        stack.pop_back();
        region.push_back(cell);
        if (static_cast<std::int64_t>(region.size()) > maxCells) {
            return Fail("Bucket fill region is larger than " + std::to_string(maxCells) + " cells");
        }
        for (const GridCoord next : {GridCoord{cell.X + 1, cell.Y}, GridCoord{cell.X - 1, cell.Y},
                                     GridCoord{cell.X, cell.Y + 1}, GridCoord{cell.X, cell.Y - 1}}) {
            if ((limit.has_value() && !limit->Contains(next)) || visited.contains(next)) {
                continue;
            }
            if (tilemap.GetTile(layer, next) == target) {
                visited.insert(next);
                stack.push_back(next);
            }
        }
    }
    TilemapCommand command;
    command.Label = "Bucket Fill";
    for (const GridCoord& cell : region) {
        ChangeCell(tilemap, layer, cell, value, command);
    }
    return Finish(std::move(command), "Filled");
}

TileCell PickTile(const TilemapComponent& tilemap, std::int32_t layer, const GridCoord& cell, std::int32_t* foundLayer) {
    if (layer >= 0) {
        if (foundLayer != nullptr) {
            *foundLayer = layer;
        }
        return tilemap.GetTile(layer, cell);
    }
    for (std::int32_t index = static_cast<std::int32_t>(tilemap.Layers.size()) - 1; index >= 0; --index) {
        if (!tilemap.Layers[static_cast<std::size_t>(index)].Visible) {
            continue;
        }
        const TileCell tile = tilemap.GetTile(index, cell);
        if (!tile.IsEmpty()) {
            if (foundLayer != nullptr) {
                *foundLayer = index;
            }
            return tile;
        }
    }
    return {};
}

TileClipboard CopyTiles(const TilemapComponent& tilemap, std::int32_t layer, const TileRect& rect) {
    TileClipboard clipboard;
    if (!tilemap.IsValidLayer(layer) || rect.Width() <= 0 || rect.Height() <= 0 ||
        rect.Area() > kMaxTileOperationCells) {
        return clipboard;
    }
    clipboard.Width = rect.Width();
    clipboard.Height = rect.Height();
    clipboard.Cells.reserve(static_cast<std::size_t>(rect.Area()));
    for (std::int32_t y = rect.Min.Y; y <= rect.Max.Y; ++y) {
        for (std::int32_t x = rect.Min.X; x <= rect.Max.X; ++x) {
            clipboard.Cells.push_back(tilemap.GetTile(layer, {x, y}));
        }
    }
    return clipboard;
}

TilemapEditResult PasteTiles(TilemapComponent& tilemap, std::int32_t layer, const GridCoord& origin,
                             const TileClipboard& clipboard, bool skipEmpty) {
    if (std::string error = CheckLayerEditable(tilemap, layer); !error.empty()) {
        return Fail(error);
    }
    if (clipboard.IsEmpty()) {
        return Fail("Nothing to paste");
    }
    TilemapCommand command;
    command.Label = "Paste";
    for (std::int32_t y = 0; y < clipboard.Height; ++y) {
        for (std::int32_t x = 0; x < clipboard.Width; ++x) {
            const TileCell& tile = clipboard.Cells[static_cast<std::size_t>(y * clipboard.Width + x)];
            if (skipEmpty && tile.IsEmpty()) {
                continue;
            }
            ChangeCell(tilemap, layer, {origin.X + x, origin.Y + y}, tile, command);
        }
    }
    return Finish(std::move(command), "Pasted");
}

TilemapEditResult MoveTiles(TilemapComponent& tilemap, std::int32_t layer, const TileRect& rect, const GridCoord& offset) {
    if (std::string error = CheckLayerEditable(tilemap, layer); !error.empty()) {
        return Fail(error);
    }
    const TileClipboard clipboard = CopyTiles(tilemap, layer, rect);
    if (clipboard.IsEmpty()) {
        return Fail("Nothing to move");
    }
    TilemapCommand command;
    command.Label = "Move Tiles";
    for (std::int32_t y = rect.Min.Y; y <= rect.Max.Y; ++y) {
        for (std::int32_t x = rect.Min.X; x <= rect.Max.X; ++x) {
            ChangeCell(tilemap, layer, {x, y}, TileCell{}, command);
        }
    }
    for (std::int32_t y = 0; y < clipboard.Height; ++y) {
        for (std::int32_t x = 0; x < clipboard.Width; ++x) {
            const TileCell& tile = clipboard.Cells[static_cast<std::size_t>(y * clipboard.Width + x)];
            if (!tile.IsEmpty()) {
                ChangeCell(tilemap, layer, {rect.Min.X + offset.X + x, rect.Min.Y + offset.Y + y}, tile, command);
            }
        }
    }
    // A cell may have been cleared and then filled again: merge into one net change per cell for a clean undo.
    std::unordered_map<GridCoord, std::size_t, GridCoordHash> first;
    std::vector<TileChange> merged;
    for (const TileChange& change : command.Changes) {
        const auto found = first.find(change.Cell);
        if (found == first.end()) {
            first.emplace(change.Cell, merged.size());
            merged.push_back(change);
        } else {
            merged[found->second].After = change.After;
        }
    }
    std::erase_if(merged, [](const TileChange& change) { return change.Before == change.After; });
    command.Changes = std::move(merged);
    return Finish(std::move(command), "Moved");
}

TilemapEditResult AddLayer(TilemapComponent& tilemap, const std::string& name, std::int32_t index) {
    if (name.empty()) {
        return Fail("Layer name is empty");
    }
    const std::int32_t position =
        index < 0 ? static_cast<std::int32_t>(tilemap.Layers.size())
                  : std::min(index, static_cast<std::int32_t>(tilemap.Layers.size()));
    TilemapLayer layer{};
    layer.Name = name;
    TilemapCommand command;
    command.Kind = TilemapCommandKind::AddLayer;
    command.Label = "Add Layer";
    command.LayerIndex = position;
    command.LayerSnapshot = layer;
    ApplyTilemapCommand(tilemap, command, true);
    tilemap.ActiveLayer = position;
    TilemapEditResult result{};
    result.Success = true;
    result.Message = "Added layer '" + name + "'";
    result.Command = std::move(command);
    return result;
}

TilemapEditResult RemoveLayer(TilemapComponent& tilemap, std::int32_t index) {
    if (!tilemap.IsValidLayer(index)) {
        return Fail("No such layer");
    }
    TilemapCommand command;
    command.Kind = TilemapCommandKind::RemoveLayer;
    command.Label = "Remove Layer";
    command.LayerIndex = index;
    command.LayerSnapshot = tilemap.Layers[static_cast<std::size_t>(index)]; // needed to undo
    command.LayerSnapshot->RenderCache.clear();
    ApplyTilemapCommand(tilemap, command, true);
    TilemapEditResult result{};
    result.Success = true;
    result.Message = "Removed layer '" + command.LayerSnapshot->Name + "'";
    result.Command = std::move(command);
    return result;
}

TilemapEditResult RenameLayer(TilemapComponent& tilemap, std::int32_t index, const std::string& name) {
    if (!tilemap.IsValidLayer(index) || name.empty()) {
        return Fail("Invalid layer or empty name");
    }
    TilemapCommand command;
    command.Kind = TilemapCommandKind::RenameLayer;
    command.Label = "Rename Layer";
    command.LayerIndex = index;
    command.OldName = tilemap.Layers[static_cast<std::size_t>(index)].Name;
    command.NewName = name;
    ApplyTilemapCommand(tilemap, command, true);
    TilemapEditResult result{};
    result.Success = true;
    result.Message = "Renamed layer to '" + name + "'";
    result.Command = std::move(command);
    return result;
}

TilemapEditResult MoveLayer(TilemapComponent& tilemap, std::int32_t from, std::int32_t to) {
    if (!tilemap.IsValidLayer(from) || !tilemap.IsValidLayer(to) || from == to) {
        return Fail("Invalid layer move");
    }
    TilemapCommand command;
    command.Kind = TilemapCommandKind::MoveLayer;
    command.Label = "Reorder Layers";
    command.LayerIndex = from;
    command.OtherIndex = to;
    ApplyTilemapCommand(tilemap, command, true);
    if (tilemap.ActiveLayer == from) {
        tilemap.ActiveLayer = to;
    }
    TilemapEditResult result{};
    result.Success = true;
    result.Message = "Moved layer";
    result.Command = std::move(command);
    return result;
}

TilemapEditResult DuplicateLayer(TilemapComponent& tilemap, std::int32_t index) {
    if (!tilemap.IsValidLayer(index)) {
        return Fail("No such layer");
    }
    TilemapLayer copy = tilemap.Layers[static_cast<std::size_t>(index)];
    copy.Name += " Copy";
    copy.RenderCache.clear();
    for (auto& [key, chunk] : copy.Chunks) {
        (void)key;
        chunk.Revision = tilemap.NextRevision++;
    }
    TilemapCommand command;
    command.Kind = TilemapCommandKind::AddLayer;
    command.Label = "Duplicate Layer";
    command.LayerIndex = index + 1;
    command.LayerSnapshot = std::move(copy);
    ApplyTilemapCommand(tilemap, command, true);
    tilemap.ActiveLayer = index + 1;
    TilemapEditResult result{};
    result.Success = true;
    result.Message = "Duplicated layer";
    result.Command = std::move(command);
    return result;
}

} // namespace Xora2D
