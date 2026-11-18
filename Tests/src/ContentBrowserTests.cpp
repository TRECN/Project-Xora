// Milestone 15: content browser / asset pipeline tests. Every filesystem test runs against an isolated temporary
// project (XORA2D_RESOURCES points at it) that is deleted afterwards.

#include "Xora2D/Animation/AnimationClipAsset.h"
#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Assets/AssetRegistrySerializer.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Renderer/Texture2D.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Serialization/SceneSerializer.h"

#include "Xora2DEditor/Content/AssetPreview.h"
#include "Xora2DEditor/Content/AssetThumbnailCache.h"
#include "Xora2DEditor/Content/ContentBrowserDirectoryCache.h"
#include "Xora2DEditor/Content/ContentBrowserImport.h"
#include "Xora2DEditor/Content/ContentBrowserOperations.h"
#include "Xora2DEditor/Content/ContentBrowserPaths.h"
#include "Xora2DEditor/Content/ContentBrowserQuery.h"
#include "Xora2DEditor/Content/ContentBrowserState.h"
#include "Xora2DEditor/Content/EditorAssetAssignments.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace Xora2DEditor;

namespace {

int g_contentFailures = 0;

void Check(bool condition, const std::string& description, int line) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {} (ContentBrowserTests.cpp:{})", description, line);
    ++g_contentFailures;
}

#define CHECK(condition, description) Check(static_cast<bool>(condition), description, __LINE__)

void WriteText(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output << text;
}

std::string ReadText(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

bool HasTempArtifacts(const fs::path& root) {
    std::error_code errorCode;
    for (const auto& entry : fs::recursive_directory_iterator(root, errorCode)) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with(kImportTempPrefix) || name == kTrashFolderName) {
            return true;
        }
    }
    return false;
}

// Temporary project with the standard folder layout. Points the engine resource root at it for its lifetime.
class TempProject {
public:
    explicit TempProject(const std::string& name) {
        // Fixtures come from the real project before the resource root is redirected.
        m_sourceResources = Xora2D::GetResourceRoot();
        std::mt19937_64 generator{std::random_device{}()};
        m_root = fs::temp_directory_path() / ("xora2d_content_" + name + "_" + std::to_string(generator() % 1000000));
        fs::remove_all(m_root);
        for (const char* folder : {"Assets/Textures", "Assets/Audio", "Assets/Scripts", "Assets/Animations",
                                   "Assets/Registry", "Scenes", "Shaders"}) {
            fs::create_directories(m_root / folder);
        }
        m_root = fs::weakly_canonical(m_root);
        const char* previous = std::getenv("XORA2D_RESOURCES");
        if (previous != nullptr) {
            m_previousEnv = previous;
        }
        ::setenv("XORA2D_RESOURCES", m_root.string().c_str(), 1);

        m_fixtures = fs::temp_directory_path() / ("xora2d_content_fixtures_" + name);
        fs::remove_all(m_fixtures);
        fs::create_directories(m_fixtures);
    }

    ~TempProject() {
        if (m_previousEnv.has_value()) {
            ::setenv("XORA2D_RESOURCES", m_previousEnv->c_str(), 1);
        } else {
            ::unsetenv("XORA2D_RESOURCES");
        }
        std::error_code errorCode;
        fs::remove_all(m_root, errorCode);
        fs::remove_all(m_fixtures, errorCode);
    }

    [[nodiscard]] const fs::path& Root() const { return m_root; }
    [[nodiscard]] const fs::path& Fixtures() const { return m_fixtures; }

    // Copies a file from the repository's Resources/ into the fixtures folder (outside the project).
    fs::path Fixture(const std::string& resourceRelative, const std::string& asName = {}) const {
        const fs::path destination = m_fixtures / (asName.empty() ? fs::path(resourceRelative).filename().string() : asName);
        fs::copy_file(m_sourceResources / resourceRelative, destination, fs::copy_options::overwrite_existing);
        return destination;
    }

    fs::path FixtureText(const std::string& name, const std::string& contents) const {
        WriteText(m_fixtures / name, contents);
        return m_fixtures / name;
    }

private:
    fs::path m_sourceResources;
    fs::path m_root;
    fs::path m_fixtures;
    std::optional<std::string> m_previousEnv;
};

ContentBrowserEntry MakeEntry(const std::string& name, ContentItemKind kind, std::uint64_t size, int ageSeconds) {
    ContentBrowserEntry entry{};
    entry.Name = name;
    entry.ResourceRelativePath = "Assets/" + name;
    entry.Kind = kind;
    entry.FileSizeBytes = size;
    entry.LastModified = fs::file_time_type::clock::now() - std::chrono::seconds(ageSeconds);
    return entry;
}

std::vector<std::string> Names(const std::vector<ContentBrowserEntry>& entries) {
    std::vector<std::string> names;
    for (const auto& entry : entries) {
        names.push_back(entry.Name);
    }
    return names;
}

const ContentBrowserEntry* FindEntry(const std::vector<ContentBrowserEntry>& entries, const std::string& name) {
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.Name == name; });
    return found == entries.end() ? nullptr : &*found;
}

class FakeTexture final : public Xora2D::Texture2D {
public:
    FakeTexture(std::uint32_t width, std::uint32_t height, std::uint32_t id)
        : m_width(width), m_height(height), m_id(id) {}
    void Bind(std::uint32_t) const override {}
    void Unbind() const override {}
    std::uint32_t GetWidth() const override { return m_width; }
    std::uint32_t GetHeight() const override { return m_height; }
    std::uint32_t GetRendererId() const override { return m_id; }
    void SetFilter(Xora2D::TextureFilter, Xora2D::TextureFilter) override {}
    void SetWrap(Xora2D::TextureWrap, Xora2D::TextureWrap) override {}

private:
    std::uint32_t m_width;
    std::uint32_t m_height;
    std::uint32_t m_id;
};

// ------------------------------------------------------------------------------------------------ pure logic

