// Milestone 19: tilesets, tilemap data, editing + undo, rendering, collision, serialization, runtime and Python.
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Physics/PhysicsWorld2D.h"
#include "Xora2D/Platform/Window.h"
#include "Xora2D/Prefab/Prefab.h"
#include "Xora2D/Prefab/PrefabUtility.h"
#include "Xora2D/Project/Project.h"
#include "Xora2D/Project/ProjectTemplates.h"
#include "Xora2D/Renderer/ColorRenderTarget.h"
#include "Xora2D/Renderer/GraphicsContext.h"
#include "Xora2D/Renderer/OrthographicCamera.h"
#include "Xora2D/Renderer/Renderer2D.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"
#include "Xora2D/Scripting/PythonScriptEngine.h"
#include "Xora2D/Scripting/ScriptServices.h"
#include "Xora2D/Serialization/SceneSerializer.h"
#include "Xora2D/Tilemap/TilemapComponent.h"
#include "Xora2D/Tilemap/TilemapEditing.h"
#include "Xora2D/Tilemap/TilemapRendering.h"
#include "Xora2D/Tilemap/TilemapSerializer.h"
#include "Xora2D/Tilemap/Tileset.h"

#include "Xora2DEditor/Content/ContentBrowserOperations.h"
#include "Xora2DEditor/EditorContext.h"

#include <glad/gl.h>
#include <yaml-cpp/yaml.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace Xora2D;
using Xora2DEditor::EditorContext;

int g_failures = 0;

void Check(bool condition, const std::string& description) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {}", description);
    ++g_failures;
}

