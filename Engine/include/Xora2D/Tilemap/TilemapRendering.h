#pragma once

#include "Xora2D/Tilemap/TilemapComponent.h"
#include "Xora2D/Tilemap/Tileset.h"

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <functional>

namespace Xora2D {

class OrthographicCamera;
class Renderer2D;
class Texture2D;

// UV corners (BL, BR, TR, TL positions) for a tile with the cell's rotation (quarter turns CCW) and flips applied.
void BuildTileUVs(const glm::vec4& uvRect, std::uint8_t flags, glm::vec2 outCorners[4]);

// Returns the chunk's cached geometry, rebuilding it only when the chunk changed (revision), or the tileset / tile
// size changed (settings key). Tiles whose id is not in the tileset are skipped.
const TileChunkGeometry& GetChunkGeometry(const TilemapComponent& tilemap, std::int32_t layer, std::uint64_t chunkKey,
                                          const Tileset* tileset, bool* rebuilt = nullptr);

struct TilemapRenderStats {
    std::uint32_t LayersDrawn = 0;
    std::uint32_t ChunksDrawn = 0;
    std::uint32_t ChunksCulled = 0;
    std::uint32_t ChunksRebuilt = 0;
    std::uint32_t TilesDrawn = 0;
};

struct TilemapRenderResources {
    // Resolve a tileset / its texture (normally through the AssetManager). Null = skip the layer.
    std::function<const Tileset*(AssetHandle)> GetTileset;
    std::function<const Texture2D*(AssetHandle)> GetTexture;
};

// Draws the tilemap's visible layers in order (only those with AboveSprites == `aboveSprites`). Chunks outside the
// camera view are culled; cached chunk geometry is submitted in bulk, so a frame without edits rebuilds nothing.
void RenderTilemap(Renderer2D& renderer, const TilemapComponent& tilemap, const glm::mat4& model,
                   const OrthographicCamera& camera, bool aboveSprites, const TilemapRenderResources& resources,
                   TilemapRenderStats* stats = nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Collision geometry
// ---------------------------------------------------------------------------------------------------------------

// A merged block of collidable cells (in cells, tilemap-local).
struct TileCollisionRect {
    std::int32_t X = 0;
    std::int32_t Y = 0;
    std::int32_t Width = 0;
    std::int32_t Height = 0;
    TileCollision Kind = TileCollision::Solid;
};

// Greedy-merges the collidable cells of one chunk (all collision layers combined; Solid wins over OneWay) into as
// few rectangles as possible. Rectangles never cross chunk borders, so chunks rebuild independently.
std::vector<TileCollisionRect> BuildChunkCollisionRects(const TilemapComponent& tilemap, std::uint64_t chunkKey,
                                                        const std::function<const Tileset*(AssetHandle)>& getTileset);
// Chunk keys that contain tiles on a collision layer.
std::vector<std::uint64_t> CollectCollisionChunks(const TilemapComponent& tilemap);

} // namespace Xora2D