void TestPathSafety() {
    XORA_LOG_INFO("TEST content: path safety");
    CHECK(NormalizeResourceRelativePath("Assets//Textures/./") == std::optional<std::string>("Assets/Textures"),
          "normalize collapses empty and '.' segments");
    CHECK(NormalizeResourceRelativePath("Assets\\Audio") == std::optional<std::string>("Assets/Audio"),
          "normalize converts backslashes");
    CHECK(!NormalizeResourceRelativePath("../outside").has_value(), "reject leading '..'");
    CHECK(!NormalizeResourceRelativePath("Assets/../../etc").has_value(), "reject embedded '..'");
    CHECK(!NormalizeResourceRelativePath("/etc/passwd").has_value(), "reject absolute path");
    CHECK(!NormalizeResourceRelativePath("C:/Windows").has_value(), "reject drive letter");
    CHECK(!NormalizeResourceRelativePath(std::string("Assets/a\0b", 10)).has_value(), "reject NUL byte");
    CHECK(NormalizeResourceRelativePath("Assets/a..b.png").has_value(), "allow '..' inside a file name");
    CHECK(IsValidEntryName("Hero Sprite.png"), "valid entry name");
    CHECK(!IsValidEntryName("a/b") && !IsValidEntryName("..") && !IsValidEntryName(".hidden") &&
              !IsValidEntryName("bad:name") && !IsValidEntryName(""),
          "invalid entry names rejected");
    CHECK(IsProtectedResourcePath("Assets") && IsProtectedResourcePath("Scenes") &&
              IsProtectedResourcePath("Assets/Registry/AssetRegistry.yaml") && IsProtectedResourcePath(""),
          "project roots and registry are protected");
    CHECK(!IsProtectedResourcePath("Assets/Textures"), "ordinary folders are not protected");
    CHECK(IsResourcePathWithin("Assets/Textures/a.png", "Assets/Textures") &&
              !IsResourcePathWithin("Assets/TexturesOld/a.png", "Assets/Textures"),
          "subtree check respects segment boundaries");
    CHECK(FullExtensionLower("Walk.ANIM.yaml") == ".anim.yaml" && FullExtensionLower("Level.scene.yaml") == ".scene.yaml",
          "compound extensions detected");
    CHECK(ClassifyContentFile("Hero.PNG") == ContentItemKind::Texture && ClassifyContentFile("a.mp3") == ContentItemKind::Audio &&
              ClassifyContentFile("x.scene.yaml") == ContentItemKind::Scene &&
              ClassifyContentFile("x.anim.yaml") == ContentItemKind::Animation && ClassifyContentFile("x.txt") == ContentItemKind::Other,
          "content classification");
}

void TestNavigationState() {
    XORA_LOG_INFO("TEST content: navigation state");
    ContentBrowserState state;
    CHECK(state.GetCurrentDirectory() == "Assets", "browser starts in Assets");
    CHECK(state.NavigateTo("Assets/Textures"), "navigate into folder");
    CHECK(state.NavigateTo("Assets/Textures/Characters"), "navigate deeper");
    CHECK(state.NavigateBack() && state.GetCurrentDirectory() == "Assets/Textures", "back");
    CHECK(state.NavigateForward() && state.GetCurrentDirectory() == "Assets/Textures/Characters", "forward");
    CHECK(state.NavigateUp() && state.GetCurrentDirectory() == "Assets/Textures", "up");
    CHECK(state.NavigateBack() && state.NavigateTo("Scenes") && !state.CanNavigateForward(),
          "navigating after back truncates forward history");
    CHECK(!state.NavigateTo("../../etc") && state.GetCurrentDirectory() == "Scenes", "unsafe navigation rejected");
    state.NavigateTo("Assets/Old/Sub");
    state.OnPathRelocated("Assets/Old", "Assets/New");
    CHECK(state.GetCurrentDirectory() == "Assets/New/Sub", "history follows folder rename");
    state.OnPathDeleted("Assets/New");
    CHECK(state.GetCurrentDirectory() == "Assets", "history leaves deleted folder");
}

void TestQuery() {
    XORA_LOG_INFO("TEST content: filter / search / sort");
    std::vector<ContentBrowserEntry> entries = {
        MakeEntry("zeta.png", ContentItemKind::Texture, 300, 10),
        MakeEntry("Alpha.wav", ContentItemKind::Audio, 100, 30),
        MakeEntry("beta.py", ContentItemKind::Script, 200, 20),
        MakeEntry("Level.scene.yaml", ContentItemKind::Scene, 50, 5),
        MakeEntry("Walk.anim.yaml", ContentItemKind::Animation, 75, 40),
        MakeEntry("Sprites", ContentItemKind::Folder, 0, 1),
    };
    entries.push_back(MakeEntry("gone.png", ContentItemKind::Texture, 0, 0));
    entries.back().Status = ContentImportStatus::Missing;

    ContentQuery query{};
    auto result = ApplyContentQuery(entries, query);
    CHECK(result.size() == entries.size() && result.front().Name == "Sprites", "folders first");
    CHECK(Names(result)[1] == "Alpha.wav" && Names(result)[2] == "beta.py", "name sort is case-insensitive");

    query.Ascending = false;
    result = ApplyContentQuery(entries, query);
    CHECK(result.front().Name == "Sprites" && result[1].Name == "zeta.png", "descending keeps folders first");

    query = {};
    query.SortKey = ContentSortKey::Size;
    result = ApplyContentQuery(entries, query);
    CHECK(result[1].Name == "gone.png" && result.back().Name == "zeta.png", "sort by size");

    query.SortKey = ContentSortKey::Modified;
    result = ApplyContentQuery(entries, query);
    CHECK(result[1].Name == "Walk.anim.yaml" && result.back().Name == "gone.png", "sort by modified date (oldest first)");

    query.SortKey = ContentSortKey::Type;
    result = ApplyContentQuery(entries, query);
    CHECK(result[1].Kind == ContentItemKind::Animation && result.back().Kind == ContentItemKind::Texture, "sort by type");

    query = {};
    query.Filter = ContentTypeFilter::Textures;
    CHECK(ApplyContentQuery(entries, query).size() == 2, "texture filter");
    query.Filter = ContentTypeFilter::Scenes;
    CHECK(Names(ApplyContentQuery(entries, query)) == std::vector<std::string>{"Level.scene.yaml"}, "scene filter");
    query.Filter = ContentTypeFilter::Folders;
    CHECK(ApplyContentQuery(entries, query).size() == 1, "folder filter");
    query.Filter = ContentTypeFilter::Missing;
    CHECK(Names(ApplyContentQuery(entries, query)) == std::vector<std::string>{"gone.png"}, "missing filter");

    query = {};
    query.Search = "ALP";
    CHECK(Names(ApplyContentQuery(entries, query)) == std::vector<std::string>{"Alpha.wav"}, "case-insensitive search");
    query.Search = "anim walk";
    CHECK(ApplyContentQuery(entries, query).size() == 1, "multi-term search");
    query.Search = "nothing-matches";
    CHECK(ApplyContentQuery(entries, query).empty(), "search with no matches");
    query.Search = "a";
    query.Filter = ContentTypeFilter::Audio;
    CHECK(ApplyContentQuery(entries, query).size() == 1, "search combined with filter");
}