// ---------------------------------------------------------------------------------------------------------------
// Minimal PNG writer (stored deflate blocks) so tests control every pixel of their spritesheets.
// ---------------------------------------------------------------------------------------------------------------

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0xFFFFFFFFu) {
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

void PutU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void WritePng(const fs::path& path, int width, int height, const std::vector<std::array<std::uint8_t, 4>>& pixels) {
    std::vector<std::uint8_t> raw;
    for (int y = 0; y < height; ++y) {
        raw.push_back(0); // filter: none
        for (int x = 0; x < width; ++x) {
            const auto& p = pixels[static_cast<std::size_t>(y * width + x)];
            raw.insert(raw.end(), p.begin(), p.end());
        }
    }
    std::vector<std::uint8_t> zlib{0x78, 0x01};
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    for (std::size_t offset = 0; offset < raw.size(); offset += 65535) {
        const std::size_t length = std::min<std::size_t>(65535, raw.size() - offset);
        zlib.push_back(offset + length == raw.size() ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(length));
        zlib.push_back(static_cast<std::uint8_t>(length >> 8));
        zlib.push_back(static_cast<std::uint8_t>(~length));
        zlib.push_back(static_cast<std::uint8_t>(~length >> 8));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
    }
    PutU32(zlib, (b << 16) | a);

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    const auto chunk = [&png](const char* type, const std::vector<std::uint8_t>& data) {
        PutU32(png, static_cast<std::uint32_t>(data.size()));
        std::vector<std::uint8_t> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        png.insert(png.end(), body.begin(), body.end());
        PutU32(png, Crc32(body.data(), body.size()) ^ 0xFFFFFFFFu);
    };
    std::vector<std::uint8_t> header;
    PutU32(header, static_cast<std::uint32_t>(width));
    PutU32(header, static_cast<std::uint32_t>(height));
    header.insert(header.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA
    chunk("IHDR", header);
    chunk("IDAT", zlib);
    chunk("IEND", {});
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(png.data()),
                                                 static_cast<std::streamsize>(png.size()));
}

using Rgba = std::array<std::uint8_t, 4>;
constexpr Rgba kRed{255, 0, 0, 255};
constexpr Rgba kGreen{0, 255, 0, 255};
constexpr Rgba kBlue{0, 0, 255, 255};
constexpr Rgba kWhite{255, 255, 255, 255};
constexpr Rgba kMagenta{255, 0, 255, 255};
constexpr Rgba kYellow{255, 255, 0, 255};

// 2x2 tiles of 8x8 pixels, margin 1, spacing 2 -> 20x20 image. Tile 1 has four coloured quadrants
// (top-left red, top-right green, bottom-left blue, bottom-right white); tiles 2-4 are solid green / blue / yellow.
// Margin and spacing pixels are magenta so sampling outside a tile is detectable.
void WriteTestSheet(const fs::path& path) {
    std::vector<Rgba> pixels(20 * 20, kMagenta);
    const auto fill = [&](int x0, int y0, int w, int h, Rgba color) {
        for (int y = y0; y < y0 + h; ++y) {
            for (int x = x0; x < x0 + w; ++x) {
                pixels[static_cast<std::size_t>(y * 20 + x)] = color;
            }
        }
    };
    fill(1, 1, 4, 4, kRed);
    fill(5, 1, 4, 4, kGreen);
    fill(1, 5, 4, 4, kBlue);
    fill(5, 5, 4, 4, kWhite);
    fill(11, 1, 8, 8, kGreen);
    fill(1, 11, 8, 8, kBlue);
    fill(11, 11, 8, 8, kYellow);
    WritePng(path, 20, 20, pixels);
}

TilesetSettings TestSheetSettings(AssetHandle texture) {
    TilesetSettings settings{};
    settings.Texture = texture;
    settings.TextureWidth = 20;
    settings.TextureHeight = 20;
    settings.TileWidth = 8;
    settings.TileHeight = 8;
    settings.Margin = 1;
    settings.Spacing = 2;
    return settings;
}

class TilemapFixture {
public:
    TilemapFixture() {
        std::mt19937_64 generator{std::random_device{}()};
        const fs::path parent = fs::temp_directory_path() / ("xora2d_tilemaps_" + std::to_string(generator() % 100000000));
        fs::create_directories(parent);
        m_parent = fs::weakly_canonical(parent);
        ProjectCreateOptions options{};
        options.Name = "TileGame";
        options.ParentDirectory = m_parent;
        const auto created = CreateProject(options);
        m_root = created.ProjectRoot;
        WriteTestSheet(m_root / "Assets/Textures/Sheet.png");
        Context.GetAssetManager().SetGraphicsResourcesEnabled(false);
        (void)Context.OpenProject(Project::Load(created.ProjectFile).Loaded);
        Texture = Context.GetAssetManager().RegisterTextureAtRelativePath("Textures/Sheet.png", "Sheet").Handle;

        // Tileset: tile 2 solid, tile 3 one-way, tiles 1 and 4 decorative.
        Tileset tileset;
        const auto build = tileset.Build("Terrain", TestSheetSettings(Texture),
                                         {TileProperties{.Id = 2, .Collision = TileCollision::Solid},
                                          TileProperties{.Id = 3, .Collision = TileCollision::OneWay}});
        Check(build.Success, "fixture tileset builds: " + build.Message);
        (void)TilesetSerializer::Save(tileset, m_root / "Assets/Tilesets/Terrain.tileset.yaml", false);
        TilesetHandle = Context.GetAssetManager().RegisterTilesetAtRelativePath("Tilesets/Terrain.tileset.yaml").Handle;
    }
    ~TilemapFixture() {
        Context.CloseProject();
        std::error_code ec;
        fs::remove_all(m_parent, ec);
    }
    TilemapFixture(const TilemapFixture&) = delete;
    TilemapFixture& operator=(const TilemapFixture&) = delete;

    [[nodiscard]] const fs::path& Root() const { return m_root; }
    Scene& EditScene() { return *Context.GetEditScene(); }
    AssetManager& Assets() { return Context.GetAssetManager(); }
    const Tileset* GetTileset() {
        (void)Assets().LoadTileset(TilesetHandle);
        return Assets().GetLoadedTileset(TilesetHandle).get();
    }

    Entity MakeTilemap(Scene& scene, const std::string& name = "Level") {
        Entity entity = scene.CreateEntity(name);
        auto& tilemap = entity.AddComponent<TilemapComponent>();
        tilemap.Tileset = TilesetHandle;
        tilemap.TileSize = {16, 16};
        tilemap.CreateDefaultLayers();
        return entity;
    }

    EditorContext Context;
    AssetHandle Texture{};
    AssetHandle TilesetHandle{};

private:
    fs::path m_parent;
    fs::path m_root;
};

// ---------------------------------------------------------------------------------------------------------------
// Tilesets
// ---------------------------------------------------------------------------------------------------------------

void TestTilesets() {
    XORA_LOG_INFO("TEST tilesets");
    TilemapFixture fixture;
    const Tileset* tileset = fixture.GetTileset();
    Check(tileset != nullptr, "tileset registered and loaded through the AssetManager");
    if (tileset == nullptr) {
        return;
    }
    Check(tileset->GetColumns() == 2 && tileset->GetRows() == 2 && tileset->GetTiles().size() == 4,
          "grid generation honours margin and spacing (2x2 tiles in 20x20)");
    const TileDefinition* first = tileset->FindTile(1);
    const TileDefinition* last = tileset->FindTile(4);
    Check(first != nullptr && first->Region == TileRegion{1, 1, 8, 8} && last != nullptr &&
              last->Region == TileRegion{11, 11, 8, 8},
          "tile regions skip the margin and the spacing");
    Check(tileset->GridTileId(1, 0) == 2 && tileset->GridTileId(0, 1) == 3 && tileset->FindTile(5) == nullptr &&
              tileset->FindTile(0) == nullptr,
          "stable row-major tile ids; unknown ids rejected");
    // v is bottom-up: tile 1 occupies image rows 1..8 of 20 -> v from (20-9)/20 to (20-1)/20.
    Check(first->UV == glm::vec4(1.0f / 20.0f, 11.0f / 20.0f, 9.0f / 20.0f, 19.0f / 20.0f), "tile UVs are exact");
    Check(tileset->FindTile(2)->Collision == TileCollision::Solid &&
              tileset->FindTile(3)->Collision == TileCollision::OneWay &&
              tileset->FindTile(1)->Collision == TileCollision::None,
          "per-tile collision configuration");

    // Validation.
    TilesetSettings settings = TestSheetSettings(fixture.Texture);
    Tileset scratch;
    settings.TileWidth = 0;
    Check(!scratch.Build("X", settings, {}).Success, "zero tile width rejected");
    settings = TestSheetSettings(fixture.Texture);
    settings.Spacing = -1;
    Check(!scratch.Build("X", settings, {}).Success, "negative spacing rejected");
    settings = TestSheetSettings(fixture.Texture);
    settings.TileWidth = 32;
    Check(!scratch.Build("X", settings, {}).Success, "tiles larger than the texture rejected");
    settings = TestSheetSettings(fixture.Texture);
    settings.Margin = 9;
    Check(!scratch.Build("X", settings, {}).Success, "margins leaving no room for a tile rejected");
    settings = TestSheetSettings(fixture.Texture);
    settings.TextureWidth = 0;
    Check(!scratch.Build("X", settings, {}).Success, "unknown texture size rejected");
    settings = TestSheetSettings(fixture.Texture);
    Check(!scratch.Build("X", settings, {TileProperties{.Id = 9}}).Success, "properties for non-existent tiles rejected");
    settings.Spacing = 0;
    settings.Margin = 0;
    Check(scratch.Build("X", settings, {}).Success && scratch.GetColumns() == 2 && scratch.GetTiles().size() == 4,
          "zero margin / spacing sheets also work");

    // Manual regions.
    settings = TestSheetSettings(fixture.Texture);
    settings.Mode = TilesetMode::Manual;
    Check(scratch.Build("Manual", settings,
                        {TileProperties{.Id = 7, .Region = {11, 1, 8, 8}}, TileProperties{.Id = 3, .Region = {0, 0, 20, 2}}})
                  .Success &&
              scratch.FindTile(7) != nullptr && scratch.FindTile(7)->Region == TileRegion{11, 1, 8, 8} &&
              scratch.GetTiles().size() == 2,
          "manual tile regions with explicit ids");
    Check(!scratch.Build("Manual", settings, {TileProperties{.Id = 1, .Region = {15, 15, 8, 8}}}).Success,
          "manual regions outside the texture rejected");

    // Serialization round trip.
    const std::string text = TilesetSerializer::Emit(*tileset);
    const auto parsed = TilesetSerializer::Parse(text, "terrain.tileset.yaml");
    Check(parsed.Success && parsed.Loaded->GetSettings() == tileset->GetSettings() &&
              parsed.Loaded->FindTile(2)->Collision == TileCollision::Solid && TilesetSerializer::Emit(*parsed.Loaded) == text,
          "tileset file round-trips");
    for (const char* bad : {"Tileset: [", "Foo: 1", "Tileset: {FormatVersion: 9, Name: X}",
                            "Tileset: {FormatVersion: 1, Name: X, TextureSize: [20, 20], TileSize: [8], Mode: Grid}",
                            "Tileset: {FormatVersion: 1, Name: X, TextureSize: [20, 20], TileSize: [8, 8], Mode: Hex}"}) {
        Check(!TilesetSerializer::Parse(bad, "bad.tileset.yaml").Success, std::string("malformed tileset rejected: ") + bad);
    }

    // Missing / resized textures.
    Tileset orphan;
    TilesetSettings orphanSettings = TestSheetSettings(AssetHandle(0xBADC0DE));
    (void)orphan.Build("Orphan", orphanSettings, {});
    const auto orphanLoad = TilesetSerializer::Parse(TilesetSerializer::Emit(orphan), "orphan", &fixture.Assets());
    Check(orphanLoad.Success && !orphanLoad.Warnings.empty(), "missing texture: tileset loads with a warning");
    fs::remove(fixture.Root() / "Assets/Textures/Sheet.png");
    WritePng(fixture.Root() / "Assets/Textures/Sheet.png", 4, 4, std::vector<Rgba>(16, kRed));
    fixture.Assets().UnloadAsset(fixture.TilesetHandle);
    Check(!fixture.Assets().LoadTileset(fixture.TilesetHandle).Success,
          "texture resized after import: load reports it instead of using wrong regions");

    // Handles survive rename / move.
    WriteTestSheet(fixture.Root() / "Assets/Textures/Sheet.png");
    const auto renamed =
        Xora2DEditor::RenameResourceEntry("Assets/Tilesets/Terrain.tileset.yaml", "Ground", fixture.Context.GetAssetRegistry());
    fixture.Assets().UnloadAsset(fixture.TilesetHandle);
    Check(renamed.Success &&
              fixture.Context.GetAssetRegistry().FindByHandle(fixture.TilesetHandle)->RelativePath ==
                  "Tilesets/Ground.tileset.yaml" &&
              fixture.Assets().LoadTileset(fixture.TilesetHandle).Success,
          "renaming a tileset keeps its handle");
}

// ---------------------------------------------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------------------------------------------

void TestTilemapData() {
    XORA_LOG_INFO("TEST tilemap data");
    TilemapComponent tilemap;
    tilemap.CreateDefaultLayers();
    Check(tilemap.Layers.size() == 4 && tilemap.Layers[0].Name == "Background" && tilemap.Layers[1].Collision &&
              tilemap.Layers[3].AboveSprites && tilemap.ActiveLayer == 1,
          "default layers: Background, Ground (collision), Decorations, Foreground (above sprites)");
    Check(sizeof(TileCell) == 8, "tile cells are 8 bytes");

    Check(tilemap.SetTile(1, {0, 0}, TileCell::Make(2)) && tilemap.GetTile(1, {0, 0}).Id == 2, "set / get a tile");
    Check(!tilemap.SetTile(1, {0, 0}, TileCell::Make(2)), "setting the same tile reports no change");
    Check(tilemap.SetTile(1, {-1, -1}, TileCell::Make(3)) && tilemap.GetTile(1, {-1, -1}).Id == 3 &&
              tilemap.GetTile(1, {-1, 0}).IsEmpty(),
          "negative coordinates");
    Check(ChunkOf({-1, -1}) == GridCoord{-1, -1} && ChunkOf({31, 32}) == GridCoord{0, 1} && ChunkOf({-32, 0}) == GridCoord{-1, 0} &&
              ChunkOf({-33, 0}) == GridCoord{-2, 0},
          "chunk math floors toward negative infinity");
    tilemap.SetTile(1, {31, 31}, TileCell::Make(4));
    tilemap.SetTile(1, {32, 31}, TileCell::Make(4));
    Check(tilemap.GetTileCount(1) == 4 && tilemap.Layers[1].Chunks.size() == 3, "tiles across chunk borders");
    Check(tilemap.ClearTile(1, {-1, -1}) && tilemap.Layers[1].Chunks.size() == 2 && tilemap.GetTileCount(1) == 3,
          "removing the last tile of a chunk frees the chunk");
    Check(!tilemap.SetTile(9, {0, 0}, TileCell::Make(1)) && tilemap.GetTile(-1, {0, 0}).IsEmpty(),
          "invalid layers are ignored safely");
    const auto bounds = tilemap.GetBounds(-1);
    Check(bounds.has_value() && bounds->first == GridCoord{0, 0} && bounds->second == GridCoord{32, 31}, "bounds");

    const TileCell flipped = TileCell::Make(5, true, false, 3);
    Check(flipped.FlipX() && !flipped.FlipY() && flipped.Rotation() == 3 && TileCell::Make(5, false, true, -1).Rotation() == 3,
          "flip and rotation flags");
    tilemap.Layers[0].Visible = false;
    tilemap.Layers[0].Opacity = 0.5f;
    Check(!tilemap.Layers[0].Visible && tilemap.Layers[0].Opacity == 0.5f, "layer visibility and opacity");

    const std::uint64_t revisionBefore = tilemap.Layers[1].Chunks.at(ChunkKey({0, 0})).Revision;
    tilemap.SetTile(1, {1, 0}, TileCell::Make(1));
    Check(tilemap.Layers[1].Chunks.at(ChunkKey({0, 0})).Revision > revisionBefore, "edits bump the chunk revision");

    Check(tilemap.LocalToCell({-0.5f, 15.9f}) == GridCoord{-1, 0} && tilemap.LocalToCell({16.0f, -16.0f}) == GridCoord{1, -1} &&
              tilemap.CellToLocal({-2, 3}) == glm::vec2(-32.0f, 48.0f),
          "local pixel <-> integer cell conversion");
    tilemap.TileSize = {24, 12};
    Check(tilemap.LocalToCell({25.0f, 13.0f}) == GridCoord{1, 1}, "rectangular tiles");
}

// ---------------------------------------------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------------------------------------------

void TestEditing() {
    XORA_LOG_INFO("TEST tilemap editing");
    TilemapComponent tilemap;
    tilemap.CreateDefaultLayers();
    TilemapEditHistory history;
    const std::int32_t ground = 1;

    // Brush stroke: dragging over the same cell repeatedly paints once.
    TilemapStroke stroke(tilemap, ground, "Brush");
    std::size_t changed = 0;
    for (const GridCoord cell : {GridCoord{0, 0}, GridCoord{0, 0}, GridCoord{1, 0}, GridCoord{1, 0}, GridCoord{2, 0}}) {
        changed += stroke.Paint(cell, TileCell::Make(2)) ? 1 : 0;
    }
    TilemapCommand brush = stroke.Finish();
    Check(changed == 3 && brush.Changes.size() == 3 && tilemap.GetTileCount(ground) == 3,
          "brush drag paints each cell once (duplicate events ignored)");
    history.Record(std::move(brush));

    TilemapStroke eraser(tilemap, ground, "Erase");
    (void)eraser.Paint({1, 0}, TileCell{});
    history.Record(eraser.Finish());
    Check(tilemap.GetTile(ground, {1, 0}).IsEmpty() && tilemap.GetTileCount(ground) == 2, "eraser removes tiles");

    auto rect = FillRect(tilemap, ground, TileRect::FromCorners({5, 5}, {2, 3}), TileCell::Make(4));
    Check(rect.Success && rect.ChangedCells == 12 && tilemap.GetTile(ground, {2, 3}).Id == 4 &&
              tilemap.GetTile(ground, {5, 5}).Id == 4,
          "rectangle fill (corners in any order)");
    history.Record(std::move(rect.Command));

    // Bucket fill inside a closed border.
    TilemapComponent bucketMap;
    bucketMap.CreateDefaultLayers();
    (void)FillRect(bucketMap, ground, TileRect::FromCorners({0, 0}, {9, 9}), TileCell::Make(2));
    (void)FillRect(bucketMap, ground, TileRect::FromCorners({1, 1}, {8, 8}), TileCell{});
    auto bucket = BucketFill(bucketMap, ground, {4, 4}, TileCell::Make(3));
    Check(bucket.Success && bucket.ChangedCells == 64 && bucketMap.GetTile(ground, {1, 8}).Id == 3 &&
              bucketMap.GetTile(ground, {0, 0}).Id == 2,
          "bucket fill fills the enclosed area only");
    auto replace = BucketFill(bucketMap, ground, {0, 0}, TileCell::Make(4));
    Check(replace.Success && replace.ChangedCells == 36, "bucket fill replaces a connected region of one tile");
    Check(!BucketFill(bucketMap, ground, {50, 50}, TileCell::Make(1)).Success && bucketMap.GetTile(ground, {50, 50}).IsEmpty(),
          "bucket fill outside the painted area is refused (no unbounded fill)");
    Check(!BucketFill(bucketMap, ground, {0, 0}, TileCell::Make(4)).Success, "bucket fill with the same tile is a no-op");

    // Eyedropper.
    std::int32_t pickedLayer = -1;
    Check(PickTile(tilemap, -1, {3, 4}, &pickedLayer).Id == 4 && pickedLayer == ground &&
              PickTile(tilemap, 0, {3, 4}).IsEmpty(),
          "eyedropper picks the topmost visible tile");

    // Selection: copy / paste / move.
    const TileClipboard clipboard = CopyTiles(tilemap, ground, TileRect::FromCorners({2, 3}, {3, 4}));
    Check(clipboard.Width == 2 && clipboard.Height == 2 && clipboard.Cells[0].Id == 4, "copy a rectangular selection");
    auto paste = PasteTiles(tilemap, ground, {-10, -10}, clipboard);
    Check(paste.Success && paste.ChangedCells == 4 && tilemap.GetTile(ground, {-9, -9}).Id == 4, "paste at another place");
    history.Record(std::move(paste.Command));
    auto move = MoveTiles(tilemap, ground, TileRect::FromCorners({-10, -10}, {-9, -9}), {0, 20});
    Check(move.Success && tilemap.GetTile(ground, {-10, -10}).IsEmpty() && tilemap.GetTile(ground, {-10, 10}).Id == 4,
          "move a selected region");
    history.Record(std::move(move.Command));

    // Undo / redo.
    const std::size_t before = tilemap.GetTileCount(ground);
    Check(history.Undo(tilemap) && tilemap.GetTile(ground, {-10, -10}).Id == 4 && tilemap.GetTile(ground, {-10, 10}).IsEmpty(),
          "undo move");
    Check(history.Undo(tilemap) && tilemap.GetTile(ground, {-10, -10}).IsEmpty(), "undo paste");
    Check(history.Undo(tilemap) && tilemap.GetTile(ground, {3, 4}).IsEmpty(), "undo rectangle fill");
    Check(history.Undo(tilemap) && tilemap.GetTile(ground, {1, 0}).Id == 2, "undo erase");
    Check(history.Undo(tilemap) && tilemap.GetTileCount(ground) == 0 && !history.CanUndo(), "undo brush");
    for (int i = 0; i < 5; ++i) {
        (void)history.Redo(tilemap);
    }
    Check(tilemap.GetTileCount(ground) == before && !history.CanRedo(), "redo restores everything");
    (void)history.Undo(tilemap);
    TilemapStroke branch(tilemap, ground, "Brush");
    (void)branch.Paint({100, 100}, TileCell::Make(1));
    history.Record(branch.Finish());
    Check(!history.CanRedo(), "a new edit clears the redo stack");

    // Undo memory is proportional to the edit, not the map.
    TilemapComponent big;
    big.CreateDefaultLayers();
    (void)FillRect(big, ground, TileRect::FromCorners({0, 0}, {199, 99}), TileCell::Make(2));
    TilemapEditHistory bigHistory;
    TilemapStroke single(big, ground, "Brush");
    (void)single.Paint({5, 5}, TileCell::Make(3));
    bigHistory.Record(single.Finish());
    Check(bigHistory.ApproximateBytes() < 1024, "single-tile undo on a 20,000-tile map stores a diff (" +
                                                    std::to_string(bigHistory.ApproximateBytes()) + " bytes)");

    // Locked / hidden layers.
    tilemap.Layers[ground].Locked = true;
    const std::size_t lockedCount = tilemap.GetTileCount(ground);
    TilemapStroke lockedStroke(tilemap, ground, "Brush");
    Check(!lockedStroke.Paint({50, 50}, TileCell::Make(1)) && !FillRect(tilemap, ground, TileRect{{0, 0}, {3, 3}}, TileCell{}).Success &&
              !BucketFill(tilemap, ground, {0, 0}, TileCell::Make(1)).Success &&
              !PasteTiles(tilemap, ground, {0, 0}, clipboard).Success && tilemap.GetTileCount(ground) == lockedCount,
          "locked layers reject every paint operation");
    tilemap.Layers[ground].Locked = false;
    tilemap.Layers[ground].Visible = false;
    Check(!PaintTiles(tilemap, ground, {{60, 60}}, TileCell::Make(1)).Success && tilemap.GetTile(ground, {60, 60}).IsEmpty(),
          "hidden layers reject paint operations");
    tilemap.Layers[ground].Visible = true;

    // Layer management with undo.
    TilemapEditHistory layers;
    auto added = AddLayer(tilemap, "Water", 0);
    layers.Record(std::move(added.Command));
    Check(tilemap.Layers.size() == 5 && tilemap.Layers[0].Name == "Water", "add layer");
    auto renamed = RenameLayer(tilemap, 0, "Lava");
    layers.Record(std::move(renamed.Command));
    auto moved = MoveLayer(tilemap, 0, 4);
    layers.Record(std::move(moved.Command));
    Check(tilemap.Layers[4].Name == "Lava", "rename and reorder layers");
    const std::int32_t groundIndex = tilemap.FindLayer("Ground");
    auto duplicated = DuplicateLayer(tilemap, groundIndex);
    layers.Record(std::move(duplicated.Command));
    Check(tilemap.Layers[static_cast<std::size_t>(groundIndex + 1)].Name == "Ground Copy" &&
              tilemap.GetTileCount(groundIndex + 1) == tilemap.GetTileCount(groundIndex) && tilemap.GetTileCount(groundIndex) > 0,
          "duplicate layer copies its tiles");
    const std::size_t groundTiles = tilemap.GetTileCount(groundIndex);
    auto removed = RemoveLayer(tilemap, groundIndex);
    layers.Record(std::move(removed.Command));
    Check(tilemap.FindLayer("Ground") < 0, "remove layer");
    Check(layers.Undo(tilemap) && tilemap.FindLayer("Ground") == groundIndex && tilemap.GetTileCount(groundIndex) == groundTiles,
          "undo layer removal restores its tiles");
    while (layers.Undo(tilemap)) {
    }
    Check(tilemap.Layers.size() == 4 && tilemap.FindLayer("Water") < 0 && tilemap.FindLayer("Lava") < 0,
          "undo all layer operations");
}

// ---------------------------------------------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------------------------------------------

void TestRenderingGeometry() {
    XORA_LOG_INFO("TEST tilemap render geometry");
    TilemapFixture fixture;
    const Tileset* tileset = fixture.GetTileset();
    const glm::vec4 uv{0.0f, 0.0f, 1.0f, 1.0f};
    glm::vec2 corners[4];
    BuildTileUVs(uv, 0, corners);
    Check(corners[0] == glm::vec2(0, 0) && corners[2] == glm::vec2(1, 1), "unflipped UV corners (BL, BR, TR, TL)");
    BuildTileUVs(uv, kTileFlipX, corners);
    Check(corners[0] == glm::vec2(1, 0) && corners[1] == glm::vec2(0, 0), "horizontal flip mirrors U");
    BuildTileUVs(uv, kTileFlipY, corners);
    Check(corners[0] == glm::vec2(0, 1) && corners[3] == glm::vec2(0, 0), "vertical flip mirrors V");
    BuildTileUVs(uv, static_cast<std::uint8_t>(1 << kTileRotationShift), corners);
    Check(corners[1] == glm::vec2(0, 0) && corners[2] == glm::vec2(1, 0), "quarter turn rotates the UV corners");
    BuildTileUVs(uv, static_cast<std::uint8_t>(2 << kTileRotationShift), corners);
    glm::vec2 both[4];
    BuildTileUVs(uv, kTileFlipX | kTileFlipY, both);
    Check(corners[0] == both[0] && corners[2] == both[2], "half turn equals flipping both axes");

    TilemapComponent tilemap;
    tilemap.CreateDefaultLayers();
    tilemap.TileSize = {16, 16};
    const bool emptyRebuild = [&] {
        bool rebuilt = false;
        (void)GetChunkGeometry(tilemap, 1, ChunkKey({0, 0}), tileset, &rebuilt);
        return rebuilt;
    }();
    Check(emptyRebuild && GetChunkGeometry(tilemap, 1, ChunkKey({0, 0}), tileset).Vertices.empty(),
          "empty chunk: no geometry");
    tilemap.SetTile(1, {2, 1}, TileCell::Make(1));
    tilemap.SetTile(1, {3, 1}, TileCell::Make(99)); // invalid id
    bool rebuilt = false;
    const TileChunkGeometry& geometry = GetChunkGeometry(tilemap, 1, ChunkKey({0, 0}), tileset, &rebuilt);
    Check(rebuilt && geometry.Vertices.size() == 4 && geometry.Vertices[0].Position == glm::vec2(32.0f, 16.0f) &&
              geometry.Vertices[2].Position == glm::vec2(48.0f, 32.0f) &&
              geometry.Vertices[0].TexCoord == glm::vec2(tileset->FindTile(1)->RenderUV.x, tileset->FindTile(1)->RenderUV.y) &&
              tileset->FindTile(1)->RenderUV.x > tileset->FindTile(1)->UV.x &&
              tileset->FindTile(1)->RenderUV.z < tileset->FindTile(1)->UV.z,
          "chunk geometry places tiles on the grid with their tileset UVs; invalid ids are skipped");
    (void)GetChunkGeometry(tilemap, 1, ChunkKey({0, 0}), tileset, &rebuilt);
    Check(!rebuilt, "unchanged chunk geometry is reused");
    tilemap.SetTile(1, {40, 0}, TileCell::Make(2)); // another chunk
    (void)GetChunkGeometry(tilemap, 1, ChunkKey({0, 0}), tileset, &rebuilt);
    Check(!rebuilt, "editing another chunk does not invalidate this one");
    tilemap.SetTile(1, {2, 2}, TileCell::Make(2));
    (void)GetChunkGeometry(tilemap, 1, ChunkKey({0, 0}), tileset, &rebuilt);
    Check(rebuilt, "editing the chunk invalidates its geometry");
    tilemap.TileSize = {32, 32};
    (void)GetChunkGeometry(tilemap, 1, ChunkKey({0, 0}), tileset, &rebuilt);
    Check(rebuilt, "changing the tile size rebuilds geometry");
}

std::uint32_t ReadPixel(int x, int y) {
    std::uint8_t pixel[4] = {};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    return (static_cast<std::uint32_t>(pixel[0]) << 16) | (static_cast<std::uint32_t>(pixel[1]) << 8) | pixel[2];
}

void TestRenderingGpu() {
    XORA_LOG_INFO("TEST tilemap GPU rendering");
    TilemapFixture fixture;
    WindowProperties props{.title = "TilemapRender", .width = 64, .height = 64, .enableOpenGL = true};
    Window window(props);
    std::unique_ptr<GraphicsContext> gl = window.CreateGraphicsContext();
    gl->MakeCurrent();
    auto target = ColorRenderTarget::Create();
    if (!target->Resize(64, 64)) {
        Check(false, "render target");
        return;
    }
    Renderer2D renderer;
    renderer.Init();
    fixture.Assets().SetGraphicsResourcesEnabled(true);

    Scene& scene = fixture.EditScene();
    scene.CreateEntity("Camera").AddComponent<CameraComponent>().Primary = true;
    Entity map = fixture.MakeTilemap(scene);
    auto& tilemap = map.GetComponent<TilemapComponent>();
    tilemap.TileSize = {32, 32};
    tilemap.SetTile(1, {0, 0}, TileCell::Make(1)); // quadrant tile at pixels 0..32
    tilemap.SetTile(1, {1, 0}, TileCell::Make(1, true, false));
    tilemap.SetTile(1, {0, 1}, TileCell::Make(2));
    tilemap.SetTile(3, {0, 1}, TileCell::Make(4)); // Foreground (above sprites, later layer) over green

    const auto render = [&]() {
        scene.SetViewportSize(64.0f, 64.0f);
        target->Bind();
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        renderer.ResetStats();
        scene.OnRender(renderer);
    };
    render();
    const std::uint32_t topLeft = ReadPixel(8, 24);
    const std::uint32_t topRight = ReadPixel(24, 24);
    const std::uint32_t bottomLeft = ReadPixel(8, 8);
    const std::uint32_t bottomRight = ReadPixel(24, 8);
    const std::uint32_t flippedTopLeft = ReadPixel(40, 24);
    const std::uint32_t flippedTopRight = ReadPixel(56, 24);
    const std::uint32_t upper = ReadPixel(16, 48);
    target->Unbind();
    XORA_LOG_INFO("  pixels TL={:06x} TR={:06x} BL={:06x} BR={:06x} flipTL={:06x} flipTR={:06x} upper={:06x}", topLeft,
                  topRight, bottomLeft, bottomRight, flippedTopLeft, flippedTopRight, upper);
    Check(topLeft == 0xFF0000 && topRight == 0x00FF00 && bottomLeft == 0x0000FF && bottomRight == 0xFFFFFF,
          "tile UVs map the texture region upright (no margin / spacing bleeding)");
    Check(flippedTopLeft == 0x00FF00 && flippedTopRight == 0xFF0000, "horizontally flipped tile renders mirrored");
    Check(upper == 0xFFFF00, "upper layer draws over lower layers");
    Check(scene.GetTilemapRenderStats().ChunksRebuilt >= 1 && scene.GetTilemapRenderStats().TilesDrawn == 4,
          "first frame builds chunk geometry");

    render();
    Check(scene.GetTilemapRenderStats().ChunksRebuilt == 0 && scene.GetTilemapRenderStats().TilesDrawn == 4,
          "frames without edits rebuild no geometry");
    target->Unbind();

    tilemap.Layers[3].Visible = false;
    render();
    const std::uint32_t hidden = ReadPixel(16, 48);
    target->Unbind();
    Check(hidden == 0x00FF00, "hidden layers are not drawn");
    tilemap.Layers[3].Visible = true;
    tilemap.Layers[3].Opacity = 0.0f;
    render();
    Check(ReadPixel(16, 48) == 0x00FF00, "zero-opacity layers are skipped");
    target->Unbind();
    tilemap.Layers[3].Opacity = 1.0f;

    // Batching: a 120 x 100 map (12,000 tiles) is drawn in a couple of draw calls, not one per tile.
    scene.FindEntityByName("Camera").GetComponent<CameraComponent>().Zoom = 0.5f; // 128x128 view
    TilemapComponent& big = fixture.MakeTilemap(scene, "Big").GetComponent<TilemapComponent>();
    big.TileSize = {1, 1}; // the whole 120x100 map is in view
    (void)FillRect(big, 1, TileRect::FromCorners({0, 0}, {119, 99}), TileCell::Make(2));
    render();
    target->Unbind();
    const Renderer2DStats& stats = renderer.GetStats();
    Check(stats.drawCalls <= 3 && stats.quadCount >= 12'000,
          "12,000 tiles batched into " + std::to_string(stats.drawCalls) + " draw call(s)");

    // Culling: chunks outside the view are skipped.
    auto& far = fixture.MakeTilemap(scene, "Far").GetComponent<TilemapComponent>();
    far.SetTile(1, {1000, 1000}, TileCell::Make(2));
    render();
    target->Unbind();
    Check(scene.GetTilemapRenderStats().ChunksCulled >= 1, "off-screen chunks are culled");

    // Empty tilemap.
    fixture.MakeTilemap(scene, "Empty");
    render();
    target->Unbind();
    Check(true, "empty tilemaps render without errors");
    fixture.Assets().UnloadAll();
    renderer.Shutdown();
}

// ---------------------------------------------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------------------------------------------

Entity MakeBox(Scene& scene, glm::vec2 position, glm::vec2 velocity = {0.0f, 0.0f}) {
    Entity box = scene.CreateEntity("Box");
    box.GetComponent<TransformComponent>().Translation = position;
    box.AddComponent<RigidBody2DComponent>().FixedRotation = true;
    box.AddComponent<BoxCollider2DComponent>().Size = {14.0f, 14.0f};
    (void)velocity;
    return box;
}

void Simulate(Scene& scene, int frames) {
    for (int frame = 0; frame < frames; ++frame) {
        scene.OnUpdate(1.0f / 60.0f);
    }
}

void TestPhysics() {
    XORA_LOG_INFO("TEST tilemap collision");
    TilemapFixture fixture;
    auto scene = Scene::Create();
    scene->SetAssetManager(&fixture.Assets());
    Entity map = fixture.MakeTilemap(*scene);
    auto& tilemap = map.GetComponent<TilemapComponent>();
    // Solid floor (tile 2) 20 cells wide at y = 0, decorative tiles (tile 1) at y = 10.
    (void)FillRect(tilemap, 1, TileRect::FromCorners({0, 0}, {19, 0}), TileCell::Make(2));
    (void)FillRect(tilemap, 1, TileRect::FromCorners({0, 10}, {19, 10}), TileCell::Make(1));

    Entity box = MakeBox(*scene, {100.0f, 250.0f});
    Simulate(*scene, 180);
    const float restY = box.GetComponent<TransformComponent>().Translation.y;
    Check(scene->GetPhysicsWorld().HasTilemapBody(map), "tilemap gets a static body");
    Check(restY > 16.0f && restY < 30.0f, "box passes decorative tiles and rests on solid tiles (y=" +
                                              std::to_string(restY) + ")");
    Check(scene->GetPhysicsWorld().GetTilemapFixtureCount(map) == 1, "a 20-tile floor is merged into one fixture");
    Check(scene->GetPhysicsWorld().GetTilemapBodyCount() == 1, "one body per tilemap, not per tile");

    // Merging: an L shape needs two rectangles; chunk borders split rectangles.
    TilemapComponent shapes;
    shapes.CreateDefaultLayers();
    (void)FillRect(shapes, 1, TileRect::FromCorners({0, 0}, {9, 2}), TileCell::Make(2));
    (void)FillRect(shapes, 1, TileRect::FromCorners({0, 3}, {2, 9}), TileCell::Make(2));
    const auto getTileset = [&](AssetHandle) { return fixture.GetTileset(); };
    shapes.Tileset = fixture.TilesetHandle;
    Check(BuildChunkCollisionRects(shapes, ChunkKey({0, 0}), getTileset).size() == 2, "L shape merges into 2 rectangles");
    TilemapComponent mixed;
    mixed.CreateDefaultLayers();
    (void)FillRect(mixed, 1, TileRect::FromCorners({0, 0}, {3, 0}), TileCell::Make(2));
    (void)FillRect(mixed, 1, TileRect::FromCorners({4, 0}, {7, 0}), TileCell::Make(3));
    (void)FillRect(mixed, 1, TileRect::FromCorners({8, 0}, {11, 0}), TileCell::Make(1));
    const auto mixedRects = BuildChunkCollisionRects(mixed, ChunkKey({0, 0}), getTileset);
    Check(mixedRects.size() == 2 && mixedRects[0].Kind == TileCollision::Solid && mixedRects[1].Kind == TileCollision::OneWay &&
              mixedRects[0].Width == 4 && mixedRects[1].Width == 4,
          "solid and one-way tiles merge separately; decorative tiles have no collision");
    mixed.Layers[1].Collision = false;
    Check(CollectCollisionChunks(mixed).empty(), "layers without collision produce no geometry");

    // Rebuild only what changed.
    const std::uint64_t rebuildsBefore = scene->GetPhysicsWorld().GetTilemapChunkRebuildCount();
    Simulate(*scene, 10);
    Check(scene->GetPhysicsWorld().GetTilemapChunkRebuildCount() == rebuildsBefore,
          "no collision rebuild while tiles do not change");
    tilemap.SetTile(1, {100, 0}, TileCell::Make(2)); // new chunk
    Simulate(*scene, 1);
    Check(scene->GetPhysicsWorld().GetTilemapChunkRebuildCount() == rebuildsBefore + 1 &&
              scene->GetPhysicsWorld().GetTilemapFixtureCount(map) == 2,
          "adding a tile rebuilds only its chunk");
    tilemap.SetTile(1, {1, 10}, TileCell::Make(4)); // decorative tile in a collision layer chunk
    Simulate(*scene, 1);
    Check(scene->GetPhysicsWorld().GetTilemapChunkRebuildCount() == rebuildsBefore + 2,
          "any change in a collision chunk triggers that chunk's rebuild");
    tilemap.Layers[0].Chunks.size();
    tilemap.SetTile(0, {5, 5}, TileCell::Make(2)); // background layer has no collision
    Simulate(*scene, 1);
    Check(scene->GetPhysicsWorld().GetTilemapChunkRebuildCount() == rebuildsBefore + 2,
          "edits on non-collision layers never rebuild collision");

    // Erasing the floor under the box lets it fall.
    (void)FillRect(tilemap, 1, TileRect::FromCorners({0, 0}, {19, 0}), TileCell{});
    Simulate(*scene, 60);
    Check(box.GetComponent<TransformComponent>().Translation.y < restY - 20.0f,
          "collision follows runtime edits (box falls once the floor is erased)");

    // One-way platforms.
    auto oneWayScene = Scene::Create();
    oneWayScene->SetAssetManager(&fixture.Assets());
    Entity platforms = fixture.MakeTilemap(*oneWayScene);
    (void)FillRect(platforms.GetComponent<TilemapComponent>(), 1, TileRect::FromCorners({0, 5}, {19, 5}), TileCell::Make(3));
    Entity fromAbove = MakeBox(*oneWayScene, {64.0f, 200.0f});
    Entity fromBelow = MakeBox(*oneWayScene, {200.0f, 20.0f});
    fromBelow.GetComponent<RigidBody2DComponent>().GravityScale = -1.0f; // "jumps" upward through the platform
    Simulate(*oneWayScene, 180);
    const float aboveY = fromAbove.GetComponent<TransformComponent>().Translation.y;
    const float belowY = fromBelow.GetComponent<TransformComponent>().Translation.y;
    Check(aboveY > 96.0f && aboveY < 110.0f, "one-way platform holds a body landing from above (y=" + std::to_string(aboveY) + ")");
    Check(belowY > 112.0f, "one-way platform lets a body pass from below (y=" + std::to_string(belowY) + ")");

    // Cleanup.
    scene->DestroyEntity(map);
    Check(scene->GetPhysicsWorld().GetTilemapBodyCount() == 0, "destroying the tilemap entity destroys its body");
    Entity again = fixture.MakeTilemap(*scene, "Again");
    again.GetComponent<TilemapComponent>().SetTile(1, {0, 0}, TileCell::Make(2));
    Simulate(*scene, 1);
    again.RemoveComponent<TilemapComponent>();
    Simulate(*scene, 1);
    Check(scene->GetPhysicsWorld().GetTilemapBodyCount() == 0, "removing the component destroys its body");
    Entity disabled = fixture.MakeTilemap(*scene, "Disabled");
    disabled.GetComponent<TilemapComponent>().SetTile(1, {0, 0}, TileCell::Make(2));
    disabled.GetComponent<TilemapComponent>().Collision.Enabled = false;
    Simulate(*scene, 1);
    Check(!scene->GetPhysicsWorld().HasTilemapBody(disabled), "collision can be switched off per tilemap");
}

// ---------------------------------------------------------------------------------------------------------------
// Serialization, prefabs, runtime, Python
// ---------------------------------------------------------------------------------------------------------------

void TestSerialization() {
    XORA_LOG_INFO("TEST tilemap serialization");
    TilemapFixture fixture;
    Entity map = fixture.MakeTilemap(fixture.EditScene());
    const UUID mapId = map.GetUUID();
    auto& tilemap = map.GetComponent<TilemapComponent>();
    tilemap.TileSize = {24, 12};
    tilemap.SetTile(1, {0, 0}, TileCell::Make(2));
    tilemap.SetTile(1, {-40, -3}, TileCell::Make(3, true, true, 1));
    (void)FillRect(tilemap, 0, TileRect::FromCorners({0, 0}, {63, 3}), TileCell::Make(1));
    tilemap.Layers[2].Locked = true;
    tilemap.Layers[2].Opacity = 0.25f;
    tilemap.Layers[3].Visible = false;
    tilemap.Collision.Friction = 0.1f;
    tilemap.Collision.MaskBits = 0x00F0;

    // RLE encoding.
    TileChunk chunk{};
    chunk.Cells[0] = TileCell::Make(5);
    chunk.Cells[1] = TileCell::Make(5);
    chunk.Cells[2] = TileCell::Make(7, true);
    chunk.Count = 3;
    const std::string encoded = TilemapSerializer::EncodeChunk(chunk);
    TileChunk decoded{};
    std::string error;
    Check(encoded == "5x2 7:1 0x1021" && TilemapSerializer::DecodeChunk(encoded, decoded, error) &&
              decoded.Cells[2] == chunk.Cells[2] && decoded.Count == 3,
          "chunks are run-length encoded (" + encoded + ")");
    Check(!TilemapSerializer::DecodeChunk("5x2000", decoded, error) && !TilemapSerializer::DecodeChunk("abc", decoded, error) &&
              !TilemapSerializer::DecodeChunk("1x10", decoded, error) && !TilemapSerializer::DecodeChunk("1:99x1024", decoded, error),
          "malformed chunk data rejected");

    const auto save = fixture.Context.SaveSceneAs("Scenes/Tiles.scene.yaml");
    Check(save.Success, "scene with a tilemap saved");
    const std::string yaml = std::ifstream(fixture.Root() / "Scenes/Tiles.scene.yaml") ?
        [&] { std::ostringstream s; s << std::ifstream(fixture.Root() / "Scenes/Tiles.scene.yaml").rdbuf(); return s.str(); }() : "";
    Check(yaml.find("TilemapComponent:") != std::string::npos && yaml.find(fixture.TilesetHandle.ToString()) != std::string::npos &&
              yaml.find(".png") == std::string::npos && yaml.size() < 8000,
          "scene stores tileset handles and compact tile data, no texture data (" + std::to_string(yaml.size()) + " bytes)");

    const auto reopen = fixture.Context.OpenSceneFile(fixture.Root() / "Scenes/Tiles.scene.yaml");
    const Entity loaded = fixture.EditScene().FindEntityByUUID(mapId);
    Check(reopen.Success && loaded.IsValid() && loaded.HasComponent<TilemapComponent>(), "scene reloaded");
    if (loaded.IsValid() && loaded.HasComponent<TilemapComponent>()) {
        const auto& restored = loaded.GetComponent<TilemapComponent>();
        Check(restored.Tileset == fixture.TilesetHandle && restored.TileSize == glm::ivec2(24, 12) &&
                  restored.Layers.size() == 4 && restored.GetTileCount(0) == 256 && restored.GetTile(1, {0, 0}).Id == 2,
              "tileset handle, tile size, layers and tiles restored");
        Check(restored.GetTile(1, {-40, -3}) == TileCell::Make(3, true, true, 1), "negative coordinates and flags restored");
        Check(restored.Layers[2].Locked && restored.Layers[2].Opacity == 0.25f && !restored.Layers[3].Visible &&
                  restored.Layers[1].Collision && restored.Layers[3].AboveSprites,
              "layer configuration restored");
        Check(restored.Collision.Friction == 0.1f && restored.Collision.MaskBits == 0x00F0, "collision settings restored");
    }

    // Transactional load: a corrupt tilemap leaves the open scene untouched.
    std::string corrupt = yaml;
    const std::size_t tilesAt = corrupt.find("Tiles: ");
    corrupt.replace(tilesAt, 7, "Tiles: 1x9999 ");
    std::ofstream(fixture.Root() / "Scenes/Corrupt.scene.yaml") << corrupt;
    const std::size_t entitiesBefore = fixture.EditScene().GetEntityCount();
    auto staging = Scene::Create();
    const auto corruptLoad =
        SceneSerializer::Deserialize(fixture.EditScene(), fixture.Root() / "Scenes/Corrupt.scene.yaml", &fixture.Assets());
    Check(!corruptLoad.Success && fixture.EditScene().GetEntityCount() == entitiesBefore,
          "malformed tile data fails the load without touching the scene (" + corruptLoad.Message + ")");

    // Invalid ids and missing tilesets are warnings.
    fixture.EditScene().FindEntityByUUID(mapId).GetComponent<TilemapComponent>().SetTile(1, {5, 5}, TileCell::Make(77));
    (void)fixture.Context.SaveSceneAs("Scenes/Invalid.scene.yaml");
    auto invalid = Scene::Create();
    const auto invalidLoad = SceneSerializer::Deserialize(*invalid, fixture.Root() / "Scenes/Invalid.scene.yaml", &fixture.Assets());
    Check(invalidLoad.Success && invalidLoad.Warnings.size() == 1 &&
              invalidLoad.Warnings[0].find("does not define") != std::string::npos &&
              invalid->FindEntityByUUID(mapId).GetComponent<TilemapComponent>().GetTile(1, {5, 5}).Id == 77,
          "unknown tile ids load with a warning and are kept");
    (void)fixture.Context.GetAssetRegistry().RemoveAsset(fixture.TilesetHandle);
    fixture.Assets().UnloadAsset(fixture.TilesetHandle);
    auto missing = Scene::Create();
    const auto missingLoad = SceneSerializer::Deserialize(*missing, fixture.Root() / "Scenes/Invalid.scene.yaml", &fixture.Assets());
    Check(missingLoad.Success && !missingLoad.Warnings.empty() &&
              missing->FindEntityByUUID(mapId).GetComponent<TilemapComponent>().GetTotalTileCount() > 0,
          "missing tileset: scene loads with a warning and keeps the tiles");

    // Tilemap asset files.
    std::string assetError;
    const fs::path assetPath = fixture.Root() / "Assets/Tilemaps/Level.tilemap.yaml";
    const TilemapComponent& current = invalid->FindEntityByUUID(mapId).GetComponent<TilemapComponent>();
    Check(TilemapSerializer::SaveAsset(current, "Level", assetPath, false, assetError), "tilemap asset saved");
    Check(!TilemapSerializer::SaveAsset(current, "Level", assetPath, false, assetError), "tilemap assets are not overwritten silently");
    TilemapComponent fromAsset;
    std::string assetName;
    Check(TilemapSerializer::LoadAsset(assetPath, fromAsset, assetName, assetError) && assetName == "Level" &&
              fromAsset.GetTotalTileCount() == current.GetTotalTileCount() && fromAsset.Layers.size() == 4,
          "tilemap asset loads");
    Check(fixture.Assets().RegisterTilemapAtRelativePath("Tilemaps/Level.tilemap.yaml").Success, "tilemap asset registered");
}

void TestPrefabAndRuntime() {
    XORA_LOG_INFO("TEST tilemap prefabs and runtime");
    TilemapFixture fixture;
    Entity map = fixture.MakeTilemap(fixture.EditScene());
    auto& tilemap = map.GetComponent<TilemapComponent>();
    (void)FillRect(tilemap, 1, TileRect::FromCorners({0, 0}, {9, 0}), TileCell::Make(2));
    tilemap.SetTile(3, {2, 2}, TileCell::Make(4, false, true));

    // Prefab with a tilemap.
    const auto prefab = CreatePrefabFromEntities({map}, {.Name = "Room", .LinkSourceEntities = false}, fixture.Assets());
    Check(prefab.Success, "prefab created from a tilemap entity");
    const auto instance = InstantiatePrefab(fixture.EditScene(), prefab.Handle, fixture.Assets());
    Check(instance.Success && instance.Entities.front().HasComponent<TilemapComponent>() &&
              instance.Entities.front().GetComponent<TilemapComponent>().GetTileCount(1) == 10 &&
              instance.Entities.front().GetComponent<TilemapComponent>().GetTile(3, {2, 2}) == TileCell::Make(4, false, true) &&
              instance.Entities.front().GetComponent<TilemapComponent>().Tileset == fixture.TilesetHandle,
          "prefab instance restores tiles, layers and tileset handle");
    Entity roomInstance = instance.Entities.front();
    const auto before = CapturePrefabProperties(roomInstance);
    roomInstance.GetComponent<TilemapComponent>().SetTile(1, {0, 1}, TileCell::Make(2));
    (void)RecordPrefabEdits(roomInstance, before, fixture.Assets().GetLoadedPrefab(prefab.Handle).get());
    Check(HasPrefabOverride(roomInstance, "Tilemap"), "tile edits on an instance are tracked as a Tilemap override");

    // Play mode.
    const auto editBefore = TilemapSerializer::EncodeChunk(tilemap.Layers[1].Chunks.at(ChunkKey({0, 0})));
    Check(fixture.Context.StartPlayMode(), "play started");
    const auto runtime = fixture.Context.GetScene();
    Entity runtimeMap = runtime->FindEntityByUUID(map.GetUUID());
    Check(runtimeMap.IsValid() && runtimeMap.GetComponent<TilemapComponent>().GetTileCount(1) == 10,
          "runtime copy has the tilemap data");
    fixture.Context.UpdatePlayMode(1.0f / 60.0f);
    Check(runtime->GetPhysicsWorld().HasTilemapBody(runtimeMap) && !fixture.EditScene().GetPhysicsWorld().HasTilemapBody(map),
          "collision exists only in the runtime scene");
    runtimeMap.GetComponent<TilemapComponent>().ClearLayer(1);
    fixture.Context.UpdatePlayMode(1.0f / 60.0f);
    Check(runtime->GetPhysicsWorld().GetTilemapFixtureCount(runtimeMap) == 0 && tilemap.GetTileCount(1) == 10,
          "runtime tile changes do not reach the editable scene");
    std::weak_ptr<Scene> weakRuntime = runtime;
    (void)fixture.Context.StopPlayMode();
    Check(weakRuntime.expired() || weakRuntime.lock()->GetPhysicsWorld().GetTilemapBodyCount() == 0,
          "runtime scene and its tilemap bodies are released on Stop");
    Check(TilemapSerializer::EncodeChunk(fixture.EditScene().FindEntityByUUID(map.GetUUID()).GetComponent<TilemapComponent>().Layers[1].Chunks.at(ChunkKey({0, 0}))) ==
              editBefore,
          "after Stop the original tilemap is unchanged");
}

#if XORA2D_SCRIPTING_ENABLED
void TestPython() {
    XORA_LOG_INFO("TEST tilemap Python API");
    TilemapFixture fixture;
    {
        std::ofstream script(fixture.Root() / "Assets/Scripts/Digger.py");
        script << R"(
class Digger:
    def on_create(self):
        tm = self.entity.tilemap
        self.report = [tm.exists, tm.get_tile(0, 0), tm.get_tile(0, 0, "Background"), tm.get_tile(0, 0, "Nope"),
                       list(tm.layer_names()), tm.world_to_cell(40.0, 17.0), tm.cell_to_world(0, 0)]
    def on_update(self, dt):
        tm = self.entity.tilemap
        tm.set_tile(3, 4, 2, "Ground", flip_x=True)
        tm.clear_tile(0, 0)
        with open(self.out, "w") as handle:
            handle.write(repr(self.report) + "|" + str(tm.get_tile(3, 4)) + "|" + str(tm.tile_count("Ground")))
)";
    }
    PythonScriptEngine engine;
    if (!engine.Initialize().Success) {
        Check(false, "python engine");
        return;
    }
    fixture.Context.GetPlaySession().SetHooks({.WireRuntimeScene =
                                                   [&](Scene& scene) {
                                                       scene.SetPythonScriptEngine(&engine);
                                                       ScriptServices services{};
                                                       services.Assets = &fixture.Assets();
                                                       engine.SetServices(services);
                                                   },
                                               .ClearGameplayInput = {}});
    const auto script = fixture.Assets().RegisterPythonScriptAtRelativePath("Scripts/Digger.py", "Digger");
    Entity map = fixture.MakeTilemap(fixture.EditScene());
    map.GetComponent<TransformComponent>().Translation = {8.0f, 1.0f};
    map.GetComponent<TilemapComponent>().SetTile(1, {0, 0}, TileCell::Make(2));
    auto& component = map.AddComponent<PythonScriptComponent>();
    component.ScriptAsset = script.Handle;
    component.ClassName = "Digger";
    const fs::path out = fixture.Root() / "digger.txt";
    component.Properties = {ScriptProperty{.Name = "out", .Type = ScriptPropertyType::String, .StringValue = out.string()}};

    (void)fixture.Context.StartPlayMode();
    fixture.Context.UpdatePlayMode(1.0f / 60.0f);
    std::ostringstream text;
    text << std::ifstream(out).rdbuf();
    const std::string report = text.str();
    XORA_LOG_INFO("  python report: {}", report);
    Check(report.find("[True, 2, 0, -1, ['Background', 'Ground', 'Decorations', 'Foreground'], (2, 1), (16.0, 9.0)]") == 0,
          "Python reads tiles, layers and converts world <-> cell coordinates");
    Check(report.ends_with("|2|1"), "Python modifies tiles (set_tile / clear_tile)");
    const Entity runtimeMap = fixture.Context.GetScene()->FindEntityByUUID(map.GetUUID());
    Check(runtimeMap.GetComponent<TilemapComponent>().GetTile(1, {3, 4}) == TileCell::Make(2, true) &&
              map.GetComponent<TilemapComponent>().GetTile(1, {3, 4}).IsEmpty() &&
              map.GetComponent<TilemapComponent>().GetTile(1, {0, 0}).Id == 2,
          "script edits affect only the runtime tilemap");
    (void)fixture.Context.StopPlayMode();
}
#endif

