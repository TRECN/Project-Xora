#include "Xora2D/Tilemap/Tileset.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"

#include <stb_image.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace Xora2D {

namespace {

TilesetLoadResult LoadFailure(std::string message) {
    TilesetLoadResult result{};
    result.Message = std::move(message);
    return result;
}

bool RegionInside(const TileRegion& region, std::int32_t width, std::int32_t height) {
    return region.Width > 0 && region.Height > 0 && region.X >= 0 && region.Y >= 0 &&
           region.X + region.Width <= width && region.Y + region.Height <= height;
}

} // namespace

const char* ToString(TileCollision collision) {
    switch (collision) {
    case TileCollision::None:
        return "None";
    case TileCollision::Solid:
        return "Solid";
    case TileCollision::OneWay:
        return "OneWay";
    }
    return "None";
}

bool ParseTileCollision(const std::string& text, TileCollision& out) {
    if (text == "None") {
        out = TileCollision::None;
    } else if (text == "Solid") {
        out = TileCollision::Solid;
    } else if (text == "OneWay") {
        out = TileCollision::OneWay;
    } else {
        return false;
    }
    return true;
}

bool ReadImageSize(const std::filesystem::path& file, std::int32_t& width, std::int32_t& height) {
    int w = 0;
    int h = 0;
    int channels = 0;
    if (stbi_info(file.string().c_str(), &w, &h, &channels) == 0 || w <= 0 || h <= 0) {
        return false;
    }
    width = w;
    height = h;
    return true;
}

glm::vec4 TileRegionToUV(const TileRegion& region, std::int32_t textureWidth, std::int32_t textureHeight) {
    const float texW = static_cast<float>(std::max(textureWidth, 1));
    const float texH = static_cast<float>(std::max(textureHeight, 1));
    const float u0 = static_cast<float>(region.X) / texW;
    const float u1 = static_cast<float>(region.X + region.Width) / texW;
    // Textures are loaded flipped (v = 0 is the bottom row of the image).
    const float v0 = static_cast<float>(textureHeight - (region.Y + region.Height)) / texH;
    const float v1 = static_cast<float>(textureHeight - region.Y) / texH;
    return {u0, v0, u1, v1};
}

std::string ValidateTilesetSettings(const TilesetSettings& settings) {
    if (settings.TextureWidth <= 0 || settings.TextureHeight <= 0) {
        return "Texture size is unknown or zero (the source image is missing or unreadable)";
    }
    if (settings.TileWidth <= 0 || settings.TileHeight <= 0) {
        return "Tile width and height must be positive";
    }
    if (settings.TileWidth > 4096 || settings.TileHeight > 4096) {
        return "Tiles larger than 4096 pixels are not supported";
    }
    if (settings.Margin < 0 || settings.Spacing < 0) {
        return "Margin and spacing cannot be negative";
    }
    if (settings.Mode == TilesetMode::Grid) {
        if (settings.Margin * 2 + settings.TileWidth > settings.TextureWidth ||
            settings.Margin * 2 + settings.TileHeight > settings.TextureHeight) {
            return "No complete tile fits inside the texture with this tile size and margin";
        }
    }
    return {};
}

