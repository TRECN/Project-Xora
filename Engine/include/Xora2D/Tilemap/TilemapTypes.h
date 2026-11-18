#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace Xora2D {

// Tile ids are stable per tileset: grid tilesets number tiles row-major from 1 (row * columns + column + 1);
// manual tilesets assign ids explicitly. 0 is "no tile".
using TileId = std::uint32_t;
inline constexpr TileId kEmptyTile = 0;

// Per-cell rendering flags. Rotation is applied first (quarter turns, counter-clockwise), then flips.
enum TileFlagBits : std::uint8_t {
    kTileFlipX = 1u << 0,
    kTileFlipY = 1u << 1,
    kTileRotationShift = 2, // bits 2-3: quarter turns 0..3
    kTileRotationMask = 0x3u << kTileRotationShift,
};

// One grid cell: 8 bytes, stored by value inside chunks (no per-tile allocations).
struct TileCell {
    TileId Id = kEmptyTile;
    std::uint8_t Flags = 0;

    [[nodiscard]] bool IsEmpty() const { return Id == kEmptyTile; }
    [[nodiscard]] bool FlipX() const { return (Flags & kTileFlipX) != 0; }
    [[nodiscard]] bool FlipY() const { return (Flags & kTileFlipY) != 0; }
    [[nodiscard]] int Rotation() const { return (Flags & kTileRotationMask) >> kTileRotationShift; }

    static TileCell Make(TileId id, bool flipX = false, bool flipY = false, int quarterTurns = 0) {
        TileCell cell{};
        cell.Id = id;
        cell.Flags = static_cast<std::uint8_t>((flipX ? kTileFlipX : 0) | (flipY ? kTileFlipY : 0) |
                                               (((quarterTurns % 4 + 4) % 4) << kTileRotationShift));
        return cell;
    }

    bool operator==(const TileCell&) const = default;
};

// Integer grid coordinate (cell). Cell (x, y) covers [x * tileWidth, (x + 1) * tileWidth) horizontally and
// [y * tileHeight, (y + 1) * tileHeight) vertically in the tilemap's local space; +Y is up like the rest of the
// engine. Negative coordinates are valid.
struct GridCoord {
    std::int32_t X = 0;
    std::int32_t Y = 0;

    bool operator==(const GridCoord&) const = default;
};

struct GridCoordHash {
    std::size_t operator()(const GridCoord& coord) const {
        return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.X)) << 32) |
                                          static_cast<std::uint32_t>(coord.Y));
    }
};

enum class TileCollision : std::uint8_t {
    None = 0, // decorative
    Solid,    // blocks from every side
    OneWay,   // platform: solid only from above
};

// Chunks are 32x32 cells: the unit of storage, render-geometry caching and collision rebuilding.
inline constexpr std::int32_t kTileChunkSize = 32;
inline constexpr std::size_t kTileChunkCellCount = static_cast<std::size_t>(kTileChunkSize * kTileChunkSize);

[[nodiscard]] inline std::int32_t FloorDiv(std::int32_t value, std::int32_t divisor) {
    std::int32_t quotient = value / divisor;
    if ((value % divisor != 0) && ((value < 0) != (divisor < 0))) {
        --quotient;
    }
    return quotient;
}

[[nodiscard]] inline GridCoord ChunkOf(const GridCoord& cell) {
    return {FloorDiv(cell.X, kTileChunkSize), FloorDiv(cell.Y, kTileChunkSize)};
}

[[nodiscard]] inline std::uint64_t ChunkKey(const GridCoord& chunk) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunk.X)) << 32) |
           static_cast<std::uint32_t>(chunk.Y);
}

[[nodiscard]] inline GridCoord ChunkFromKey(std::uint64_t key) {
    return {static_cast<std::int32_t>(static_cast<std::uint32_t>(key >> 32)),
            static_cast<std::int32_t>(static_cast<std::uint32_t>(key & 0xFFFFFFFFu))};
}

// Index of a cell inside its chunk (row-major from the chunk's bottom-left).
[[nodiscard]] inline std::size_t CellIndexInChunk(const GridCoord& cell) {
    const std::int32_t localX = cell.X - FloorDiv(cell.X, kTileChunkSize) * kTileChunkSize;
    const std::int32_t localY = cell.Y - FloorDiv(cell.Y, kTileChunkSize) * kTileChunkSize;
    return static_cast<std::size_t>(localY * kTileChunkSize + localX);
}

} // namespace Xora2D
