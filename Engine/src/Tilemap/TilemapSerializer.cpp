#include "Xora2D/Tilemap/TilemapSerializer.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Tilemap/Tileset.h"

#include <yaml-cpp/yaml.h>

#include <charconv>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <system_error>

namespace Xora2D {

namespace {

std::optional<AssetHandle> ReadHandle(const YAML::Node& node) {
    if (!node || node.IsNull()) {
        return AssetHandle{};
    }
    const std::string text = node.as<std::string>();
    if (text.empty() || text == "0") {
        return AssetHandle{};
    }
    return AssetHandle::FromString(text);
}

template <typename T>
bool ParseNumber(std::string_view text, T& value) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

} // namespace

std::string TilemapSerializer::EncodeChunk(const TileChunk& chunk) {
    std::string out;
    out.reserve(64);
    std::size_t index = 0;
    while (index < kTileChunkCellCount) {
        const TileCell cell = chunk.Cells[index];
        std::size_t run = 1;
        while (index + run < kTileChunkCellCount && chunk.Cells[index + run] == cell) {
            ++run;
        }
        if (!out.empty()) {
            out.push_back(' ');
        }
        out += std::to_string(cell.Id);
        if (cell.Flags != 0) {
            out += ':' + std::to_string(cell.Flags);
        }
        if (run > 1) {
            out += 'x' + std::to_string(run);
        }
        index += run;
    }
    return out;
}

bool TilemapSerializer::DecodeChunk(const std::string& text, TileChunk& out, std::string& error) {
    TileChunk chunk{};
    std::size_t index = 0;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        std::string_view view(token);
        std::size_t run = 1;
        if (const std::size_t x = view.find('x'); x != std::string_view::npos) {
            if (!ParseNumber(view.substr(x + 1), run) || run == 0) {
                error = "invalid run length in '" + token + "'";
                return false;
            }
            view = view.substr(0, x);
        }
        TileCell cell{};
        if (const std::size_t colon = view.find(':'); colon != std::string_view::npos) {
            unsigned flags = 0;
            if (!ParseNumber(view.substr(colon + 1), flags) || flags > 0x0F) {
                error = "invalid tile flags in '" + token + "'";
                return false;
            }
            cell.Flags = static_cast<std::uint8_t>(flags);
            view = view.substr(0, colon);
        }
        if (!ParseNumber(view, cell.Id)) {
            error = "invalid tile id in '" + token + "'";
            return false;
        }
        if (cell.Id == kEmptyTile) {
            cell.Flags = 0;
        }
        if (index + run > kTileChunkCellCount) {
            error = "chunk has more than 1024 cells";
            return false;
        }
        for (std::size_t i = 0; i < run; ++i) {
            chunk.Cells[index + i] = cell;
        }
        if (!cell.IsEmpty()) {
            chunk.Count += static_cast<std::uint32_t>(run);
        }
        index += run;
    }
    if (index != kTileChunkCellCount) {
        error = "chunk has " + std::to_string(index) + " cells instead of 1024";
        return false;
    }
    out = chunk;
    return true;
}

