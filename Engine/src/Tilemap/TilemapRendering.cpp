#include "Xora2D/Tilemap/TilemapRendering.h"

#include "Xora2D/Renderer/OrthographicCamera.h"
#include "Xora2D/Renderer/Renderer2D.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

namespace Xora2D {

namespace {

std::uint64_t SettingsKey(const Tileset* tileset, const glm::ivec2& tileSize) {
    const auto pointer = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(tileset));
    return (pointer * 1099511628211ull) ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tileSize.x)) << 20) ^
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(tileSize.y));
}

} // namespace

void BuildTileUVs(const glm::vec4& uvRect, std::uint8_t flags, glm::vec2 outCorners[4]) {
    const glm::vec2 base[4] = {{uvRect.x, uvRect.y}, {uvRect.z, uvRect.y}, {uvRect.z, uvRect.w}, {uvRect.x, uvRect.w}};
    const int turns = (flags & kTileRotationMask) >> kTileRotationShift;
    std::array<int, 4> source{};
    // Rotating the image CCW by one quarter moves what was at corner i to corner i + 1.
    for (int corner = 0; corner < 4; ++corner) {
        source[static_cast<std::size_t>(corner)] = (corner - turns + 4) % 4;
    }
    if ((flags & kTileFlipX) != 0) {
        std::swap(source[0], source[1]);
        std::swap(source[3], source[2]);
    }
    if ((flags & kTileFlipY) != 0) {
        std::swap(source[0], source[3]);
        std::swap(source[1], source[2]);
    }
    for (int corner = 0; corner < 4; ++corner) {
        outCorners[corner] = base[source[static_cast<std::size_t>(corner)]];
    }
}

const TileChunkGeometry& GetChunkGeometry(const TilemapComponent& tilemap, std::int32_t layer, std::uint64_t chunkKey,
                                          const Tileset* tileset, bool* rebuilt) {
    const TilemapLayer& data = tilemap.Layers[static_cast<std::size_t>(layer)];
    TileChunkGeometry& geometry = data.RenderCache[chunkKey];
    const auto found = data.Chunks.find(chunkKey);
    const std::uint64_t revision = found == data.Chunks.end() ? 0 : found->second.Revision;
    const std::uint64_t settingsKey = SettingsKey(tileset, tilemap.TileSize);
    if (rebuilt != nullptr) {
        *rebuilt = false;
    }
    if (geometry.BuiltRevision == revision && geometry.BuiltSettingsKey == settingsKey) {
        return geometry;
    }
    geometry.Vertices.clear(); // keeps capacity: no reallocation for an edited chunk of similar size
    if (found != data.Chunks.end() && tileset != nullptr) {
        const TileChunk& chunk = found->second;
        geometry.Vertices.reserve(static_cast<std::size_t>(chunk.Count) * 4);
        const GridCoord origin = ChunkFromKey(chunkKey);
        const float width = static_cast<float>(tilemap.TileSize.x);
        const float height = static_cast<float>(tilemap.TileSize.y);
        for (std::size_t index = 0; index < kTileChunkCellCount; ++index) {
            const TileCell& cell = chunk.Cells[index];
            if (cell.IsEmpty()) {
                continue;
            }
            const TileDefinition* tile = tileset->FindTile(cell.Id);
            if (tile == nullptr) {
                continue; // invalid id: kept in the data, not drawn
            }
            const float x = static_cast<float>(origin.X * kTileChunkSize + static_cast<std::int32_t>(index % kTileChunkSize)) * width;
            const float y = static_cast<float>(origin.Y * kTileChunkSize + static_cast<std::int32_t>(index / kTileChunkSize)) * height;
            glm::vec2 uvs[4];
            BuildTileUVs(tile->RenderUV, cell.Flags, uvs);
            const glm::vec2 positions[4] = {{x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}};
            for (int corner = 0; corner < 4; ++corner) {
                geometry.Vertices.push_back(TileVertex{positions[corner], uvs[corner], tile->Tint});
            }
        }
    }
    geometry.BuiltRevision = revision;
    geometry.BuiltSettingsKey = settingsKey;
    if (rebuilt != nullptr) {
        *rebuilt = true;
    }
    return geometry;
}