TilesetBuildResult Tileset::Build(std::string name, const TilesetSettings& settings,
                                  const std::vector<TileProperties>& properties) {
    TilesetBuildResult result{};
    if (name.empty()) {
        result.Message = "Tileset name is empty";
        return result;
    }
    if (std::string error = ValidateTilesetSettings(settings); !error.empty()) {
        result.Message = error;
        return result;
    }
    std::vector<TileDefinition> tiles;
    std::int32_t columns = 0;
    std::int32_t rows = 0;
    if (settings.Mode == TilesetMode::Grid) {
        // Columns = how many (tile + spacing) steps fit between the margins (no spacing after the last tile).
        columns = (settings.TextureWidth - 2 * settings.Margin + settings.Spacing) / (settings.TileWidth + settings.Spacing);
        rows = (settings.TextureHeight - 2 * settings.Margin + settings.Spacing) / (settings.TileHeight + settings.Spacing);
        tiles.reserve(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows));
        for (std::int32_t row = 0; row < rows; ++row) {
            for (std::int32_t column = 0; column < columns; ++column) {
                TileDefinition tile{};
                tile.Id = static_cast<TileId>(row * columns + column + 1);
                tile.Region = {settings.Margin + column * (settings.TileWidth + settings.Spacing),
                               settings.Margin + row * (settings.TileHeight + settings.Spacing), settings.TileWidth,
                               settings.TileHeight};
                tiles.push_back(tile);
            }
        }
    }

    std::unordered_set<TileId> seen;
    for (const TileProperties& property : properties) {
        if (property.Id == kEmptyTile || !seen.insert(property.Id).second) {
            result.Message = "Tile id " + std::to_string(property.Id) + " is invalid or used twice";
            return result;
        }
        if (settings.Mode == TilesetMode::Manual) {
            if (!RegionInside(property.Region, settings.TextureWidth, settings.TextureHeight)) {
                result.Message = "Tile " + std::to_string(property.Id) + " region is empty or outside the texture";
                return result;
            }
            TileDefinition tile{};
            tile.Id = property.Id;
            tile.Region = property.Region;
            tiles.push_back(tile);
        }
    }
    if (settings.Mode == TilesetMode::Manual) {
        std::sort(tiles.begin(), tiles.end(), [](const TileDefinition& a, const TileDefinition& b) { return a.Id < b.Id; });
        if (tiles.empty()) {
            result.Message = "A manual tileset needs at least one tile region";
            return result;
        }
        if (tiles.back().Id > 1'000'000) {
            result.Message = "Tile ids above 1000000 are not supported";
            return result;
        }
    }
    if (tiles.empty()) {
        result.Message = "The tileset contains no tiles";
        return result;
    }

    std::vector<std::int32_t> index(static_cast<std::size_t>(tiles.back().Id) + 1, -1);
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        index[tiles[i].Id] = static_cast<std::int32_t>(i);
        tiles[i].UV = TileRegionToUV(tiles[i].Region, settings.TextureWidth, settings.TextureHeight);
        const float halfU = 0.5f / static_cast<float>(settings.TextureWidth);
        const float halfV = 0.5f / static_cast<float>(settings.TextureHeight);
        tiles[i].RenderUV = {tiles[i].UV.x + halfU, tiles[i].UV.y + halfV, tiles[i].UV.z - halfU, tiles[i].UV.w - halfV};
    }
    for (const TileProperties& property : properties) {
        if (property.Id >= index.size() || index[property.Id] < 0) {
            result.Message = "Tile " + std::to_string(property.Id) + " does not exist in this grid";
            return result;
        }
        TileDefinition& tile = tiles[static_cast<std::size_t>(index[property.Id])];
        tile.Collision = property.Collision;
        tile.Tint = property.Tint;
        tile.Name = property.Name;
    }

    m_name = std::move(name);
    m_settings = settings;
    m_tiles = std::move(tiles);
    m_indexById = std::move(index);
    m_columns = columns;
    m_rows = rows;
    result.Success = true;
    result.Message = "Tileset '" + m_name + "' has " + std::to_string(m_tiles.size()) + " tiles";
    return result;
}

const TileDefinition* Tileset::FindTile(TileId id) const {
    if (id == kEmptyTile || id >= m_indexById.size()) {
        return nullptr;
    }
    const std::int32_t index = m_indexById[id];
    return index < 0 ? nullptr : &m_tiles[static_cast<std::size_t>(index)];
}

TileId Tileset::GridTileId(std::int32_t column, std::int32_t row) const {
    if (column < 0 || row < 0 || column >= m_columns || row >= m_rows) {
        return kEmptyTile;
    }
    return static_cast<TileId>(row * m_columns + column + 1);
}

std::vector<TileProperties> Tileset::GetProperties() const {
    std::vector<TileProperties> properties;
    for (const TileDefinition& tile : m_tiles) {
        const bool custom = tile.Collision != TileCollision::None || tile.Tint != glm::vec4(1.0f) || !tile.Name.empty();
        if (m_settings.Mode == TilesetMode::Manual || custom) {
            TileProperties property{};
            property.Id = tile.Id;
            property.Collision = tile.Collision;
            property.Tint = tile.Tint;
            property.Name = tile.Name;
            property.Region = tile.Region;
            properties.push_back(property);
        }
    }
    return properties;
}

// ---------------------------------------------------------------------------------------------------------------

