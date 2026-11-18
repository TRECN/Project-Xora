#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Renderer/QuadVertex.h"
#include "Xora2D/Tilemap/TilemapTypes.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Xora2D {

// 32x32 cells stored inline (8 KiB). `Revision` changes whenever a cell changes; render and collision caches compare
// it to know which chunks to rebuild.
struct TileChunk {
    std::array<TileCell, kTileChunkCellCount> Cells{};
    std::uint32_t Count = 0;
    std::uint64_t Revision = 0;
};

// Cached render geometry of one chunk (runtime only, never serialized). Rebuilt only when the chunk revision, the
// tileset or the tile size changes.
using TileVertex = TexturedQuadVertex; // positions in tilemap-local pixels

struct TileChunkGeometry {
    std::vector<TileVertex> Vertices; // 4 per quad
    std::uint64_t BuiltRevision = ~0ull;
    std::uint64_t BuiltSettingsKey = ~0ull;
};

struct TilemapLayer {
    std::string Name = "Layer";
    bool Visible = true;
    bool Locked = false;
    float Opacity = 1.0f;
    // Drawn after sprites (foreground). Other layers are drawn before sprites.
    bool AboveSprites = false;
    // Participates in tile collision.
    bool Collision = false;
    // Optional per-layer tileset; invalid = the tilemap's tileset.
    AssetHandle Tileset{};

    std::unordered_map<std::uint64_t, TileChunk> Chunks;
    mutable std::unordered_map<std::uint64_t, TileChunkGeometry> RenderCache;
};

struct TilemapCollisionSettings {
    bool Enabled = true;
    float Friction = 0.6f;
    float Restitution = 0.0f;
    // Box2D filter bits for the tilemap's fixtures (collision layers).
    std::uint16_t CategoryBits = 0x0001;
    std::uint16_t MaskBits = 0xFFFF;

    bool operator==(const TilemapCollisionSettings&) const = default;
};

// A grid of tiles on the entity's transform. The data model is independent of the editor: tiles are read and
// written through SetTile / GetTile (or TilemapEditing for undoable edits).
struct TilemapComponent {
    AssetHandle Tileset{};
    // Pixel size of one cell (square by default; rectangular cells are supported).
    glm::ivec2 TileSize{16, 16};
    std::vector<TilemapLayer> Layers;
    std::int32_t ActiveLayer = 0;
    TilemapCollisionSettings Collision;
    // Tilemap asset this data was loaded from / is saved to (optional).
    AssetHandle Source{};

    // Bumped by structural changes that affect collision (layer collision flags, tile size, tileset, settings).
    std::uint64_t CollisionRevision = 1;
    // Monotonic source for chunk revisions (never reused, so a deleted and re-created chunk is still "new").
    std::uint64_t NextRevision = 1;

    // ---- Layers -------------------------------------------------------------------------------------------------
    [[nodiscard]] bool IsValidLayer(std::int32_t layer) const {
        return layer >= 0 && layer < static_cast<std::int32_t>(Layers.size());
    }
    [[nodiscard]] std::int32_t FindLayer(const std::string& name) const;
    // Background, Ground (collision), Decorations, Foreground (above sprites).
    void CreateDefaultLayers();
    [[nodiscard]] AssetHandle GetLayerTileset(std::int32_t layer) const;
    // Call after changing layer collision flags / tile size / tileset / collision settings directly.
    void MarkCollisionDirty() { ++CollisionRevision; }
    // Call after changing tile size / tileset directly (render caches rebuild lazily).
    void InvalidateRenderCache() const;

    // ---- Cells (no lock/visibility checks: that is an editor rule, see TilemapEditing) -------------------------
    [[nodiscard]] TileCell GetTile(std::int32_t layer, const GridCoord& cell) const;
    // Returns true when the cell changed.
    bool SetTile(std::int32_t layer, const GridCoord& cell, const TileCell& value);
    bool ClearTile(std::int32_t layer, const GridCoord& cell) { return SetTile(layer, cell, TileCell{}); }
    void ClearLayer(std::int32_t layer);
    [[nodiscard]] std::size_t GetTileCount(std::int32_t layer) const;
    [[nodiscard]] std::size_t GetTotalTileCount() const;
    // Inclusive cell bounds of all tiles in a layer (nullopt when empty); -1 = every layer.
    [[nodiscard]] std::optional<std::pair<GridCoord, GridCoord>> GetBounds(std::int32_t layer = -1) const;
    // Calls fn(cell, tile) for every non-empty cell of the layer (chunk order).
    template <typename Function>
    void ForEachTile(std::int32_t layer, Function&& fn) const;

    // ---- Coordinates (tilemap-local pixels; apply the entity transform yourself) -------------------------------
    [[nodiscard]] GridCoord LocalToCell(const glm::vec2& localPixels) const;
    [[nodiscard]] glm::vec2 CellToLocal(const GridCoord& cell) const; // bottom-left corner of the cell
    // Combined revision of every collision-relevant chunk at `chunk` (used by physics to rebuild only what changed).
    [[nodiscard]] std::uint64_t CollisionSignature(std::uint64_t chunkKey) const;
};

template <typename Function>
void TilemapComponent::ForEachTile(std::int32_t layer, Function&& fn) const {
    if (!IsValidLayer(layer)) {
        return;
    }
    for (const auto& [key, chunk] : Layers[static_cast<std::size_t>(layer)].Chunks) {
        if (chunk.Count == 0) {
            continue;
        }
        const GridCoord origin = ChunkFromKey(key);
        for (std::size_t index = 0; index < kTileChunkCellCount; ++index) {
            const TileCell& tile = chunk.Cells[index];
            if (!tile.IsEmpty()) {
                const GridCoord cell{origin.X * kTileChunkSize + static_cast<std::int32_t>(index % kTileChunkSize),
                                     origin.Y * kTileChunkSize + static_cast<std::int32_t>(index / kTileChunkSize)};
                fn(cell, tile);
            }
        }
    }
}

} // namespace Xora2D