// ---------------------------------------------------------------------------------------------------------------
// Performance (logged; asserts only structural properties)
// ---------------------------------------------------------------------------------------------------------------

void TestLargeMapPerformance() {
    XORA_LOG_INFO("TEST tilemap large map");
    TilemapFixture fixture;
    const Tileset* tileset = fixture.GetTileset();
    TilemapComponent map;
    map.CreateDefaultLayers();
    map.Tileset = fixture.TilesetHandle;
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::duration duration) { return std::chrono::duration<double, std::milli>(duration).count(); };

    auto start = Clock::now();
    (void)FillRect(map, 1, TileRect::FromCorners({0, 0}, {255, 63}), TileCell::Make(2)); // 16,384 tiles
    (void)FillRect(map, 0, TileRect::FromCorners({0, 0}, {255, 63}), TileCell::Make(1)); // + 16,384
    const double fillMs = ms(Clock::now() - start);
    const std::size_t chunks = map.Layers[0].Chunks.size() + map.Layers[1].Chunks.size();

    start = Clock::now();
    std::size_t vertices = 0;
    for (std::int32_t layer : {0, 1}) {
        for (const auto& [key, chunk] : map.Layers[static_cast<std::size_t>(layer)].Chunks) {
            (void)chunk;
            vertices += GetChunkGeometry(map, layer, key, tileset).Vertices.size();
        }
    }
    const double buildMs = ms(Clock::now() - start);
    start = Clock::now();
    std::size_t reused = 0;
    for (std::int32_t layer : {0, 1}) {
        for (const auto& [key, chunk] : map.Layers[static_cast<std::size_t>(layer)].Chunks) {
            (void)chunk;
            bool rebuilt = true;
            (void)GetChunkGeometry(map, layer, key, tileset, &rebuilt);
            reused += rebuilt ? 0 : 1;
        }
    }
    const double cachedMs = ms(Clock::now() - start);

    start = Clock::now();
    std::size_t rects = 0;
    for (const std::uint64_t key : CollectCollisionChunks(map)) {
        rects += BuildChunkCollisionRects(map, key, [&](AssetHandle) { return tileset; }).size();
    }
    const double collisionMs = ms(Clock::now() - start);

    auto scene = Scene::Create();
    scene->SetAssetManager(&fixture.Assets());
    Entity entity = scene->CreateEntity("Big");
    entity.AddComponent<TilemapComponent>() = map;
    start = Clock::now();
    scene->OnUpdate(1.0f / 60.0f);
    const double physicsBuildMs = ms(Clock::now() - start);
    const std::size_t fixturesAfterBuild = scene->GetPhysicsWorld().GetTilemapFixtureCount(entity);
    start = Clock::now();
    for (int frame = 0; frame < 60; ++frame) {
        scene->OnUpdate(1.0f / 60.0f);
    }
    const double physicsIdleMs = ms(Clock::now() - start) / 60.0;
    entity.GetComponent<TilemapComponent>().SetTile(1, {10, 10}, TileCell{});
    start = Clock::now();
    scene->OnUpdate(1.0f / 60.0f);
    const double physicsEditMs = ms(Clock::now() - start);

    XORA_LOG_INFO("  32,768 tiles in {} chunks: fill {:.2f} ms, geometry build {:.2f} ms, cached frame {:.3f} ms, "
                  "collision merge {:.2f} ms ({} rects), physics first sync {:.2f} ms, idle step {:.3f} ms, "
                  "single-tile edit step {:.3f} ms",
                  chunks, fillMs, buildMs, cachedMs, collisionMs, rects, physicsBuildMs, physicsIdleMs, physicsEditMs);
    Check(map.GetTotalTileCount() == 32'768 && vertices == 32'768 * 4, "32,768-tile map built");
    Check(reused == chunks, "a frame without edits reuses every chunk's geometry");
    Check(rects == 16 && fixturesAfterBuild == 16,
          "16,384 solid tiles (16 chunks) collide through " + std::to_string(rects) + " merged rectangles");
    Check(scene->GetPhysicsWorld().GetTilemapFixtureCount(entity) > 16 &&
              scene->GetPhysicsWorld().GetTilemapFixtureCount(entity) < 24,
          "a hole in one chunk re-merges only that chunk (" +
              std::to_string(scene->GetPhysicsWorld().GetTilemapFixtureCount(entity)) + " fixtures)");
    Check(sizeof(TileChunk) <= 8 * 1024 + 32, "chunk storage is compact (" + std::to_string(sizeof(TileChunk)) + " bytes / 1024 cells)");
}

} // namespace

int RunTilemapTests() {
    XORA_LOG_INFO("TEST tilemaps started");
    const int before = g_failures;
    TestTilesets();
    TestTilemapData();
    TestEditing();
    TestRenderingGeometry();
    TestRenderingGpu();
    TestPhysics();
    TestSerialization();
    TestPrefabAndRuntime();
#if XORA2D_SCRIPTING_ENABLED
    TestPython();
#endif
    TestLargeMapPerformance();
    XORA_LOG_INFO("TEST tilemaps finished with {} failure(s)", g_failures - before);
    return g_failures - before;
}