// ------------------------------------------------------------------------------------------------ filesystem

void TestEnumerationAndFolders() {
    XORA_LOG_INFO("TEST content: enumeration and folder creation");
    TempProject project("enum");
    Xora2D::AssetRegistry registry;
    Xora2D::AssetManager manager(registry);
    manager.SetGraphicsResourcesEnabled(false);

    WriteText(project.Root() / "Assets/Textures/.DS_Store", "x");
    WriteText(project.Root() / "Assets/Textures/.xora_import_1.tmp", "x");
    fs::copy_file(project.Fixture("Assets/Textures/Xora2D_Logo.png"), project.Root() / "Assets/Textures/Logo.png");
    WriteText(project.Root() / "Assets/Textures/notes.txt", "hello");
    CHECK(manager.RegisterTextureAtRelativePath("Textures/Logo.png").Success, "register fixture texture");

    std::string error;
    const auto entries = ListDirectory("Assets/Textures", registry, error);
    CHECK(error.empty() && entries.size() == 2, "hidden and temp files are not listed");
    const ContentBrowserEntry* logo = FindEntry(entries, "Logo.png");
    CHECK(logo != nullptr && logo->Kind == ContentItemKind::Texture && logo->Status == ContentImportStatus::Registered &&
              logo->RegisteredHandle.has_value() && logo->FileSizeBytes > 0,
          "registered texture enumerated with handle and size");
    const ContentBrowserEntry* notes = FindEntry(entries, "notes.txt");
    CHECK(notes != nullptr && notes->Kind == ContentItemKind::Other && notes->Status == ContentImportStatus::NotApplicable,
          "unmanaged file enumerated");

    const auto root = ListDirectory("Assets", registry, error);
    CHECK(FindEntry(root, "Textures") != nullptr && FindEntry(root, "Textures")->IsFolder(), "folders enumerated");
    (void)ListDirectory("Assets/DoesNotExist", registry, error);
    CHECK(!error.empty(), "missing folder reports an error");
    (void)ListDirectory("../", registry, error);
    CHECK(!error.empty(), "traversal listing rejected");

    CHECK(CreateProjectFolder("Assets/Textures", "Characters").Success, "create folder");
    CHECK(fs::is_directory(project.Root() / "Assets/Textures/Characters"), "folder exists on disk");
    CHECK(!CreateProjectFolder("Assets/Textures", "Characters").Success, "duplicate folder rejected");
    CHECK(!CreateProjectFolder("Assets/Textures", "../Escape").Success, "folder name with traversal rejected");
    CHECK(!CreateProjectFolder("../", "Escape").Success, "parent outside project rejected");
    CHECK(!CreateProjectFolder("Assets/Registry", "Nested").Success, "folder inside registry rejected");
    CHECK(!fs::exists(project.Root().parent_path() / "Escape"), "nothing created outside the project");

    const auto folders = ListFoldersRecursive("");
    CHECK(std::find(folders.begin(), folders.end(), "Assets/Textures/Characters") != folders.end(),
          "recursive folder listing");

    ContentBrowserDirectoryCache cache;
    cache.SetAutoRefreshInterval(std::chrono::seconds(0));
    (void)cache.Get("Assets/Textures", registry);
    (void)cache.Get("Assets/Textures", registry);
    CHECK(cache.GetScanCount() == 1, "directory cache avoids rescans");
    WriteText(project.Root() / "Assets/Textures/external.txt", "made outside the editor");
    cache.InvalidateDirectory("Assets/Textures");
    CHECK(FindEntry(cache.Get("Assets/Textures", registry).Entries, "external.txt") != nullptr && cache.GetScanCount() == 2,
          "invalidation picks up external changes");
    ContentBrowserDirectoryCache timed;
    timed.SetAutoRefreshInterval(std::chrono::milliseconds(1));
    const auto start = ContentBrowserDirectoryCache::Clock::now();
    (void)timed.Get("Assets", registry, start);
    (void)timed.Get("Assets", registry, start + std::chrono::milliseconds(5));
    CHECK(timed.GetScanCount() == 2, "stale listings rescan automatically");

#if !defined(_WIN32)
    const fs::path outside = project.Fixtures() / "outside_dir";
    fs::create_directories(outside);
    std::error_code linkError;
    fs::create_directory_symlink(outside, project.Root() / "Assets/Escape", linkError);
    if (!linkError) {
        CHECK(ResolveResourceRelativePath("Assets/Escape").empty(), "symlink escaping the project is not resolved");
        CHECK(FindEntry(ListDirectory("Assets", registry, error), "Escape") == nullptr, "escaping symlink not listed");
        CHECK(!CreateProjectFolder("Assets/Escape", "x").Success, "cannot create through escaping symlink");
    }
#endif
}