void RenderTilemap(Renderer2D& renderer, const TilemapComponent& tilemap, const glm::mat4& model,
                   const OrthographicCamera& camera, bool aboveSprites, const TilemapRenderResources& resources,
                   TilemapRenderStats* stats) {
    // Camera view rectangle in world space (culling is skipped for rotated cameras).
    const bool cull = camera.GetRotationRadians() == 0.0f;
    const glm::vec2 viewMin = camera.GetPosition();
    const glm::vec2 viewMax =
        viewMin + glm::vec2{camera.GetViewportWidth(), camera.GetViewportHeight()} / std::max(camera.GetZoom(), 0.01f);
    const float chunkWidth = static_cast<float>(tilemap.TileSize.x * kTileChunkSize);
    const float chunkHeight = static_cast<float>(tilemap.TileSize.y * kTileChunkSize);

    for (std::int32_t layer = 0; layer < static_cast<std::int32_t>(tilemap.Layers.size()); ++layer) {
        const TilemapLayer& data = tilemap.Layers[static_cast<std::size_t>(layer)];
        if (!data.Visible || data.AboveSprites != aboveSprites || data.Opacity <= 0.0f || data.Chunks.empty()) {
            continue;
        }
        const AssetHandle tilesetHandle = tilemap.GetLayerTileset(layer);
        const Tileset* tileset = resources.GetTileset ? resources.GetTileset(tilesetHandle) : nullptr;
        if (tileset == nullptr) {
            continue;
        }
        const Texture2D* texture = resources.GetTexture ? resources.GetTexture(tileset->GetSettings().Texture) : nullptr;
        if (texture == nullptr) {
            continue;
        }
        if (stats != nullptr) {
            ++stats->LayersDrawn;
        }
        const glm::vec4 tint{1.0f, 1.0f, 1.0f, std::clamp(data.Opacity, 0.0f, 1.0f)};
        for (const auto& [key, chunk] : data.Chunks) {
            if (chunk.Count == 0) {
                continue;
            }
            if (cull) {
                const GridCoord origin = ChunkFromKey(key);
                const glm::vec2 local{static_cast<float>(origin.X) * chunkWidth, static_cast<float>(origin.Y) * chunkHeight};
                glm::vec2 lo{1e30f};
                glm::vec2 hi{-1e30f};
                for (const glm::vec2 corner : {local, local + glm::vec2{chunkWidth, 0.0f}, local + glm::vec2{0.0f, chunkHeight},
                                               local + glm::vec2{chunkWidth, chunkHeight}}) {
                    const glm::vec4 world = model * glm::vec4(corner, 0.0f, 1.0f);
                    lo = glm::min(lo, glm::vec2(world));
                    hi = glm::max(hi, glm::vec2(world));
                }
                if (hi.x < viewMin.x || lo.x > viewMax.x || hi.y < viewMin.y || lo.y > viewMax.y) {
                    if (stats != nullptr) {
                        ++stats->ChunksCulled;
                    }
                    continue;
                }
            }
            bool rebuilt = false;
            const TileChunkGeometry& geometry = GetChunkGeometry(tilemap, layer, key, tileset, &rebuilt);
            if (geometry.Vertices.empty()) {
                continue;
            }
            renderer.DrawTexturedQuads(*texture, geometry.Vertices.data(), geometry.Vertices.size(), model, tint);
            if (stats != nullptr) {
                ++stats->ChunksDrawn;
                stats->ChunksRebuilt += rebuilt ? 1u : 0u;
                stats->TilesDrawn += static_cast<std::uint32_t>(geometry.Vertices.size() / 4);
            }
        }
        // Drop cache entries of chunks that no longer exist (erased when emptied).
        if (data.RenderCache.size() > data.Chunks.size()) {
            std::erase_if(data.RenderCache, [&data](const auto& entry) { return !data.Chunks.contains(entry.first); });
        }
    }
}