void TilemapSerializer::EmitBody(YAML::Emitter& out, const TilemapComponent& tilemap) {
    out << YAML::Key << "FormatVersion" << YAML::Value << kFormatVersion;
    out << YAML::Key << "Tileset" << YAML::Value << tilemap.Tileset.ToString();
    out << YAML::Key << "TileSize" << YAML::Value << YAML::Flow << YAML::BeginSeq << tilemap.TileSize.x
        << tilemap.TileSize.y << YAML::EndSeq;
    out << YAML::Key << "ActiveLayer" << YAML::Value << tilemap.ActiveLayer;
    if (tilemap.Source.IsValid()) {
        out << YAML::Key << "Source" << YAML::Value << tilemap.Source.ToString();
    }
    out << YAML::Key << "Collision" << YAML::Value << YAML::Flow << YAML::BeginMap;
    out << YAML::Key << "Enabled" << YAML::Value << tilemap.Collision.Enabled;
    out << YAML::Key << "Friction" << YAML::Value << tilemap.Collision.Friction;
    out << YAML::Key << "Restitution" << YAML::Value << tilemap.Collision.Restitution;
    out << YAML::Key << "Category" << YAML::Value << tilemap.Collision.CategoryBits;
    out << YAML::Key << "Mask" << YAML::Value << tilemap.Collision.MaskBits;
    out << YAML::EndMap;
    out << YAML::Key << "Layers" << YAML::Value << YAML::BeginSeq;
    for (const TilemapLayer& layer : tilemap.Layers) {
        out << YAML::BeginMap;
        out << YAML::Key << "Name" << YAML::Value << layer.Name;
        out << YAML::Key << "Visible" << YAML::Value << layer.Visible;
        out << YAML::Key << "Locked" << YAML::Value << layer.Locked;
        out << YAML::Key << "Opacity" << YAML::Value << layer.Opacity;
        out << YAML::Key << "AboveSprites" << YAML::Value << layer.AboveSprites;
        out << YAML::Key << "Collision" << YAML::Value << layer.Collision;
        if (layer.Tileset.IsValid()) {
            out << YAML::Key << "Tileset" << YAML::Value << layer.Tileset.ToString();
        }
        // Deterministic chunk order (scene files diff cleanly).
        std::set<std::pair<std::int32_t, std::int32_t>> order;
        for (const auto& [key, chunk] : layer.Chunks) {
            if (chunk.Count > 0) {
                const GridCoord coord = ChunkFromKey(key);
                order.insert({coord.Y, coord.X});
            }
        }
        out << YAML::Key << "Chunks" << YAML::Value << YAML::BeginSeq;
        for (const auto& [y, x] : order) {
            const TileChunk& chunk = layer.Chunks.at(ChunkKey({x, y}));
            out << YAML::BeginMap;
            out << YAML::Key << "At" << YAML::Value << YAML::Flow << YAML::BeginSeq << x << y << YAML::EndSeq;
            out << YAML::Key << "Tiles" << YAML::Value << EncodeChunk(chunk);
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
}

bool TilemapSerializer::ParseBody(const YAML::Node& node, TilemapComponent& out, std::string& error) {
    TilemapComponent tilemap{};
    try {
        const int version = node["FormatVersion"] ? node["FormatVersion"].as<int>() : 1;
        if (version < 1 || version > kFormatVersion) {
            error = "unsupported tilemap format version " + std::to_string(version);
            return false;
        }
        const auto tileset = ReadHandle(node["Tileset"]);
        if (!tileset.has_value()) {
            error = "invalid tilemap Tileset handle";
            return false;
        }
        tilemap.Tileset = *tileset;
        if (const YAML::Node size = node["TileSize"]) {
            if (!size.IsSequence() || size.size() != 2) {
                error = "TileSize must be [width, height]";
                return false;
            }
            tilemap.TileSize = {size[0].as<int>(), size[1].as<int>()};
        }
        if (tilemap.TileSize.x <= 0 || tilemap.TileSize.y <= 0 || tilemap.TileSize.x > 4096 || tilemap.TileSize.y > 4096) {
            error = "TileSize must be between 1 and 4096";
            return false;
        }
        if (node["Source"]) {
            const auto source = ReadHandle(node["Source"]);
            if (!source.has_value()) {
                error = "invalid tilemap Source handle";
                return false;
            }
            tilemap.Source = *source;
        }
        if (const YAML::Node collision = node["Collision"]) {
            tilemap.Collision.Enabled = collision["Enabled"].as<bool>(true);
            tilemap.Collision.Friction = collision["Friction"].as<float>(0.6f);
            tilemap.Collision.Restitution = collision["Restitution"].as<float>(0.0f);
            tilemap.Collision.CategoryBits = collision["Category"].as<std::uint16_t>(0x0001);
            tilemap.Collision.MaskBits = collision["Mask"].as<std::uint16_t>(0xFFFF);
        }
        if (const YAML::Node layers = node["Layers"]) {
            if (!layers.IsSequence()) {
                error = "Layers must be a sequence";
                return false;
            }
            for (const YAML::Node& layerNode : layers) {
                TilemapLayer layer{};
                layer.Name = layerNode["Name"].as<std::string>("Layer");
                layer.Visible = layerNode["Visible"].as<bool>(true);
                layer.Locked = layerNode["Locked"].as<bool>(false);
                layer.Opacity = std::clamp(layerNode["Opacity"].as<float>(1.0f), 0.0f, 1.0f);
                layer.AboveSprites = layerNode["AboveSprites"].as<bool>(false);
                layer.Collision = layerNode["Collision"].as<bool>(false);
                const auto layerTileset = ReadHandle(layerNode["Tileset"]);
                if (!layerTileset.has_value()) {
                    error = "invalid Tileset handle on layer '" + layer.Name + "'";
                    return false;
                }
                layer.Tileset = *layerTileset;
                if (const YAML::Node chunks = layerNode["Chunks"]) {
                    if (!chunks.IsSequence()) {
                        error = "Chunks of layer '" + layer.Name + "' must be a sequence";
                        return false;
                    }
                    for (const YAML::Node& chunkNode : chunks) {
                        const YAML::Node at = chunkNode["At"];
                        if (!at || !at.IsSequence() || at.size() != 2 || !chunkNode["Tiles"]) {
                            error = "chunk of layer '" + layer.Name + "' needs At: [x, y] and Tiles";
                            return false;
                        }
                        const GridCoord coord{at[0].as<std::int32_t>(), at[1].as<std::int32_t>()};
                        TileChunk chunk{};
                        std::string chunkError;
                        if (!DecodeChunk(chunkNode["Tiles"].as<std::string>(), chunk, chunkError)) {
                            error = "layer '" + layer.Name + "' chunk [" + std::to_string(coord.X) + ", " +
                                    std::to_string(coord.Y) + "]: " + chunkError;
                            return false;
                        }
                        if (chunk.Count == 0) {
                            continue;
                        }
                        chunk.Revision = tilemap.NextRevision++;
                        if (!layer.Chunks.emplace(ChunkKey(coord), chunk).second) {
                            error = "duplicate chunk in layer '" + layer.Name + "'";
                            return false;
                        }
                    }
                }
                tilemap.Layers.push_back(std::move(layer));
            }
        }
        tilemap.ActiveLayer = node["ActiveLayer"].as<std::int32_t>(0);
        if (!tilemap.Layers.empty()) {
            tilemap.ActiveLayer = std::clamp(tilemap.ActiveLayer, 0, static_cast<std::int32_t>(tilemap.Layers.size()) - 1);
        } else {
            tilemap.ActiveLayer = 0;
        }
    } catch (const YAML::Exception& exception) {
        error = std::string("invalid tilemap value (") + exception.what() + ")";
        return false;
    }
    out = std::move(tilemap);
    return true;
}

std::vector<std::string> TilemapSerializer::ValidateReferences(const TilemapComponent& tilemap, AssetManager& assets) {
    std::vector<std::string> warnings;
    std::set<std::uint64_t> checked;
    for (std::int32_t layer = 0; layer < static_cast<std::int32_t>(tilemap.Layers.size()); ++layer) {
        const AssetHandle handle = tilemap.GetLayerTileset(layer);
        if (tilemap.GetTileCount(layer) == 0) {
            continue;
        }
        if (!handle.IsValid()) {
            warnings.push_back("layer '" + tilemap.Layers[static_cast<std::size_t>(layer)].Name + "' has tiles but no tileset");
            continue;
        }
        const AssetResult load = assets.LoadTileset(handle);
        const auto tileset = load.Success ? assets.GetLoadedTileset(handle) : nullptr;
        if (tileset == nullptr) {
            if (checked.insert(handle.Value()).second) {
                warnings.push_back("tileset " + handle.ToString() + " is missing or invalid: " + load.Message);
            }
            continue;
        }
        std::size_t invalid = 0;
        tilemap.ForEachTile(layer, [&](const GridCoord&, const TileCell& tile) {
            invalid += tileset->FindTile(tile.Id) == nullptr ? 1 : 0;
        });
        if (invalid > 0) {
            warnings.push_back("layer '" + tilemap.Layers[static_cast<std::size_t>(layer)].Name + "' has " +
                               std::to_string(invalid) + " tile(s) with ids that tileset '" + tileset->GetName() +
                               "' does not define (kept, not drawn)");
        }
    }
    return warnings;
}

bool TilemapSerializer::SaveAsset(const TilemapComponent& tilemap, const std::string& name,
                                  const std::filesystem::path& file, bool allowOverwrite, std::string& error) {
    std::error_code errorCode;
    if (!allowOverwrite && std::filesystem::exists(file, errorCode)) {
        error = "A file already exists at " + file.string();
        return false;
    }
    if (name.empty()) {
        error = "Tilemap name is empty";
        return false;
    }
    YAML::Emitter emitter;
    emitter << YAML::BeginMap;
    emitter << YAML::Key << "Tilemap" << YAML::Value << YAML::BeginMap << YAML::Key << "Name" << YAML::Value << name
            << YAML::EndMap;
    TilemapComponent copy = tilemap;
    copy.Source = {}; // the asset does not point at itself
    EmitBody(emitter, copy);
    emitter << YAML::EndMap;
    const std::string text = std::string(emitter.c_str()) + "\n";
    // Validate what will be written.
    TilemapComponent check;
    if (!ParseBody(YAML::Load(text), check, error)) {
        error = "Tilemap data is invalid, nothing was written: " + error;
        return false;
    }
    std::filesystem::create_directories(file.parent_path(), errorCode);
    std::random_device device;
    const std::filesystem::path temporary = file.parent_path() / (".xora_tilemap_" + std::to_string(device()) + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Cannot write to " + file.parent_path().string();
            return false;
        }
        output << text;
        if (!output.flush()) {
            output.close();
            std::filesystem::remove(temporary, errorCode);
            error = "Failed while writing " + file.string();
            return false;
        }
    }
    std::filesystem::rename(temporary, file, errorCode);
    if (errorCode) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        error = "Cannot replace " + file.string() + ": " + errorCode.message();
        return false;
    }
    return true;
}

bool TilemapSerializer::LoadAsset(const std::filesystem::path& file, TilemapComponent& out, std::string& name,
                                  std::string& error) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(file.string());
    } catch (const YAML::Exception& exception) {
        error = file.filename().string() + ": malformed YAML (" + exception.what() + ")";
        return false;
    }
    if (!root.IsMap() || !root["Tilemap"]) {
        error = file.filename().string() + ": not a tilemap asset (missing 'Tilemap' header)";
        return false;
    }
    name = root["Tilemap"]["Name"].as<std::string>("");
    if (!ParseBody(root, out, error)) {
        error = file.filename().string() + ": " + error;
        return false;
    }
    return true;
}

} // namespace Xora2D