void TestImport() {
    XORA_LOG_INFO("TEST content: import");
    TempProject project("import");
    Xora2D::AssetRegistry registry;
    Xora2D::AssetManager manager(registry);
    manager.SetGraphicsResourcesEnabled(false);

    const fs::path png = project.Fixture("Assets/Textures/Xora2D_Logo.png", "Hero.png");
    const fs::path wav = project.Fixture("Assets/Audio/sfx_beep.wav", "Beep.wav");

    // Valid image.
    auto batch = ImportExternalFiles({png}, "Assets", manager);
    CHECK(batch.AllSucceeded() && batch.Items.size() == 1, "valid image import succeeds");
    const ContentImportItemResult image = batch.Items.front();
    CHECK(image.DestinationResourcePath == "Assets/Textures/Hero.png", "image routed to Assets/Textures");
    CHECK(fs::exists(project.Root() / "Assets/Textures/Hero.png"), "image copied into project");
    CHECK(image.Handle == Xora2D::AssetHandle::StableFromRelativePath("Textures/Hero.png"),
          "imported image gets the stable path-derived handle");
    const Xora2D::AssetMetadata* imageMeta = registry.FindByHandle(image.Handle);
    CHECK(imageMeta != nullptr && imageMeta->Type == Xora2D::AssetType::Texture2D, "image registered as Texture2D");

    // Valid audio, imported while browsing a subfolder of the audio folder.
    CHECK(CreateProjectFolder("Assets/Audio", "SFX").Success, "create audio subfolder");
    batch = ImportExternalFiles({wav}, "Assets/Audio/SFX", manager);
    CHECK(batch.AllSucceeded() && batch.Items.front().DestinationResourcePath == "Assets/Audio/SFX/Beep.wav",
          "audio import respects current folder inside the audio tree");
    CHECK(registry.FindByHandle(batch.Items.front().Handle) != nullptr &&
              registry.FindByHandle(batch.Items.front().Handle)->Type == Xora2D::AssetType::Audio,
          "audio registered");
    batch = ImportExternalFiles({project.Fixture("Assets/Audio/sfx_click.wav")}, "Assets/Textures", manager);
    CHECK(batch.AllSucceeded() && batch.Items.front().DestinationResourcePath == "Assets/Audio/sfx_click.wav",
          "audio imported from the texture folder goes to Assets/Audio");

    // Duplicate import: identical content.
    const std::size_t countBefore = registry.Count();
    batch = ImportExternalFiles({png}, "Assets", manager);
    CHECK(batch.AllSucceeded() && batch.Items.front().Duplicate && batch.Items.front().Handle == image.Handle,
          "re-importing identical file reuses the existing handle");
    CHECK(registry.Count() == countBefore, "duplicate import adds no registry entry");
    CHECK(!fs::exists(project.Root() / "Assets/Textures/Hero_1.png"), "duplicate import does not copy again");

    // Same name, different content: unique name, new handle, original untouched.
    const fs::path otherPng = project.Fixture("Assets/Textures/DemoSpriteSheet.png", "Hero.png");
    batch = ImportExternalFiles({otherPng}, "Assets", manager);
    CHECK(batch.AllSucceeded() && batch.Items.front().DestinationResourcePath == "Assets/Textures/Hero_1.png",
          "name clash with different content imports under a unique name");
    CHECK(batch.Items.front().Handle != image.Handle && registry.Count() == countBefore + 1, "clash gets its own handle");

    // Invalid files are rejected and leave nothing behind.
    const std::size_t countBeforeInvalid = registry.Count();
    const fs::path corrupt = project.FixtureText("Corrupt.png", "this is not a png");
    const fs::path fakeWav = project.FixtureText("Fake.wav", "RIFF????");
    const fs::path ogg = project.FixtureText("Music.ogg", "OggS");
    const fs::path text = project.FixtureText("Readme.txt", "hello");
    const fs::path badScene = project.FixtureText("Broken.scene.yaml", "Entities: [unclosed");
    const fs::path notScene = project.FixtureText("Other.scene.yaml", "Name: x\n");
    const fs::path badAnim = project.FixtureText("Bad.anim.yaml", "Version: 7\nFrames: []\n");
    const fs::path empty = project.FixtureText("Empty.png", "");
    batch = ImportExternalFiles({corrupt, fakeWav, ogg, text, badScene, notScene, badAnim, empty,
                                 project.Fixtures() / "DoesNotExist.png"},
                                "Assets", manager);
    CHECK(batch.SuccessCount() == 0, "all invalid files rejected");
    CHECK(registry.Count() == countBeforeInvalid, "rejected files are not registered");
    CHECK(!fs::exists(project.Root() / "Assets/Textures/Corrupt.png") && !fs::exists(project.Root() / "Assets/Audio/Fake.wav") &&
              !fs::exists(project.Root() / "Scenes/Broken.scene.yaml"),
          "rejected files are not copied");
    CHECK(!HasTempArtifacts(project.Root()), "no temporary files left after failed imports");
    const auto oggResult = std::find_if(batch.Items.begin(), batch.Items.end(),
                                        [&](const auto& item) { return item.Source == ogg; });
    CHECK(oggResult != batch.Items.end() && oggResult->Message.find("OGG") != std::string::npos,
          "OGG rejection explains why");

    // Scene import: copied into Scenes/, not a registry asset.
    const fs::path scene = project.FixtureText(
        "Level1.scene.yaml", "Version: 1\nName: Level1\nEntities:\n  - Entity: 1020304050607080-aabbccddeeff0011\n"
                             "    TagComponent:\n      Tag: Player\n");
    batch = ImportExternalFiles({scene}, "Assets", manager);
    CHECK(batch.AllSucceeded() && batch.Items.front().DestinationResourcePath == "Scenes/Level1.scene.yaml" &&
              !batch.Items.front().Handle.IsValid(),
          "scene imported into Scenes without registration");

    // Animation clip imported in the same batch as its texture: texture is staged first.
    WriteText(project.Fixtures() / "anim/Sheet.png", ReadText(project.Fixtures() / "Hero.png"));
    fs::copy_file(project.Fixture("Assets/Textures/DemoSpriteSheet.png"), project.Fixtures() / "anim/Sheet.png",
                  fs::copy_options::overwrite_existing);
    WriteText(project.Fixtures() / "anim/Run.anim.yaml",
              "Version: 1\nName: Run\nTextureRelativePath: Textures/Sheet.png\nLoop: true\nSpriteSheet:\n"
              "  Columns: 4\n  Rows: 1\n  FrameWidth: 32\n  FrameHeight: 32\n  FrameCount: 4\n");
    batch = ImportExternalFiles({project.Fixtures() / "anim/Run.anim.yaml", project.Fixtures() / "anim/Sheet.png"},
                                "Assets", manager);
    CHECK(batch.AllSucceeded() && batch.Items.size() == 2, "animation + texture batch imports");
    const auto clipItem = std::find_if(batch.Items.begin(), batch.Items.end(), [](const auto& item) {
        return item.DestinationResourcePath == "Assets/Animations/Run.anim.yaml";
    });
    CHECK(clipItem != batch.Items.end() && clipItem->Warning.empty() &&
              registry.FindByHandle(clipItem->Handle)->Type == Xora2D::AssetType::AnimationClip,
          "animation clip registered without texture warning");
    const Xora2D::AssetHandle clipHandle = clipItem != batch.Items.end() ? clipItem->Handle : Xora2D::AssetHandle{};

    // Script import depends on the scripting build option.
    const fs::path script = project.FixtureText("Mover.py", "class Mover:\n    pass\n");
    batch = ImportExternalFiles({script}, "Assets", manager);
#if XORA2D_SCRIPTING_ENABLED
    CHECK(batch.AllSucceeded() && batch.Items.front().DestinationResourcePath == "Assets/Scripts/Mover.py",
          "python script imported when scripting is enabled");
#else
    CHECK(!batch.AllSucceeded() && !fs::exists(project.Root() / "Assets/Scripts/Mover.py"),
          "python script rejected when scripting is disabled");
#endif

    // Importing a file that already lives in the project registers it in place.
    WriteText(project.Root() / "Assets/Textures/Placed.png", ReadText(png));
    batch = ImportExternalFiles({project.Root() / "Assets/Textures/Placed.png"}, "Assets", manager);
    CHECK(batch.AllSucceeded() && batch.Items.front().DestinationResourcePath == "Assets/Textures/Placed.png" &&
              !fs::exists(project.Root() / "Assets/Textures/Placed_1.png"),
          "in-project file registered in place");

    // Registry persistence round-trip.
    const fs::path registryFile = project.Root() / "Assets/Registry/AssetRegistry.yaml";
    CHECK(Xora2D::AssetRegistrySerializer::Save(registry, registryFile).Success, "save registry");
    Xora2D::AssetRegistry reloaded;
    CHECK(Xora2D::AssetRegistrySerializer::Load(reloaded, registryFile).Success, "reload registry");
    CHECK(reloaded.Count() == registry.Count() && reloaded.FindByHandle(image.Handle) != nullptr &&
              reloaded.FindByHandle(image.Handle)->RelativePath == "Textures/Hero.png",
          "registry persists handles and paths");
    CHECK(reloaded.FindByHandle(clipHandle) != nullptr &&
              reloaded.FindByHandle(clipHandle)->Type == Xora2D::AssetType::AnimationClip,
          "registry persists animation clip type");

    // Async queue: staging off-thread, registration on Poll().
    ContentImportQueue queue;
    queue.Submit({project.Fixture("Assets/Audio/music_loop.wav")}, "Assets");
    CHECK(queue.WaitUntilStaged(std::chrono::seconds(20)), "import queue stages on worker");
    const std::optional<ContentImportBatchResult> queued = queue.Poll(manager);
    CHECK(queued.has_value() && queued->AllSucceeded() &&
              registry.FindByRelativePath("Audio/music_loop.wav") != nullptr,
          "import queue commits on poll");
    CHECK(!queue.IsBusy(), "import queue idle afterwards");
    queue.Shutdown();
    CHECK(!HasTempArtifacts(project.Root()), "no temporary files after imports");
}

