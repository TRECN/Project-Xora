#include "Xora2D/Tilemap/TilemapComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace Xora2D {

std::int32_t TilemapComponent::FindLayer(const std::string& name) const {
    for (std::size_t index = 0; index < Layers.size(); ++index) {
        if (Layers[index].Name == name) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

void TilemapComponent::CreateDefaultLayers() {
    Layers.clear();
    for (const char* name : {"Background", "Ground", "Decorations", "Foreground"}) {
        TilemapLayer layer{};
        layer.Name = name;
        layer.Collision = std::string_view(name) == "Ground";
        layer.AboveSprites = std::string_view(name) == "Foreground";
        Layers.push_back(std::move(layer));
    }
    ActiveLayer = 1;
    MarkCollisionDirty();
}

AssetHandle TilemapComponent::GetLayerTileset(std::int32_t layer) const {
    if (IsValidLayer(layer) && Layers[static_cast<std::size_t>(layer)].Tileset.IsValid()) {
        return Layers[static_cast<std::size_t>(layer)].Tileset;
    }
    return Tileset;
}

void TilemapComponent::InvalidateRenderCache() const {
    for (const TilemapLayer& layer : Layers) {
        layer.RenderCache.clear();
    }
}

TileCell TilemapComponent::GetTile(std::int32_t layer, const GridCoord& cell) const {
    if (!IsValidLayer(layer)) {
        return {};
    }
    const auto& chunks = Layers[static_cast<std::size_t>(layer)].Chunks;
    const auto found = chunks.find(ChunkKey(ChunkOf(cell)));
    return found == chunks.end() ? TileCell{} : found->second.Cells[CellIndexInChunk(cell)];
}

bool TilemapComponent::SetTile(std::int32_t layer, const GridCoord& cell, const TileCell& value) {
    if (!IsValidLayer(layer)) {
        return false;
    }
    auto& chunks = Layers[static_cast<std::size_t>(layer)].Chunks;
    const std::uint64_t key = ChunkKey(ChunkOf(cell));
    auto found = chunks.find(key);
    if (found == chunks.end()) {
        if (value.IsEmpty()) {
            return false;
        }
        found = chunks.emplace(key, TileChunk{}).first;
    }
    TileChunk& chunk = found->second;
    TileCell& slot = chunk.Cells[CellIndexInChunk(cell)];
    if (slot == value || (slot.IsEmpty() && value.IsEmpty())) {
        return false;
    }
    if (slot.IsEmpty() && !value.IsEmpty()) {
        ++chunk.Count;
    } else if (!slot.IsEmpty() && value.IsEmpty()) {
        --chunk.Count;
    }
    slot = value.IsEmpty() ? TileCell{} : value;
    chunk.Revision = NextRevision++;
    if (chunk.Count == 0) {
        chunks.erase(found); // empty chunks are dropped; their caches notice the missing chunk
    }
    return true;
}

void TilemapComponent::ClearLayer(std::int32_t layer) {
    if (IsValidLayer(layer)) {
        Layers[static_cast<std::size_t>(layer)].Chunks.clear();
        MarkCollisionDirty();
    }
}

std::size_t TilemapComponent::GetTileCount(std::int32_t layer) const {
    if (!IsValidLayer(layer)) {
        return 0;
    }
    std::size_t count = 0;
    for (const auto& [key, chunk] : Layers[static_cast<std::size_t>(layer)].Chunks) {
        (void)key;
        count += chunk.Count;
    }
    return count;
}

std::size_t TilemapComponent::GetTotalTileCount() const {
    std::size_t count = 0;
    for (std::int32_t layer = 0; layer < static_cast<std::int32_t>(Layers.size()); ++layer) {
        count += GetTileCount(layer);
    }
    return count;
}

std::optional<std::pair<GridCoord, GridCoord>> TilemapComponent::GetBounds(std::int32_t layer) const {
    GridCoord minimum{std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::max()};
    GridCoord maximum{std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::min()};
    bool any = false;
    for (std::int32_t index = 0; index < static_cast<std::int32_t>(Layers.size()); ++index) {
        if (layer >= 0 && index != layer) {
            continue;
        }
        ForEachTile(index, [&](const GridCoord& cell, const TileCell&) {
            minimum = {std::min(minimum.X, cell.X), std::min(minimum.Y, cell.Y)};
            maximum = {std::max(maximum.X, cell.X), std::max(maximum.Y, cell.Y)};
            any = true;
        });
    }
    if (!any) {
        return std::nullopt;
    }
    return std::make_pair(minimum, maximum);
}

GridCoord TilemapComponent::LocalToCell(const glm::vec2& localPixels) const {
    const float width = static_cast<float>(std::max(TileSize.x, 1));
    const float height = static_cast<float>(std::max(TileSize.y, 1));
    return {static_cast<std::int32_t>(std::floor(localPixels.x / width)),
            static_cast<std::int32_t>(std::floor(localPixels.y / height))};
}

glm::vec2 TilemapComponent::CellToLocal(const GridCoord& cell) const {
    return {static_cast<float>(cell.X) * static_cast<float>(TileSize.x),
            static_cast<float>(cell.Y) * static_cast<float>(TileSize.y)};
}

std::uint64_t TilemapComponent::CollisionSignature(std::uint64_t chunkKey) const {
    // FNV-style mix of the structural revision and the revision of each collision layer's chunk.
    std::uint64_t signature = 1469598103934665603ull ^ CollisionRevision;
    for (std::size_t index = 0; index < Layers.size(); ++index) {
        const TilemapLayer& layer = Layers[index];
        if (!layer.Collision) {
            continue;
        }
        const auto found = layer.Chunks.find(chunkKey);
        const std::uint64_t revision = found == layer.Chunks.end() ? 0 : found->second.Revision;
        signature = (signature ^ (revision + index * 0x9E3779B97F4A7C15ull)) * 1099511628211ull;
    }
    return signature;
}

} // namespace Xora2D