TilesetLoadResult TilesetSerializer::Parse(const std::string& yamlText, const std::string& sourceName,
                                           AssetManager* assets) {
    YAML::Node root;
    try {
        root = YAML::Load(yamlText);
    } catch (const YAML::Exception& exception) {
        return LoadFailure(sourceName + ": malformed YAML (" + exception.what() + ")");
    }
    if (!root.IsMap() || !root["Tileset"] || !root["Tileset"].IsMap()) {
        return LoadFailure(sourceName + ": not a tileset (missing 'Tileset' header)");
    }
    std::string name;
    TilesetSettings settings{};
    std::vector<TileProperties> properties;
    try {
        const YAML::Node header = root["Tileset"];
        if (!header["FormatVersion"]) {
            return LoadFailure(sourceName + ": missing Tileset.FormatVersion");
        }
        const int version = header["FormatVersion"].as<int>();
        if (version < 1 || version > Tileset::kFormatVersion) {
            return LoadFailure(sourceName + ": unsupported tileset format version " + std::to_string(version));
        }
        name = header["Name"] ? header["Name"].as<std::string>() : std::string{};
        if (header["Texture"]) {
            const auto handle = AssetHandle::FromString(header["Texture"].as<std::string>());
            if (!handle.has_value()) {
                return LoadFailure(sourceName + ": invalid Tileset.Texture handle");
            }
            settings.Texture = *handle;
        }
        const auto readPair = [&](const char* key, std::int32_t& a, std::int32_t& b) {
            const YAML::Node node = header[key];
            if (!node) {
                return true;
            }
            if (!node.IsSequence() || node.size() != 2) {
                return false;
            }
            a = node[0].as<std::int32_t>();
            b = node[1].as<std::int32_t>();
            return true;
        };
        if (!readPair("TextureSize", settings.TextureWidth, settings.TextureHeight) ||
            !readPair("TileSize", settings.TileWidth, settings.TileHeight)) {
            return LoadFailure(sourceName + ": TextureSize / TileSize must be [width, height]");
        }
        settings.Margin = header["Margin"] ? header["Margin"].as<std::int32_t>() : 0;
        settings.Spacing = header["Spacing"] ? header["Spacing"].as<std::int32_t>() : 0;
        const std::string mode = header["Mode"] ? header["Mode"].as<std::string>() : std::string("Grid");
        if (mode == "Grid") {
            settings.Mode = TilesetMode::Grid;
        } else if (mode == "Manual") {
            settings.Mode = TilesetMode::Manual;
        } else {
            return LoadFailure(sourceName + ": Tileset.Mode must be Grid or Manual");
        }
        if (const YAML::Node tiles = root["Tiles"]) {
            if (!tiles.IsSequence()) {
                return LoadFailure(sourceName + ": 'Tiles' must be a sequence");
            }
            for (const YAML::Node& node : tiles) {
                TileProperties property{};
                property.Id = node["Id"] ? node["Id"].as<TileId>() : kEmptyTile;
                if (node["Collision"] && !ParseTileCollision(node["Collision"].as<std::string>(), property.Collision)) {
                    return LoadFailure(sourceName + ": unknown collision '" + node["Collision"].as<std::string>() + "'");
                }
                if (node["Name"]) {
                    property.Name = node["Name"].as<std::string>();
                }
                if (const YAML::Node tint = node["Tint"]; tint && tint.IsSequence() && tint.size() == 4) {
                    property.Tint = {tint[0].as<float>(), tint[1].as<float>(), tint[2].as<float>(), tint[3].as<float>()};
                }
                if (const YAML::Node region = node["Region"]) {
                    if (!region.IsSequence() || region.size() != 4) {
                        return LoadFailure(sourceName + ": Region must be [x, y, width, height]");
                    }
                    property.Region = {region[0].as<std::int32_t>(), region[1].as<std::int32_t>(),
                                       region[2].as<std::int32_t>(), region[3].as<std::int32_t>()};
                }
                properties.push_back(property);
            }
        }
    } catch (const YAML::Exception& exception) {
        return LoadFailure(sourceName + ": invalid value (" + exception.what() + ")");
    }

    TilesetLoadResult result{};
    // Validate the recorded size against the actual image when it is available.
    if (assets != nullptr && settings.Texture.IsValid()) {
        const AssetMetadata* meta = assets->GetRegistry().FindByHandle(settings.Texture);
        std::int32_t width = 0;
        std::int32_t height = 0;
        if (meta == nullptr || meta->Type != AssetType::Texture2D) {
            result.Warnings.push_back("texture " + settings.Texture.ToString() + " is not a registered texture");
        } else if (!ReadImageSize(ResolveAssetPath(meta->RelativePath), width, height)) {
            result.Warnings.push_back("texture file Assets/" + meta->RelativePath + " is missing or unreadable");
        } else if (width != settings.TextureWidth || height != settings.TextureHeight) {
            return LoadFailure(sourceName + ": the texture is now " + std::to_string(width) + "x" +
                               std::to_string(height) + " but the tileset was made for " +
                               std::to_string(settings.TextureWidth) + "x" + std::to_string(settings.TextureHeight) +
                               "; re-import the tileset");
        }
    } else if (!settings.Texture.IsValid()) {
        result.Warnings.push_back("the tileset has no texture");
    }

    auto tileset = std::make_shared<Tileset>();
    const TilesetBuildResult build = tileset->Build(name, settings, properties);
    if (!build.Success) {
        return LoadFailure(sourceName + ": " + build.Message);
    }
    result.Success = true;
    result.Message = build.Message;
    result.Loaded = std::move(tileset);
    return result;
}