void TestRenameMoveDelete() {
    XORA_LOG_INFO("TEST content: rename / move / delete");
    TempProject project("ops");
    Xora2D::AssetRegistry registry;
    Xora2D::AssetManager manager(registry);
    manager.SetGraphicsResourcesEnabled(false);

    auto batch = ImportExternalFiles({project.Fixture("Assets/Textures/DemoSpriteSheet.png", "Sheet.png"),
                                      project.Fixture("Assets/Audio/sfx_beep.wav")},
                                     "Assets", manager);
    CHECK(batch.AllSucceeded(), "fixtures imported");
    const Xora2D::AssetHandle sheet = registry.FindByRelativePath("Textures/Sheet.png")->Handle;
    const Xora2D::AssetHandle beep = registry.FindByRelativePath("Audio/sfx_beep.wav")->Handle;

    // Animation clip referencing the texture by path (the format used by the demo clips).
    WriteText(project.Root() / "Assets/Animations/Idle.anim.yaml",
              "Version: 1\nName: Idle\nTextureRelativePath: Textures/Sheet.png\nLoop: true\nSpriteSheet:\n"
              "  Columns: 4\n  Rows: 1\n  FrameWidth: 32\n  FrameHeight: 32\n  FrameCount: 2\n");
    const Xora2D::AssetHandle clip = manager.RegisterAnimationClipAtRelativePath("Animations/Idle.anim.yaml").Handle;
    CHECK(clip.IsValid(), "clip registered");

    // Scene referencing the texture and audio by handle.
    {
        Xora2D::Scene scene;
        scene.SetAssetManager(&manager);
        Xora2D::Entity hero = scene.CreateEntity("Hero");
        CHECK(AssignAssetToEntity(scene, hero, sheet, registry).Success, "assign texture");
        CHECK(AssignAssetToEntity(scene, hero, beep, registry).Success, "assign audio");
        CHECK(Xora2D::SceneSerializer::Serialize(scene, project.Root() / "Scenes/Main.scene.yaml").Success, "save scene");
    }

    // Rename keeps the handle and the extension.
    auto result = RenameResourceEntry("Assets/Textures/Sheet.png", "HeroSheet", registry);
    CHECK(result.Success && result.ResultPath == "Assets/Textures/HeroSheet.png", "rename appends original extension");
    CHECK(registry.FindByHandle(sheet) != nullptr && registry.FindByHandle(sheet)->RelativePath == "Textures/HeroSheet.png",
          "rename preserves handle and updates registry path");
    CHECK(registry.FindByHandle(sheet)->Name == "HeroSheet", "display name follows file name");
    CHECK(!fs::exists(project.Root() / "Assets/Textures/Sheet.png") && fs::exists(project.Root() / "Assets/Textures/HeroSheet.png"),
          "file renamed on disk");
    CHECK(result.UpdatedReferenceFiles == 1, "animation clip texture path rewritten");
    const std::string clipText = ReadText(project.Root() / "Assets/Animations/Idle.anim.yaml");
    CHECK(clipText.find("TextureRelativePath: Textures/HeroSheet.png") != std::string::npos &&
              clipText.find("TextureAssetHandle: " + sheet.ToString()) != std::string::npos,
          "clip now carries new path and stable texture handle");
    manager.UnloadAsset(clip);
    CHECK(manager.LoadAnimationClip(clip).Success && manager.GetLoadedAnimationClip(clip)->GetClip().TextureAsset == sheet,
          "renamed texture still resolves from the animation clip");

    CHECK(!RenameResourceEntry("Assets/Textures/HeroSheet.png", "HeroSheet.wav", registry).Success,
          "changing extension rejected");
    CHECK(!RenameResourceEntry("Assets/Textures/HeroSheet.png", "../evil.png", registry).Success,
          "rename with traversal rejected");
    WriteText(project.Root() / "Assets/Textures/Taken.png", "x");
    CHECK(!RenameResourceEntry("Assets/Textures/HeroSheet.png", "Taken.png", registry).Success &&
              ReadText(project.Root() / "Assets/Textures/Taken.png") == "x",
          "rename onto existing file rejected without overwriting");
    CHECK(!RenameResourceEntry("Assets", "Stuff", registry).Success, "protected folder rename rejected");

    // Move file into a new folder.
    CHECK(CreateProjectFolder("Assets/Textures", "Characters").Success, "create destination");
    result = MoveResourceEntry("Assets/Textures/HeroSheet.png", "Assets/Textures/Characters", registry);
    CHECK(result.Success && registry.FindByHandle(sheet)->RelativePath == "Textures/Characters/HeroSheet.png",
          "move preserves handle");
    CHECK(ReadText(project.Root() / "Assets/Animations/Idle.anim.yaml").find("Textures/Characters/HeroSheet.png") !=
              std::string::npos,
          "clip follows moved texture");

    // Folder rename relocates every contained asset.
    result = RenameResourceEntry("Assets/Textures/Characters", "Heroes", registry);
    CHECK(result.Success && result.RelocatedAssetCount == 1 &&
              registry.FindByHandle(sheet)->RelativePath == "Textures/Heroes/HeroSheet.png",
          "folder rename relocates contained assets");
    CHECK(!MoveResourceEntry("Assets/Textures/Heroes", "Assets/Textures/Heroes", registry).Success ||
              registry.FindByHandle(sheet)->RelativePath == "Textures/Heroes/HeroSheet.png",
          "moving folder onto itself is a no-op");
    CHECK(CreateProjectFolder("Assets/Textures/Heroes", "Inner").Success, "create inner folder");
    CHECK(!MoveResourceEntry("Assets/Textures/Heroes", "Assets/Textures/Heroes/Inner", registry).Success,
          "folder cannot move into its own subfolder");

    // Scene references survive rename/move: the saved scene still loads and points at the same handle.
    {
        Xora2D::Scene reloaded;
        const auto load = Xora2D::SceneSerializer::Deserialize(reloaded, project.Root() / "Scenes/Main.scene.yaml", &manager);
        CHECK(load.Success, "scene with references loads after rename/move");
        const Xora2D::Entity hero = reloaded.FindEntityByName("Hero");
        CHECK(hero.IsValid() && hero.GetComponent<Xora2D::SpriteRendererComponent>().TextureAsset == sheet,
              "scene sprite still references the moved texture handle");
    }

    // Failed operations keep registry and disk consistent.
    const auto snapshot = registry.ListAssets();
    const auto sameRegistry = [&]() {
        const auto now = registry.ListAssets();
        return now.size() == snapshot.size() &&
               std::all_of(snapshot.begin(), snapshot.end(), [&](const Xora2D::AssetMetadata& meta) {
                   const Xora2D::AssetMetadata* current = registry.FindByHandle(meta.Handle);
                   return current != nullptr && current->RelativePath == meta.RelativePath;
               });
    };
    CHECK(!MoveResourceEntry("Assets/Audio/sfx_beep.wav", "Scenes", registry).Success,
          "registered asset cannot leave the Assets folder");
    CHECK(fs::exists(project.Root() / "Assets/Audio/sfx_beep.wav") && sameRegistry(), "refused move changed nothing");
    CHECK(!MoveResourceEntry("Assets/Audio/sfx_beep.wav", "Assets/NoSuchFolder", registry).Success && sameRegistry(),
          "move to missing folder changes nothing");
    // A registry entry already claims the destination path: detected before touching the disk.
    Xora2D::AssetMetadata squatter{};
    squatter.Handle = Xora2D::AssetHandle(0x1234);
    squatter.Type = Xora2D::AssetType::Audio;
    squatter.Name = "squatter";
    squatter.RelativePath = "Audio/Renamed.wav";
    squatter.Available = false;
    CHECK(registry.RegisterAsset(squatter), "register conflicting entry");
    CHECK(!RenameResourceEntry("Assets/Audio/sfx_beep.wav", "Renamed.wav", registry).Success,
          "rename conflicting with registry entry refused");
    CHECK(fs::exists(project.Root() / "Assets/Audio/sfx_beep.wav") &&
              registry.FindByHandle(beep)->RelativePath == "Audio/sfx_beep.wav",
          "conflicting rename left disk and registry untouched");
    CHECK(registry.RemoveAsset(squatter.Handle), "remove conflicting entry");

    // A new file at a renamed asset's old path gets a distinct handle (the old handle stays with the moved asset).
    batch = ImportExternalFiles({project.Fixture("Assets/Textures/Xora2D_Logo.png", "Sheet.png")}, "Assets", manager);
    CHECK(batch.AllSucceeded() && batch.Items.front().Handle.IsValid() && batch.Items.front().Handle != sheet,
          "re-using an old path does not steal the moved asset's handle");

    // Delete: references are detected (scene file + animation clip + open scene) and deletion is refused.
    Xora2D::Scene openScene;
    Xora2D::Entity listener = openScene.CreateEntity("Speaker");
    listener.AddComponent<Xora2D::AudioSourceComponent>().ClipAsset = beep;
    const auto references = FindAssetReferences({sheet}, registry);
    CHECK(references.size() == 2, "texture referenced by scene file and animation clip");
    DeleteOptions options{};
    options.OpenScene = &openScene;
    result = DeleteResourceEntry("Assets/Audio/sfx_beep.wav", manager, options);
    CHECK(!result.Success && !result.References.empty() && fs::exists(project.Root() / "Assets/Audio/sfx_beep.wav") &&
              registry.Contains(beep),
          "referenced asset deletion refused and nothing changed");
    const bool reportsOpenScene = std::any_of(result.References.begin(), result.References.end(),
                                              [](const AssetReference& reference) { return reference.ReferencedBy == "<open scene>"; });
    CHECK(reportsOpenScene, "references in the open scene are reported");
    options.AllowReferenced = true;
    result = DeleteResourceEntry("Assets/Audio/sfx_beep.wav", manager, options);
    CHECK(result.Success && !fs::exists(project.Root() / "Assets/Audio/sfx_beep.wav") && !registry.Contains(beep),
          "confirmed delete removes file and registry entry");

    // Deleting a folder removes every contained asset; references from inside the folder do not block it.
    CHECK(CreateProjectFolder("Assets", "Pack").Success, "create pack folder");
    batch = ImportExternalFiles({project.Fixture("Assets/Audio/sfx_click.wav")}, "Assets/Audio", manager);
    CHECK(MoveResourceEntry("Assets/Audio/sfx_click.wav", "Assets/Pack", registry).Success, "move into pack");
    const Xora2D::AssetHandle click = registry.FindByRelativePath("Pack/sfx_click.wav")->Handle;
    result = DeleteResourceEntry("Assets/Pack", manager);
    CHECK(result.Success && !fs::exists(project.Root() / "Assets/Pack") && !registry.Contains(click),
          "folder delete removes contents and registry entries");

    CHECK(!DeleteResourceEntry("Assets", manager).Success && fs::exists(project.Root() / "Assets"),
          "protected folder delete rejected");
    CHECK(!DeleteResourceEntry("Assets/Registry/AssetRegistry.yaml", manager).Success, "registry file delete rejected");
    CHECK(!DeleteResourceEntry("../", manager).Success && !DeleteResourceEntry("Assets/../..", manager).Success,
          "delete with traversal rejected");
    CHECK(!HasTempArtifacts(project.Root()), "no trash or temp files left behind");
}

