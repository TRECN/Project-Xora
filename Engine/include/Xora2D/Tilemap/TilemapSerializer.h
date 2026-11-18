#pragma once

#include "Xora2D/Tilemap/TilemapComponent.h"

#include <filesystem>
#include <string>
#include <vector>

namespace YAML {
class Emitter;
class Node;
} // namespace YAML

namespace Xora2D {

class AssetManager;

// Tilemap data format, shared by scenes (TilemapComponent), prefabs and `.tilemap.yaml` assets:
//
//   FormatVersion: 1
//   Tileset: <handle>
//   TileSize: [16, 16]
//   ActiveLayer: 1
//   Source: <tilemap asset handle>          # optional
//   Collision: {Enabled: true, Friction: 0.6, Restitution: 0, Category: 1, Mask: 65535}
//   Layers:
//     - Name: Ground
//       Visible: true
//       Locked: false
//       Opacity: 1
//       AboveSprites: false
//       Collision: true
//       Tileset: <handle>                    # optional per-layer override
//       Chunks:
//         - At: [0, -1]                      # chunk coordinate (32x32 cells)
//           Tiles: "0x40 5x3 7:1 0x977"      # 1024 cells, row-major from the bottom-left, run-length encoded:
//                                            # <id>[:<flags>][x<count>]
//
// Only tile ids and flags are stored (never texture data).
class TilemapSerializer {
public:
    static constexpr int kFormatVersion = 1;

    // Emits the map entries (the caller has opened the YAML map).
    static void EmitBody(YAML::Emitter& emitter, const TilemapComponent& tilemap);
    // Fails on malformed data (nothing partially applied: `out` is only assigned on success).
    static bool ParseBody(const YAML::Node& node, TilemapComponent& out, std::string& error);

    // Run-length encoding of one chunk (exposed for tests).
    [[nodiscard]] static std::string EncodeChunk(const TileChunk& chunk);
    static bool DecodeChunk(const std::string& text, TileChunk& out, std::string& error);

    // Missing tilesets / textures and tile ids the tileset does not define (reported, never fatal).
    [[nodiscard]] static std::vector<std::string> ValidateReferences(const TilemapComponent& tilemap,
                                                                     AssetManager& assets);

    // `.tilemap.yaml` asset: {Tilemap: {Name: ...}, <body>}. Save is validated and atomic.
    static bool SaveAsset(const TilemapComponent& tilemap, const std::string& name, const std::filesystem::path& file,
                          bool allowOverwrite, std::string& error);
    static bool LoadAsset(const std::filesystem::path& file, TilemapComponent& out, std::string& name,
                          std::string& error);
};

} // namespace Xora2D
