#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Tilemap/TilemapTypes.h"

#include <glm/vec4.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Xora2D {

class AssetManager;

enum class TilesetMode : std::uint8_t {
    Grid = 0, // tiles generated from tile size / margin / spacing
    Manual,   // every tile has an explicit pixel region
};

// Pixel rectangle in the source image, top-left origin (as image editors show it).
struct TileRegion {
    std::int32_t X = 0;
    std::int32_t Y = 0;
    std::int32_t Width = 0;
    std::int32_t Height = 0;

    bool operator==(const TileRegion&) const = default;
};

struct TileDefinition {
    TileId Id = kEmptyTile;
    TileRegion Region;
    // Cached texture coordinates (u0, v0, u1, v1) for the engine's bottom-up texture orientation.
    glm::vec4 UV{0.0f, 0.0f, 1.0f, 1.0f};
    // UV inset by half a texel: what rendering uses, so filtering never samples neighbouring tiles, margins or
    // spacing (no seams between tiles).
    glm::vec4 RenderUV{0.0f, 0.0f, 1.0f, 1.0f};
    TileCollision Collision = TileCollision::None;
    // Optional rendering property: multiplied with the layer tint.
    glm::vec4 Tint{1.0f};
    std::string Name;
};

struct TilesetSettings {
    AssetHandle Texture{};
    // Size of the source image in pixels (recorded at import, checked against the image on load).
    std::int32_t TextureWidth = 0;
    std::int32_t TextureHeight = 0;
    std::int32_t TileWidth = 16;
    std::int32_t TileHeight = 16;
    // Pixels around the whole sheet / between neighbouring tiles.
    std::int32_t Margin = 0;
    std::int32_t Spacing = 0;
    TilesetMode Mode = TilesetMode::Grid;

    bool operator==(const TilesetSettings&) const = default;
};

// Per-tile data a user sets on top of the generated grid (and the regions of manual tilesets).
struct TileProperties {
    TileId Id = kEmptyTile;
    TileCollision Collision = TileCollision::None;
    glm::vec4 Tint{1.0f};
    std::string Name;
    TileRegion Region; // Manual mode only
};

struct TilesetBuildResult {
    bool Success = false;
    std::string Message;
};

// A tileset: one spritesheet texture cut into tiles with stable ids, cached UVs and per-tile collision.
class Tileset {
public:
    static constexpr int kFormatVersion = 1;

    // Validates settings against the texture size and (re)generates every tile definition.
    TilesetBuildResult Build(std::string name, const TilesetSettings& settings,
                             const std::vector<TileProperties>& properties);

    [[nodiscard]] const std::string& GetName() const { return m_name; }
    [[nodiscard]] const TilesetSettings& GetSettings() const { return m_settings; }
    [[nodiscard]] const std::vector<TileDefinition>& GetTiles() const { return m_tiles; }
    [[nodiscard]] std::int32_t GetColumns() const { return m_columns; }
    [[nodiscard]] std::int32_t GetRows() const { return m_rows; }
    // O(1) lookup; nullptr for unknown ids.
    [[nodiscard]] const TileDefinition* FindTile(TileId id) const;
    [[nodiscard]] std::vector<TileProperties> GetProperties() const;

    // Grid layout helpers (Grid mode): id of the tile at a grid column / row, and back.
    [[nodiscard]] TileId GridTileId(std::int32_t column, std::int32_t row) const;

private:
    std::string m_name;
    TilesetSettings m_settings;
    std::vector<TileDefinition> m_tiles;
    std::vector<std::int32_t> m_indexById; // id -> index into m_tiles (-1 = none)
    std::int32_t m_columns = 0;
    std::int32_t m_rows = 0;
};

// Settings / dimension checks (empty string = valid). Grid mode needs at least one whole tile inside the image.
[[nodiscard]] std::string ValidateTilesetSettings(const TilesetSettings& settings);
// Texture-space UVs for a pixel region (bottom-up v, matching how textures are loaded).
[[nodiscard]] glm::vec4 TileRegionToUV(const TileRegion& region, std::int32_t textureWidth,
                                       std::int32_t textureHeight);
[[nodiscard]] const char* ToString(TileCollision collision);
[[nodiscard]] bool ParseTileCollision(const std::string& text, TileCollision& out);

struct TilesetLoadResult {
    bool Success = false;
    std::string Message;
    std::shared_ptr<Tileset> Loaded;
    std::vector<std::string> Warnings; // e.g. missing texture
};

// `.tileset.yaml`:
//   Tileset: {FormatVersion: 1, Name, Texture: <handle>, TextureSize: [w, h], TileSize: [w, h], Margin, Spacing,
//             Mode: Grid|Manual}
//   Tiles:   # optional per-tile properties (always present for Manual tiles, with Region: [x, y, w, h])
//     - {Id: 5, Collision: Solid, Name: grass}
class TilesetSerializer {
public:
    static TilesetLoadResult Parse(const std::string& yamlText, const std::string& sourceName,
                                   AssetManager* assets = nullptr);
    static TilesetLoadResult Load(const std::filesystem::path& file, AssetManager* assets = nullptr);
    [[nodiscard]] static std::string Emit(const Tileset& tileset);
    // Validated, atomic (temporary file + rename). Refuses to replace an existing file unless allowOverwrite.
    static TilesetBuildResult Save(const Tileset& tileset, const std::filesystem::path& file, bool allowOverwrite);
};

// Reads an image's pixel size from its header (no decode, no GPU). False if unreadable.
bool ReadImageSize(const std::filesystem::path& file, std::int32_t& width, std::int32_t& height);

} // namespace Xora2D