void TestMissingAssets() {
    XORA_LOG_INFO("TEST content: missing asset detection");
    TempProject project("missing");
    Xora2D::AssetRegistry registry;
    Xora2D::AssetManager manager(registry);
    manager.SetGraphicsResourcesEnabled(false);
    auto batch = ImportExternalFiles({project.Fixture("Assets/Audio/sfx_beep.wav")}, "Assets", manager);
    const Xora2D::AssetHandle beep = batch.Items.front().Handle;

    CHECK(SyncRegistryWithDisk(registry).Missing.empty(), "nothing missing initially");
    const fs::path file = project.Root() / "Assets/Audio/sfx_beep.wav";
    const fs::path aside = project.Fixtures() / "aside.wav";
    fs::rename(file, aside);
    RegistrySyncReport report = SyncRegistryWithDisk(registry);
    CHECK(report.Missing.size() == 1 && report.NewlyMissing == 1 && !registry.FindByHandle(beep)->Available,
          "file deleted outside the editor detected");
    std::string error;
    const auto listing = ListDirectory("Assets/Audio", registry, error);
    const ContentBrowserEntry* ghost = FindEntry(listing, "sfx_beep.wav");
    CHECK(ghost != nullptr && ghost->IsMissing() && ghost->RegisteredHandle == beep, "missing asset shown as ghost entry");
    Xora2D::Scene scene;
    CHECK(!AssignAssetToEntity(scene, scene.CreateEntity("E"), beep, registry).Success, "missing asset cannot be assigned");
    fs::rename(aside, file);
    report = SyncRegistryWithDisk(registry);
    CHECK(report.Missing.empty() && report.Restored == 1 && registry.FindByHandle(beep)->Available,
          "restored file detected");

    fs::rename(file, aside);
    (void)SyncRegistryWithDisk(registry);
    CHECK(ForgetMissingAsset(beep, manager).Success && !registry.Contains(beep), "missing asset can be forgotten");

    WriteText(project.Root() / "Assets/Textures/Dropped.png", ReadText(project.Fixture("Assets/Textures/Xora2D_Logo.png")));
    const auto textures = ListDirectory("Assets/Textures", registry, error);
    CHECK(FindEntry(textures, "Dropped.png") != nullptr &&
              FindEntry(textures, "Dropped.png")->Status == ContentImportStatus::NotRegistered,
          "file added outside the editor shown as not imported");
    CHECK(RegisterExistingFile("Assets/Textures/Dropped.png", manager).Success &&
              registry.FindByRelativePath("Textures/Dropped.png") != nullptr,
          "external file registered in place");
    WriteText(project.Root() / "Assets/Textures/Junk.png", "junk");
    CHECK(!RegisterExistingFile("Assets/Textures/Junk.png", manager).Success &&
              registry.FindByRelativePath("Textures/Junk.png") == nullptr &&
              fs::exists(project.Root() / "Assets/Textures/Junk.png"),
          "invalid external file not registered (and not deleted)");
}