std::vector<std::uint64_t> CollectCollisionChunks(const TilemapComponent& tilemap) {
    std::vector<std::uint64_t> keys;
    std::unordered_set<std::uint64_t> seen;
    for (const TilemapLayer& layer : tilemap.Layers) {
        if (!layer.Collision) {
            continue;
        }
        for (const auto& [key, chunk] : layer.Chunks) {
            if (chunk.Count > 0 && seen.insert(key).second) {
                keys.push_back(key);
            }
        }
    }
    return keys;
}

std::vector<TileCollisionRect> BuildChunkCollisionRects(const TilemapComponent& tilemap, std::uint64_t chunkKey,
                                                        const std::function<const Tileset*(AssetHandle)>& getTileset) {
    // 0 = empty, 1 = solid, 2 = one-way.
    std::array<std::uint8_t, kTileChunkCellCount> kinds{};
    bool any = false;
    for (std::int32_t index = 0; index < static_cast<std::int32_t>(tilemap.Layers.size()); ++index) {
        const TilemapLayer& layer = tilemap.Layers[static_cast<std::size_t>(index)];
        if (!layer.Collision) {
            continue;
        }
        const auto found = layer.Chunks.find(chunkKey);
        if (found == layer.Chunks.end()) {
            continue;
        }
        const Tileset* tileset = getTileset ? getTileset(tilemap.GetLayerTileset(index)) : nullptr;
        if (tileset == nullptr) {
            continue;
        }
        for (std::size_t cell = 0; cell < kTileChunkCellCount; ++cell) {
            const TileCell& tile = found->second.Cells[cell];
            if (tile.IsEmpty()) {
                continue;
            }
            const TileDefinition* definition = tileset->FindTile(tile.Id);
            if (definition == nullptr || definition->Collision == TileCollision::None) {
                continue;
            }
            const std::uint8_t kind = definition->Collision == TileCollision::Solid ? 1 : 2;
            if (kinds[cell] == 0 || kind == 1) {
                kinds[cell] = kind;
            }
            any = true;
        }
    }
    std::vector<TileCollisionRect> rects;
    if (!any) {
        return rects;
    }
    std::array<bool, kTileChunkCellCount> used{};
    const GridCoord origin = ChunkFromKey(chunkKey);
    const auto at = [](std::int32_t x, std::int32_t y) { return static_cast<std::size_t>(y * kTileChunkSize + x); };
    for (std::int32_t y = 0; y < kTileChunkSize; ++y) {
        for (std::int32_t x = 0; x < kTileChunkSize; ++x) {
            const std::uint8_t kind = kinds[at(x, y)];
            if (kind == 0 || used[at(x, y)]) {
                continue;
            }
            std::int32_t width = 1;
            while (x + width < kTileChunkSize && kinds[at(x + width, y)] == kind && !used[at(x + width, y)]) {
                ++width;
            }
            std::int32_t height = 1;
            while (y + height < kTileChunkSize) {
                bool rowMatches = true;
                for (std::int32_t dx = 0; dx < width; ++dx) {
                    if (kinds[at(x + dx, y + height)] != kind || used[at(x + dx, y + height)]) {
                        rowMatches = false;
                        break;
                    }
                }
                if (!rowMatches) {
                    break;
                }
                ++height;
            }
            for (std::int32_t dy = 0; dy < height; ++dy) {
                for (std::int32_t dx = 0; dx < width; ++dx) {
                    used[at(x + dx, y + dy)] = true;
                }
            }
            rects.push_back(TileCollisionRect{origin.X * kTileChunkSize + x, origin.Y * kTileChunkSize + y, width, height,
                                              kind == 1 ? TileCollision::Solid : TileCollision::OneWay});
        }
    }
    return rects;
}

} // namespace Xora2D