TilesetLoadResult TilesetSerializer::Load(const std::filesystem::path& file, AssetManager* assets) {
    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(file, errorCode)) {
        return LoadFailure("Tileset file not found: " + file.string());
    }
    std::ifstream input(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return Parse(buffer.str(), file.filename().string(), assets);
}

std::string TilesetSerializer::Emit(const Tileset& tileset) {
    const TilesetSettings& settings = tileset.GetSettings();
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "Tileset" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "FormatVersion" << YAML::Value << Tileset::kFormatVersion;
    out << YAML::Key << "Name" << YAML::Value << tileset.GetName();
    out << YAML::Key << "Texture" << YAML::Value << settings.Texture.ToString();
    out << YAML::Key << "TextureSize" << YAML::Value << YAML::Flow << YAML::BeginSeq << settings.TextureWidth
        << settings.TextureHeight << YAML::EndSeq;
    out << YAML::Key << "TileSize" << YAML::Value << YAML::Flow << YAML::BeginSeq << settings.TileWidth
        << settings.TileHeight << YAML::EndSeq;
    out << YAML::Key << "Margin" << YAML::Value << settings.Margin;
    out << YAML::Key << "Spacing" << YAML::Value << settings.Spacing;
    out << YAML::Key << "Mode" << YAML::Value << (settings.Mode == TilesetMode::Grid ? "Grid" : "Manual");
    out << YAML::EndMap;
    out << YAML::Key << "Tiles" << YAML::Value << YAML::BeginSeq;
    for (const TileProperties& property : tileset.GetProperties()) {
        out << YAML::Flow << YAML::BeginMap;
        out << YAML::Key << "Id" << YAML::Value << property.Id;
        if (property.Collision != TileCollision::None) {
            out << YAML::Key << "Collision" << YAML::Value << ToString(property.Collision);
        }
        if (!property.Name.empty()) {
            out << YAML::Key << "Name" << YAML::Value << property.Name;
        }
        if (property.Tint != glm::vec4(1.0f)) {
            out << YAML::Key << "Tint" << YAML::Value << YAML::Flow << YAML::BeginSeq << property.Tint.x
                << property.Tint.y << property.Tint.z << property.Tint.w << YAML::EndSeq;
        }
        if (settings.Mode == TilesetMode::Manual) {
            out << YAML::Key << "Region" << YAML::Value << YAML::Flow << YAML::BeginSeq << property.Region.X
                << property.Region.Y << property.Region.Width << property.Region.Height << YAML::EndSeq;
        }
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;
    return std::string(out.c_str()) + "\n";
}

TilesetBuildResult TilesetSerializer::Save(const Tileset& tileset, const std::filesystem::path& file,
                                           bool allowOverwrite) {
    TilesetBuildResult result{};
    std::error_code errorCode;
    if (!allowOverwrite && std::filesystem::exists(file, errorCode)) {
        result.Message = "A file already exists at " + file.string();
        return result;
    }
    const std::string text = Emit(tileset);
    const TilesetLoadResult check = Parse(text, file.filename().string(), nullptr);
    if (!check.Success) {
        result.Message = "Tileset data is invalid, nothing was written: " + check.Message;
        return result;
    }
    std::filesystem::create_directories(file.parent_path(), errorCode);
    std::random_device device;
    const std::filesystem::path temporary = file.parent_path() / (".xora_tileset_" + std::to_string(device()) + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            result.Message = "Cannot write to " + file.parent_path().string();
            return result;
        }
        output << text;
        if (!output.flush()) {
            output.close();
            std::filesystem::remove(temporary, errorCode);
            result.Message = "Failed while writing " + file.string();
            return result;
        }
    }
    std::filesystem::rename(temporary, file, errorCode);
    if (errorCode) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        result.Message = "Cannot replace " + file.string() + ": " + errorCode.message();
        return result;
    }
    result.Success = true;
    result.Message = "Saved tileset " + file.filename().string();
    return result;
}

} // namespace Xora2D