void TestThumbnails() {
    XORA_LOG_INFO("TEST content: thumbnails");
    TempProject project("thumbs");
    const fs::path logo = project.Fixture("Assets/Textures/Xora2D_Logo.png");
    const fs::path sheet = project.Fixture("Assets/Textures/DemoSpriteSheet.png");

    std::string error;
    const auto thumbnail = GenerateThumbnail(sheet, 32, error);
    CHECK(thumbnail.has_value() && thumbnail->Width <= 32 && thumbnail->Height <= 32, "thumbnail fits requested size");
    if (thumbnail) {
        const double sourceAspect = static_cast<double>(thumbnail->SourceWidth) / thumbnail->SourceHeight;
        const double thumbAspect = static_cast<double>(thumbnail->Width) / thumbnail->Height;
        CHECK(std::abs(sourceAspect - thumbAspect) < 0.15, "thumbnail preserves aspect ratio");
        CHECK(thumbnail->Pixels.size() == static_cast<std::size_t>(thumbnail->Width) * thumbnail->Height * 4, "RGBA8 pixels");
    }
    const auto small = GenerateThumbnail(sheet, 4096, error);
    CHECK(small.has_value() && small->Width == small->SourceWidth, "thumbnails never upscale");
    const fs::path corrupt = project.FixtureText("corrupt.png", "not an image");
    CHECK(!GenerateThumbnail(corrupt, 64, error).has_value() && !error.empty(), "corrupt image yields error");

    AssetThumbnailCache cache(64, 2);
    std::uint32_t nextId = 1;
    cache.SetUploader([&](const ThumbnailImage& image) {
        return std::make_unique<FakeTexture>(image.Width, image.Height, nextId++);
    });
    const auto mtime = fs::last_write_time(logo);
    CHECK(cache.Request("logo", logo, mtime).Status == AssetThumbnailCache::State::Pending, "first request is pending");
    CHECK(cache.WaitIdle(std::chrono::seconds(10)), "worker finishes");
    CHECK(cache.ProcessCompleted(8) == 1, "one upload processed");
    const auto ready = cache.Request("logo", logo, mtime);
    CHECK(ready.Status == AssetThumbnailCache::State::Ready && ready.RendererId != 0 && ready.Width <= 64,
          "thumbnail ready after upload");
    (void)cache.Request("logo", logo, mtime);
    CHECK(cache.GetGeneratedCount() == 1, "cached thumbnail is not regenerated");

    const auto newer = mtime + std::chrono::seconds(5);
    fs::last_write_time(logo, newer);
    CHECK(cache.Request("logo", logo, newer).Status == AssetThumbnailCache::State::Pending,
          "modified source invalidates cached thumbnail");
    CHECK(cache.WaitIdle(std::chrono::seconds(10)), "regeneration finishes");
    cache.ProcessCompleted(8);
    CHECK(cache.GetGeneratedCount() == 2 && cache.Request("logo", logo, newer).Status == AssetThumbnailCache::State::Ready,
          "thumbnail regenerated after change");

    (void)cache.Request("bad", corrupt, fs::last_write_time(corrupt));
    CHECK(cache.WaitIdle(std::chrono::seconds(10)), "corrupt decode finishes");
    cache.ProcessCompleted(8);
    const auto failed = cache.Request("bad", corrupt, fs::last_write_time(corrupt));
    CHECK(failed.Status == AssetThumbnailCache::State::Failed && !failed.Error.empty(), "corrupt image marked failed");

    (void)cache.Request("sheet", sheet, fs::last_write_time(sheet));
    CHECK(cache.GetEntryCount() <= 2, "cache respects capacity (LRU eviction)");
    cache.InvalidatePrefix("logo");
    CHECK(cache.Request("logo", logo, newer).Status == AssetThumbnailCache::State::Pending, "invalidate drops entry");
    cache.Shutdown();
    CHECK(true, "thumbnail worker shuts down cleanly");
}

void TestDetailsAndAssignment() {
    XORA_LOG_INFO("TEST content: details and scene assignment");
    TempProject project("details");
    Xora2D::AssetRegistry registry;
    Xora2D::AssetManager manager(registry);
    manager.SetGraphicsResourcesEnabled(false);
    auto batch = ImportExternalFiles({project.Fixture("Assets/Textures/DemoSpriteSheet.png"),
                                      project.Fixture("Assets/Audio/sfx_beep.wav")},
                                     "Assets", manager);
    WriteText(project.Root() / "Assets/Animations/Walk.anim.yaml",
              "Version: 1\nName: Walk\nTextureRelativePath: Textures/DemoSpriteSheet.png\nLoop: true\n"
              "DefaultFrameDuration: 0.25\nSpriteSheet:\n  Columns: 4\n  Rows: 1\n  FrameWidth: 32\n  FrameHeight: 32\n"
              "  FrameCount: 4\n");
    const Xora2D::AssetHandle walk = manager.RegisterAnimationClipAtRelativePath("Animations/Walk.anim.yaml").Handle;
    const Xora2D::AssetHandle sheet = registry.FindByRelativePath("Textures/DemoSpriteSheet.png")->Handle;
    const Xora2D::AssetHandle beep = registry.FindByRelativePath("Audio/sfx_beep.wav")->Handle;
    const auto snapshot = registry.ListAssets();

    const AssetDetails texture = GatherAssetDetails("Assets/Textures/DemoSpriteSheet.png", snapshot);
    CHECK(texture.Texture.has_value() && texture.Texture->Width == 128 && texture.Texture->Height == 32 &&
              texture.Metadata.has_value() && texture.Metadata->Handle == sheet,
          "texture details: dimensions and handle");
    const AssetDetails audio = GatherAssetDetails("Assets/Audio/sfx_beep.wav", snapshot);
    CHECK(audio.Audio.has_value() && audio.Audio->DurationSeconds > 0.0 && audio.Audio->SampleRate > 0,
          "audio details: duration and sample rate");
    const AssetDetails animation = GatherAssetDetails("Assets/Animations/Walk.anim.yaml", snapshot);
    CHECK(animation.Animation.has_value() && animation.Animation->FrameCount == 4 &&
              std::abs(animation.Animation->TotalDurationSeconds - 1.0f) < 0.01f &&
              animation.Animation->TextureHandle == sheet,
          "animation details: frames, duration, texture");
    WriteText(project.Root() / "Assets/Scripts/Tool.py", "line1\nline2\nline3\n");
    const AssetDetails script = GatherAssetDetails("Assets/Scripts/Tool.py", snapshot);
    CHECK(script.Script.has_value() && script.Script->LineCount == 3 && script.Script->Preview.starts_with("line1"),
          "script details: read-only preview without execution");
    const AssetDetails missing = GatherAssetDetails("Assets/Textures/Nope.png", snapshot);
    CHECK(!missing.ExistsOnDisk && !missing.Error.empty(), "missing file details report error");

    // Assignment + serialization round trip.
    Xora2D::Scene scene;
    scene.SetAssetManager(&manager);
    Xora2D::Entity hero = scene.CreateEntity("Hero");
    CHECK(AssignAssetToEntity(scene, hero, sheet, registry).Success &&
              hero.GetComponent<Xora2D::SpriteRendererComponent>().TextureAsset == sheet,
          "texture assigned to SpriteRenderer (component added)");
    CHECK(AssignAssetToEntity(scene, hero, beep, registry).Success &&
              hero.GetComponent<Xora2D::AudioSourceComponent>().ClipAsset == beep,
          "audio assigned to AudioSource");
    CHECK(AssignAssetToEntity(scene, hero, walk, registry).Success &&
              hero.GetComponent<Xora2D::AnimationComponent>().DefaultClip == walk &&
              hero.GetComponent<Xora2D::AnimationComponent>().ClipAssets.size() == 1,
          "animation clip assigned as default clip");
    CHECK(AssignAssetToEntity(scene, hero, walk, registry).Success &&
              hero.GetComponent<Xora2D::AnimationComponent>().ClipAssets.size() == 1,
          "re-assigning clip does not duplicate clip list entry");
    CHECK(!AssignAssetToEntity(scene, hero, Xora2D::AssetHandle(0xdeadbeef), registry).Success,
          "unregistered handle rejected");
    CHECK(!AssignAssetToEntity(scene, Xora2D::Entity{}, sheet, registry).Success, "invalid entity rejected");

    const fs::path sceneFile = project.Root() / "Scenes/Assigned.scene.yaml";
    CHECK(Xora2D::SceneSerializer::Serialize(scene, sceneFile).Success, "serialize assigned scene");
    Xora2D::Scene loaded;
    CHECK(Xora2D::SceneSerializer::Deserialize(loaded, sceneFile, &manager).Success, "deserialize assigned scene");
    const Xora2D::Entity loadedHero = loaded.FindEntityByName("Hero");
    CHECK(loadedHero.IsValid() && loadedHero.GetComponent<Xora2D::SpriteRendererComponent>().TextureAsset == sheet &&
              loadedHero.GetComponent<Xora2D::AudioSourceComponent>().ClipAsset == beep &&
              loadedHero.GetComponent<Xora2D::AnimationComponent>().DefaultClip == walk,
          "assigned handles survive serialization round-trip");
    const AssetDetails sceneDetails = GatherAssetDetails("Scenes/Assigned.scene.yaml", registry.ListAssets());
    CHECK(sceneDetails.Scene.has_value() && sceneDetails.Scene->EntityCount == 1, "scene details: entity count");
}

} // namespace

int RunContentBrowserTests() {
    const int before = g_contentFailures;
    XORA_LOG_INFO("TEST content browser started");
    TestPathSafety();
    TestNavigationState();
    TestQuery();
    TestEnumerationAndFolders();
    TestImport();
    TestRenameMoveDelete();
    TestMissingAssets();
    TestThumbnails();
    TestDetailsAndAssignment();
    XORA_LOG_INFO("TEST content browser finished ({} failure(s))", g_contentFailures - before);
    return g_contentFailures - before;
}
