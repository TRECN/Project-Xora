// Milestone 17: project files, project creation from templates, recent projects and project switching.
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Assets/AssetRegistrySerializer.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Core/Version.h"
#include "Xora2D/Events/KeyEvent.h"
#include "Xora2D/Input/Input.h"
#include "Xora2D/Input/InputManager.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Physics/PhysicsWorld2D.h"
#include "Xora2D/Project/Project.h"
#include "Xora2D/Project/ProjectSerializer.h"
#include "Xora2D/Project/ProjectTemplates.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"
#include "Xora2D/Scripting/PythonScriptEngine.h"
#include "Xora2D/Scripting/ScriptServices.h"

#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/Project/RecentProjects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Xora2D::ProjectError;
using Xora2D::ProjectTemplate;
using Xora2DEditor::EditorContext;
using Xora2DEditor::RecentProjectList;

int g_failures = 0;

void Check(bool condition, const std::string& description) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {}", description);
    ++g_failures;
}

// A unique scratch folder (canonical, so comparisons with project paths are exact). Removed on destruction.
class TempDir {
public:
    TempDir() {
        std::mt19937_64 generator{std::random_device{}()};
        const fs::path path = fs::temp_directory_path() / ("xora2d_projects_" + std::to_string(generator() % 100000000));
        fs::create_directories(path);
        m_root = fs::weakly_canonical(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    [[nodiscard]] const fs::path& Path() const { return m_root; }

private:
    fs::path m_root;
};

std::string ReadText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void WriteText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

bool HasStagingLeftovers(const fs::path& parent) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(parent, ec)) {
        if (entry.path().filename().string().starts_with(".xora_create_")) {
            return true;
        }
    }
    return false;
}

Xora2D::ProjectCreateResult Create(const fs::path& parent, const std::string& name, ProjectTemplate projectTemplate) {
    Xora2D::ProjectCreateOptions options{};
    options.Name = name;
    options.ParentDirectory = parent;
    options.Template = projectTemplate;
    return Xora2D::CreateProject(options);
}

std::size_t CountEntities(const Xora2D::Scene& scene) {
    return scene.GetEntityCount();
}

// ---------------------------------------------------------------------------------------------------------------
// Creation
// ---------------------------------------------------------------------------------------------------------------

void TestProjectCreation() {
    XORA_LOG_INFO("TEST project creation");
    TempDir temp;

    const auto created = Create(temp.Path(), "My Game", ProjectTemplate::Empty2D);
    Check(created.Success, "create Empty 2D project: " + created.Message);
    Check(created.ProjectRoot == temp.Path() / "My Game", "project created in <parent>/<name>");
    Check(created.ProjectFile == temp.Path() / "My Game" / "My Game.xora", "project file is <name>.xora");
    Check(fs::is_regular_file(created.ProjectFile), "project file written");

    bool allFolders = true;
    for (const char* folder : {"Assets", "Assets/Textures", "Assets/Audio", "Assets/Scripts", "Assets/Animations",
                               "Assets/Registry", "Scenes", "Config", "ProjectSettings"}) {
        if (!fs::is_directory(created.ProjectRoot / folder)) {
            XORA_LOG_ERROR("    missing folder {}", folder);
            allFolders = false;
        }
    }
    Check(allFolders, "standard directory structure generated");
    Check(fs::is_regular_file(created.ProjectRoot / "Scenes/Main.scene.yaml"), "main scene generated");
    Check(fs::is_regular_file(created.ProjectRoot / "Assets/Registry/AssetRegistry.yaml"), "asset registry generated");
    Check(!HasStagingLeftovers(temp.Path()), "no staging folder left after success");

    const Xora2D::ProjectFileResult file = Xora2D::ProjectSerializer::LoadFile(created.ProjectFile);
    Check(file.Success, "generated project file parses");
    Check(file.Config.Name == "My Game", "metadata: name");
    Check(file.Config.FormatVersion == Xora2D::kProjectFormatVersion, "metadata: format version");
    Check(file.Config.EngineVersion == Xora2D::GetEngineVersionString(), "metadata: engine version");
    Check(file.Config.StartupScene == "Scenes/Main.scene.yaml", "metadata: startup scene");
    Check(file.Config.Directories == Xora2D::ProjectDirectories{}, "metadata: default directories");
    Check(file.Config.Settings == Xora2D::ProjectSettings{}, "metadata: default settings");

    const std::string text = ReadText(created.ProjectFile);
    Check(text.find("XoraProject:") != std::string::npos && text.find("FormatVersion: 1") != std::string::npos,
          "project file is versioned YAML");

    // Several projects side by side.
    const auto second = Create(temp.Path(), "Second_Game-2", ProjectTemplate::PhysicsDemo);
    const auto third = Create(temp.Path(), "Third.Game", ProjectTemplate::Basic2DGame);
    Check(second.Success && third.Success, "multiple projects can be created in one folder");
}

void TestTemplates() {
    XORA_LOG_INFO("TEST project templates");
    TempDir temp;
    for (const Xora2D::ProjectTemplateInfo& info : Xora2D::GetProjectTemplates()) {
        Check(Xora2D::FindProjectTemplate(info.Id) == info.Template, std::string("template id lookup: ") + info.Id);
    }
    Check(Xora2D::GetProjectTemplates().size() == 3, "three templates are offered");

    struct Expectation {
        ProjectTemplate Template;
        const char* Name;
    };
    for (const Expectation& expectation : {Expectation{ProjectTemplate::Empty2D, "EmptyT"},
                                           Expectation{ProjectTemplate::Basic2DGame, "BasicT"},
                                           Expectation{ProjectTemplate::PhysicsDemo, "PhysicsT"}}) {
        const auto created = Create(temp.Path(), expectation.Name, expectation.Template);
        Check(created.Success, std::string("create template project ") + expectation.Name);
        const Xora2D::ProjectLoadResult load = Xora2D::Project::Load(created.ProjectFile);
        Check(load.Success && load.Warnings.empty(), std::string("template project loads without warnings: ") +
                                                         expectation.Name);
        if (!load.Success) {
            continue;
        }
        EditorContext context;
        const Xora2DEditor::ProjectOpenResult open = context.OpenProject(load.Loaded);
        Check(open.Success && !open.StartupSceneMissing, std::string("template startup scene opens: ") + expectation.Name);
        const auto scene = context.GetEditScene();
        const Xora2D::Entity camera = scene->FindEntityByName("MainCamera");
        Check(camera.IsValid() && camera.HasComponent<Xora2D::CameraComponent>() &&
                  camera.GetComponent<Xora2D::CameraComponent>().Primary,
              std::string("template has a primary camera: ") + expectation.Name);

        if (expectation.Template == ProjectTemplate::Empty2D) {
            Check(CountEntities(*scene) == 1, "empty template: only the camera");
        }
        if (expectation.Template == ProjectTemplate::Basic2DGame) {
            const Xora2D::Entity player = scene->FindEntityByName("Player");
            const Xora2D::Entity ground = scene->FindEntityByName("Ground");
            Check(player.IsValid() && player.HasComponent<Xora2D::RigidBody2DComponent>() &&
                      player.HasComponent<Xora2D::PythonScriptComponent>(),
                  "basic game: player with physics and a script");
            Check(ground.IsValid() && ground.GetComponent<Xora2D::RigidBody2DComponent>().Type ==
                                          Xora2D::RigidBodyType2D::Static,
                  "basic game: static ground");
            if (player.IsValid() && player.HasComponent<Xora2D::PythonScriptComponent>()) {
                const auto& script = player.GetComponent<Xora2D::PythonScriptComponent>();
                const Xora2D::AssetMetadata* meta = context.GetAssetRegistry().FindByHandle(script.ScriptAsset);
                Check(meta != nullptr && meta->RelativePath == "Scripts/PlayerController.py" &&
                          meta->Type == Xora2D::AssetType::PythonScript,
                      "basic game: script reference resolves through the project registry");
                Check(script.ClassName == "PlayerController", "basic game: script class name");
            }
            Check(fs::is_regular_file(created.ProjectRoot / "Assets/Scripts/PlayerController.py"),
                  "basic game: script file inside the project");
        }
        if (expectation.Template == ProjectTemplate::PhysicsDemo) {
            int dynamicBodies = 0;
            int circles = 0;
            for (const Xora2D::UUID& id : scene->GetEntityUUIDs()) {
                const Xora2D::Entity entity = scene->FindEntityByUUID(id);
                if (entity.HasComponent<Xora2D::RigidBody2DComponent>() &&
                    entity.GetComponent<Xora2D::RigidBody2DComponent>().Type == Xora2D::RigidBodyType2D::Dynamic) {
                    ++dynamicBodies;
                }
                circles += entity.HasComponent<Xora2D::CircleCollider2DComponent>() ? 1 : 0;
            }
            Check(scene->FindEntityByName("Ground").IsValid() && dynamicBodies >= 10 && circles >= 1,
                  "physics demo: ground plus dynamic boxes and balls");

            // Bodies fall and come to rest on the ground in Play mode.
            const Xora2D::UUID boxId = scene->FindEntityByName("Box10").GetUUID();
            const float startY = scene->FindEntityByUUID(boxId).GetComponent<Xora2D::TransformComponent>().Translation.y;
            (void)context.StartPlayMode();
            for (int frame = 0; frame < 240; ++frame) {
                context.UpdatePlayMode(1.0f / 60.0f);
            }
            const float endY =
                context.GetScene()->FindEntityByUUID(boxId).GetComponent<Xora2D::TransformComponent>().Translation.y;
            Check(endY < startY && endY > 0.0f, "physics demo: boxes fall and stay above the ground");
            (void)context.StopPlayMode();
        }

        // Templates do not copy engine sources or engine resources into projects.
        bool engineFilesCopied = false;
        for (const auto& entry : fs::recursive_directory_iterator(created.ProjectRoot)) {
            const std::string extension = entry.path().extension().string();
            if (extension == ".cpp" || extension == ".h" || extension == ".vert" || extension == ".frag") {
                engineFilesCopied = true;
            }
        }
        Check(!engineFilesCopied, std::string("no engine source / shader files in the project: ") + expectation.Name);
    }
}

void TestInvalidCreation() {
    XORA_LOG_INFO("TEST invalid project creation");
    TempDir temp;
    for (const std::string& name : {std::string(), std::string("../Escape"), std::string("a/b"), std::string("a\\b"),
                                    std::string(".hidden"), std::string("-dash"), std::string("colon:name"),
                                    std::string("trailing "), std::string("dot."), std::string(65, 'x'),
                                    std::string("tab\tname"), std::string("star*")}) {
        const auto result = Create(temp.Path(), name, ProjectTemplate::Empty2D);
        Check(!result.Success && result.Error == ProjectError::InvalidName, "invalid name rejected: '" + name + "'");
    }
    Check(fs::is_empty(temp.Path()), "rejected names create nothing");
    Check(!fs::exists(temp.Path().parent_path() / "Escape"), "path traversal through the name is impossible");

    const auto relative = Create("relative/folder", "Game", ProjectTemplate::Empty2D);
    Check(!relative.Success && relative.Error == ProjectError::InvalidLocation, "relative location rejected");
    const auto missing = Create(temp.Path() / "does-not-exist", "Game", ProjectTemplate::Empty2D);
    Check(!missing.Success && missing.Error == ProjectError::InvalidLocation, "missing location rejected");
    WriteText(temp.Path() / "plainfile", "x");
    const auto fileParent = Create(temp.Path() / "plainfile", "Game", ProjectTemplate::Empty2D);
    Check(!fileParent.Success && fileParent.Error == ProjectError::InvalidLocation, "file as location rejected");
    const auto empty = Create({}, "Game", ProjectTemplate::Empty2D);
    Check(!empty.Success && empty.Error == ProjectError::InvalidLocation, "empty location rejected");
}

void TestExistingDirectoryHandling() {
    XORA_LOG_INFO("TEST existing directory handling");
    TempDir temp;

    // A non-empty folder with the project name is never touched.
    WriteText(temp.Path() / "Taken/notes.txt", "user data");
    const auto taken = Create(temp.Path(), "Taken", ProjectTemplate::Basic2DGame);
    Check(!taken.Success && taken.Error == ProjectError::AlreadyExists, "non-empty folder is refused");
    Check(ReadText(temp.Path() / "Taken/notes.txt") == "user data" &&
              std::distance(fs::directory_iterator(temp.Path() / "Taken"), fs::directory_iterator{}) == 1,
          "existing folder content left untouched");

    // A file with the project name.
    WriteText(temp.Path() / "FileName", "x");
    const auto fileClash = Create(temp.Path(), "FileName", ProjectTemplate::Empty2D);
    Check(!fileClash.Success && fileClash.Error == ProjectError::AlreadyExists, "file with the project name refused");

    // An empty folder (e.g. made by a folder picker) is reused.
    fs::create_directories(temp.Path() / "EmptyFolder");
    const auto reused = Create(temp.Path(), "EmptyFolder", ProjectTemplate::Empty2D);
    Check(reused.Success && fs::is_regular_file(temp.Path() / "EmptyFolder/EmptyFolder.xora"),
          "empty folder with the project name is reused");

    // Creating the same project twice fails the second time and keeps the first intact.
    const std::string before = ReadText(reused.ProjectFile);
    const auto again = Create(temp.Path(), "EmptyFolder", ProjectTemplate::PhysicsDemo);
    Check(!again.Success && again.Error == ProjectError::AlreadyExists && ReadText(reused.ProjectFile) == before,
          "second creation refused, first project unchanged");
    Check(!HasStagingLeftovers(temp.Path()), "no staging folders left after refusals");
}

void TestFailedCreationCleanup() {
    XORA_LOG_INFO("TEST failed creation cleanup");
    TempDir temp;
    WriteText(temp.Path() / "Sibling/keep.txt", "keep me");
    for (const char* failingStep : {"directories", "assets", "scene", "project-file", "finalize"}) {
        Xora2D::ProjectCreateOptions options{};
        options.Name = "Doomed";
        options.ParentDirectory = temp.Path();
        options.Template = ProjectTemplate::Basic2DGame;
        std::vector<std::string> stepsSeen;
        options.BeforeStep = [&](std::string_view step) {
            stepsSeen.emplace_back(step);
            return step != failingStep;
        };
        const auto result = Xora2D::CreateProject(options);
        Check(!result.Success && result.Message.find(failingStep) != std::string::npos,
              std::string("creation fails at step '") + failingStep + "'");
        Check(!fs::exists(temp.Path() / "Doomed"), std::string("no project folder after failure at ") + failingStep);
        Check(!HasStagingLeftovers(temp.Path()), std::string("staging folder removed after failure at ") + failingStep);
    }
    Check(ReadText(temp.Path() / "Sibling/keep.txt") == "keep me", "unrelated files survive failed creations");

    // Failure with a pre-existing empty target folder keeps that folder.
    fs::create_directories(temp.Path() / "Prepared");
    Xora2D::ProjectCreateOptions options{};
    options.Name = "Prepared";
    options.ParentDirectory = temp.Path();
    options.BeforeStep = [](std::string_view step) { return step != "scene"; };
    const auto result = Xora2D::CreateProject(options);
    Check(!result.Success && fs::is_directory(temp.Path() / "Prepared") && fs::is_empty(temp.Path() / "Prepared"),
          "pre-existing empty folder kept (and left empty) after a failed creation");
}

// ---------------------------------------------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------------------------------------------

fs::path WriteProjectFile(const fs::path& root, const std::string& name, const std::string& yaml) {
    fs::create_directories(root / "Assets");
    const fs::path file = root / (name + ".xora");
    WriteText(file, yaml);
    return file;
}

void TestProjectLoading() {
    XORA_LOG_INFO("TEST project loading");
    TempDir temp;
    const auto created = Create(temp.Path(), "Loadable", ProjectTemplate::Basic2DGame);
    const Xora2D::ProjectLoadResult load = Xora2D::Project::Load(created.ProjectFile);
    Check(load.Success && load.Loaded != nullptr, "valid project loads");
    if (load.Success) {
        const auto& project = *load.Loaded;
        Check(project.GetName() == "Loadable" && project.GetRootDirectory() == created.ProjectRoot,
              "loaded project name and root");
        Check(project.GetAssetDirectory() == created.ProjectRoot / "Assets" &&
                  project.GetSceneDirectory() == created.ProjectRoot / "Scenes" &&
                  project.GetScriptDirectory() == created.ProjectRoot / "Assets/Scripts" &&
                  project.GetAssetRegistryPath() == created.ProjectRoot / "Assets/Registry/AssetRegistry.yaml",
              "project directories resolve under the root");
        Check(project.HasStartupScene(), "startup scene found");
        Check(project.ListScenes() == std::vector<std::string>{"Scenes/Main.scene.yaml"}, "scenes listed");
    }
    Check(Xora2D::Project::FindProjectFile(created.ProjectRoot) == created.ProjectFile, "project file found in folder");

    const fs::path root = temp.Path() / "Broken";
    struct Case {
        const char* Label;
        std::string Yaml;
        ProjectError Expected;
    };
    const std::vector<Case> cases = {
        {"invalid YAML", "XoraProject: [unclosed\n  Name: x", ProjectError::InvalidYaml},
        {"not a project file", "Something: 1\n", ProjectError::NotAProjectFile},
        {"empty file", "", ProjectError::NotAProjectFile},
        {"unsupported newer version", "XoraProject:\n  FormatVersion: 99\n  Name: Future\n",
         ProjectError::UnsupportedVersion},
        {"unsupported version 0", "XoraProject:\n  FormatVersion: 0\n  Name: Old\n", ProjectError::UnsupportedVersion},
        {"missing FormatVersion", "XoraProject:\n  Name: NoVersion\n", ProjectError::MissingField},
        {"missing Name", "XoraProject:\n  FormatVersion: 1\n", ProjectError::MissingField},
        {"invalid name", "XoraProject:\n  FormatVersion: 1\n  Name: ../evil\n", ProjectError::InvalidName},
        {"non-numeric version", "XoraProject:\n  FormatVersion: abc\n  Name: X\n", ProjectError::InvalidValue},
        {"bad window size",
         "XoraProject:\n  FormatVersion: 1\n  Name: X\n  Settings:\n    Window: {Width: -5, Height: 720}\n",
         ProjectError::InvalidValue},
        {"bad gravity", "XoraProject:\n  FormatVersion: 1\n  Name: X\n  Settings:\n    Physics: {Gravity: [1]}\n",
         ProjectError::InvalidValue},
        {"asset directory escaping the root",
         "XoraProject:\n  FormatVersion: 1\n  Name: X\n  Directories: {Assets: ../outside}\n", ProjectError::InvalidValue},
        {"absolute scene directory",
         "XoraProject:\n  FormatVersion: 1\n  Name: X\n  Directories: {Scenes: /etc}\n", ProjectError::InvalidValue},
        {"startup scene escaping the root",
         "XoraProject:\n  FormatVersion: 1\n  Name: X\n  StartupScene: ../../x.scene.yaml\n", ProjectError::InvalidValue},
        {"startup scene not a scene",
         "XoraProject:\n  FormatVersion: 1\n  Name: X\n  StartupScene: Scenes/readme.txt\n", ProjectError::InvalidValue},
    };
    for (const Case& testCase : cases) {
        const fs::path file = WriteProjectFile(root, "Broken", testCase.Yaml);
        const Xora2D::ProjectLoadResult result = Xora2D::Project::Load(file);
        Check(!result.Success && result.Error == testCase.Expected && !result.Message.empty(),
              std::string("rejected: ") + testCase.Label + " (" + Xora2D::ToString(result.Error) + ": " +
                  result.Message + ")");
    }

    const auto missingFile = Xora2D::Project::Load(temp.Path() / "Nope/Nope.xora");
    Check(!missingFile.Success && missingFile.Error == ProjectError::FileNotFound, "missing project file reported");
    WriteText(temp.Path() / "Wrong.yaml", "XoraProject:\n  FormatVersion: 1\n  Name: Wrong\n");
    const auto wrongExtension = Xora2D::Project::Load(temp.Path() / "Wrong.yaml");
    Check(!wrongExtension.Success && wrongExtension.Error == ProjectError::NotAProjectFile, "non-.xora file refused");

    // Minimal valid file: defaults fill everything that is optional.
    const fs::path minimal =
        WriteProjectFile(temp.Path() / "Minimal", "Minimal", "XoraProject:\n  FormatVersion: 1\n  Name: Minimal\n");
    const auto minimalLoad = Xora2D::Project::Load(minimal);
    Check(minimalLoad.Success && minimalLoad.Loaded->GetSettings() == Xora2D::ProjectSettings{} &&
              minimalLoad.Loaded->GetConfig().StartupScene.empty(),
          "minimal project file loads with defaults");
    Check(minimalLoad.Success && !minimalLoad.Warnings.empty(), "missing optional folders / startup scene are warnings");

    // Round trip.
    Xora2D::ProjectConfig config{};
    config.Name = "RoundTrip";
    config.StartupScene = "Scenes/Level 1.scene.yaml";
    config.Settings.WindowWidth = 1920;
    config.Settings.WindowHeight = 1080;
    config.Settings.VSync = false;
    config.Settings.Gravity = {1.5f, -20.0f};
    config.Settings.ScriptingEnabled = false;
    config.Settings.ProjectVersion = "2.3.4-beta";
    const auto parsed = Xora2D::ProjectSerializer::Parse(Xora2D::ProjectSerializer::Emit(config));
    Check(parsed.Success && parsed.Config == config, "project file round-trips every field");
}

void TestMissingStartupSceneAndDirectories() {
    XORA_LOG_INFO("TEST missing startup scene / directories");
    TempDir temp;
    const auto created = Create(temp.Path(), "Gappy", ProjectTemplate::Empty2D);
    fs::remove(created.ProjectRoot / "Scenes/Main.scene.yaml");
    fs::remove_all(created.ProjectRoot / "Config");

    const auto load = Xora2D::Project::Load(created.ProjectFile);
    Check(load.Success, "project with a missing startup scene still loads");
    const auto mentions = [&load](const std::string& text) {
        return std::any_of(load.Warnings.begin(), load.Warnings.end(),
                           [&text](const std::string& warning) { return warning.find(text) != std::string::npos; });
    };
    Check(mentions("Scenes/Main.scene.yaml") && mentions("Config"), "missing startup scene and folder are warnings");
    Check(!load.Loaded->HasStartupScene(), "HasStartupScene is false");

    {
        EditorContext context;
        const auto open = context.OpenProject(load.Loaded);
        Check(open.Success && open.StartupSceneMissing, "editor opens the project with an empty scene");
        Check(context.GetEditScene() != nullptr && context.GetEditScene()->GetEntityCount() == 1 &&
                  context.GetEditScene()->FindEntityByName("MainCamera").IsValid() && context.GetScenePath().empty(),
              "replacement scene is unsaved and holds only a camera");
        Check(fs::is_directory(created.ProjectRoot / "Config"), "missing standard folder recreated on open");

        const auto replacement = context.CreateScene("Scenes/Start.scene.yaml", true);
        Check(replacement.Success && fs::is_regular_file(created.ProjectRoot / "Scenes/Start.scene.yaml"),
              "replacement startup scene created");
        Check(context.GetScenePath() == "Scenes/Start.scene.yaml" &&
                  context.GetEditScene()->FindEntityByName("MainCamera").IsValid(),
              "replacement scene opened with a camera");
        const auto reload = Xora2D::Project::Load(created.ProjectFile);
        Check(reload.Success && reload.Loaded->GetConfig().StartupScene == "Scenes/Start.scene.yaml" &&
                  reload.Loaded->HasStartupScene(),
              "new startup scene persisted in the project file");
        const auto existing = context.CreateScene("Scenes/Start.scene.yaml", false);
        Check(!existing.Success, "creating a scene over an existing file is refused");
        const auto escape = context.SaveSceneAs("../Outside.scene.yaml");
        Check(!escape.Success && !fs::exists(temp.Path() / "Outside.scene.yaml"), "saving a scene outside is refused");
        context.CloseProject();
    }

    // Missing asset directory is an error (nothing could be resolved).
    fs::remove_all(created.ProjectRoot / "Assets");
    const auto noAssets = Xora2D::Project::Load(created.ProjectFile);
    Check(!noAssets.Success && noAssets.Error == ProjectError::MissingDirectory &&
              noAssets.Message.find("Assets") != std::string::npos,
          "missing asset directory reported");
}

void TestPathResolution() {
    XORA_LOG_INFO("TEST project-relative path resolution");
    TempDir temp;
    const auto created = Create(temp.Path(), "Paths", ProjectTemplate::Empty2D);
    const auto load = Xora2D::Project::Load(created.ProjectFile);
    if (!load.Success) {
        Check(false, "load project for path tests");
        return;
    }
    const Xora2D::Project& project = *load.Loaded;
    const fs::path root = project.GetRootDirectory();
    Check(project.ResolvePath("Scenes/Main.scene.yaml") == root / "Scenes/Main.scene.yaml", "relative path resolves");
    Check(project.ResolvePath("Scenes\\Main.scene.yaml") == root / "Scenes/Main.scene.yaml", "backslashes normalized");
    Check(project.ResolvePath("./Assets/./Textures") == root / "Assets/Textures", "'.' segments removed");
    Check(project.ResolvePath("") == root, "empty path is the root");
    Check(!project.ResolvePath("../Other/file").has_value(), "'..' rejected");
    Check(!project.ResolvePath("Assets/../../x").has_value(), "nested '..' rejected");
    Check(!project.ResolvePath("/etc/passwd").has_value(), "absolute path rejected");
    Check(!project.ResolvePath("C:/Windows").has_value(), "drive letter rejected");

    WriteText(temp.Path() / "Secret/secret.txt", "outside");
    std::error_code ec;
    fs::create_directory_symlink(temp.Path() / "Secret", root / "Assets/Link", ec);
    if (!ec) {
        Check(!project.ResolvePath("Assets/Link/secret.txt").has_value(), "symlink escaping the project rejected");
    }

    Check(project.MakeRelative(root / "Assets/Textures/hero.png") == std::string("Assets/Textures/hero.png"),
          "absolute -> project-relative");
    Check(!project.MakeRelative(temp.Path() / "Secret/secret.txt").has_value(), "outside path has no relative form");
    Check(project.Contains(root / "Scenes") && !project.Contains(temp.Path()), "Contains() checks the root");

    // The content root follows the open project: asset paths resolve inside it.
    {
        EditorContext context;
        (void)context.OpenProject(load.Loaded);
        Check(Xora2D::GetContentRoot() == root && Xora2D::HasContentRootOverride(), "content root is the project root");
        Check(Xora2D::GetAssetRoot() == root / "Assets", "asset root inside the project");
        Check(Xora2D::ResolveAssetPath("Textures/a.png") == root / "Assets/Textures/a.png", "asset path resolution");
        Check(Xora2D::ResolveAssetPath("../../escape.png").empty(), "asset traversal rejected");
        Check(context.GetAssetRegistryFilePath() == project.GetAssetRegistryPath(), "registry path inside the project");
    }
    Check(!Xora2D::HasContentRootOverride() && Xora2D::GetContentRoot() == Xora2D::GetResourceRoot(),
          "content root restored when the editor context goes away");
}

void TestProjectSettingsPersistence() {
    XORA_LOG_INFO("TEST project settings persistence");
    TempDir temp;
    const auto created = Create(temp.Path(), "Settings", ProjectTemplate::Empty2D);
    auto load = Xora2D::Project::Load(created.ProjectFile);
    if (!load.Success) {
        Check(false, "load project for settings tests");
        return;
    }
    const std::string fileBefore = ReadText(created.ProjectFile);

    Xora2D::ProjectConfig invalid = load.Loaded->GetConfig();
    invalid.Settings.WindowWidth = 10;
    const auto rejected = load.Loaded->UpdateConfig(invalid);
    Check(!rejected.Success && load.Loaded->GetSettings().WindowWidth == 1280 &&
              ReadText(created.ProjectFile) == fileBefore,
          "invalid settings rejected; memory and file unchanged");

    Xora2D::ProjectConfig updated = load.Loaded->GetConfig();
    updated.Name = "Renamed Game";
    updated.Settings.WindowWidth = 800;
    updated.Settings.WindowHeight = 600;
    updated.Settings.VSync = false;
    updated.Settings.Gravity = {0.0f, -3.0f};
    updated.Settings.ScriptingEnabled = false;
    updated.Settings.ProjectVersion = "1.0.0";
    const auto saved = load.Loaded->UpdateConfig(updated);
    Check(saved.Success, "valid settings saved");
    const auto reload = Xora2D::Project::Load(created.ProjectFile);
    Check(reload.Success && reload.Loaded->GetConfig().Settings == updated.Settings &&
              reload.Loaded->GetName() == "Renamed Game",
          "settings persisted to the project file");
    Check(fs::is_regular_file(created.ProjectFile) && !fs::exists(created.ProjectRoot / "Renamed Game.xora"),
          "renaming the project keeps the file name");

    // Gravity reaches the runtime scene, never the edit scene.
    EditorContext context;
    context.GetPlaySession().SetHooks({.WireRuntimeScene =
                                           [&context](Xora2D::Scene& scene) {
                                               scene.GetPhysicsWorld().SetGravity(
                                                   context.GetProject()->GetSettings().Gravity);
                                           },
                                       .ClearGameplayInput = {}});
    (void)context.OpenProject(reload.Loaded);
    (void)context.StartPlayMode();
    Check(context.GetScene()->GetPhysicsWorld().GetGravity().y == -3.0f, "project gravity applied in Play mode");
    (void)context.StopPlayMode();
    Check(context.GetEditScene()->GetPhysicsWorld().GetGravity().y == -9.81f, "edit scene gravity untouched");
}

// ---------------------------------------------------------------------------------------------------------------
// Recent projects
// ---------------------------------------------------------------------------------------------------------------

void TestRecentProjects() {
    XORA_LOG_INFO("TEST recent projects");
    TempDir temp;
    const auto alpha = Create(temp.Path(), "Alpha", ProjectTemplate::Empty2D);
    const auto beta = Create(temp.Path(), "Beta", ProjectTemplate::Empty2D);

    RecentProjectList list;
    list.Add("Alpha", alpha.ProjectFile, 100);
    list.Add("Beta", beta.ProjectFile, 200);
    Check(list.GetEntries().size() == 2 && list.GetEntries()[0].Name == "Beta", "newest project listed first");

    // Same project through different spellings of its path.
    list.Add("Alpha", temp.Path() / "Alpha/./Alpha.xora", 300);
    list.Add("Alpha", temp.Path() / "Beta/../Alpha/Alpha.xora", 400);
    Check(list.GetEntries().size() == 2, "duplicate paths collapse into one entry");
    Check(list.GetEntries()[0].Name == "Alpha" && list.GetEntries()[0].LastOpenedUnixSeconds == 400,
          "re-opening moves the project to the front and refreshes the timestamp");
    Check(list.Contains(alpha.ProjectFile) && list.Contains(beta.ProjectFile), "Contains() matches entries");

    // Persistence.
    const fs::path historyFile = temp.Path() / "config/recent_projects.yaml";
    Check(list.Save(historyFile).Success, "history saved");
    RecentProjectList reloaded;
    Check(reloaded.Load(historyFile).Success, "history loaded");
    Check(reloaded.GetEntries().size() == 2 && reloaded.GetEntries()[0].Name == "Alpha" &&
              reloaded.GetEntries()[0].LastOpenedUnixSeconds == 400 && reloaded.GetEntries()[1].Name == "Beta" &&
              reloaded.GetEntries()[1].ProjectFile == list.GetEntries()[1].ProjectFile,
          "order, names, paths and timestamps survive a reload");

    // Removal never touches project files.
    Check(reloaded.Remove(temp.Path() / "Beta/Beta.xora"), "project removed from history");
    Check(!reloaded.Remove(temp.Path() / "Beta/Beta.xora"), "removing twice reports nothing removed");
    Check(fs::is_regular_file(beta.ProjectFile) && fs::is_regular_file(beta.ProjectRoot / "Scenes/Main.scene.yaml"),
          "removed project's files remain intact");
    Check(reloaded.GetEntries().size() == 1 && !reloaded.Contains(beta.ProjectFile), "only the other entry remains");

    // Moved / deleted projects stay listed (so they can be removed) but are reported as unavailable.
    fs::rename(alpha.ProjectRoot, temp.Path() / "AlphaMoved");
    Check(!RecentProjectList::IsAvailable(reloaded.GetEntries()[0]), "moved project is reported missing");
    Check(reloaded.Save(historyFile).Success && reloaded.Load(historyFile).Success &&
              reloaded.GetEntries().size() == 1,
          "missing project entry survives a reload");
    const auto openMissing = Xora2D::Project::Load(reloaded.GetEntries()[0].ProjectFile);
    Check(!openMissing.Success && openMissing.Error == ProjectError::FileNotFound, "opening a moved project fails cleanly");

    // Capacity.
    RecentProjectList bounded;
    for (int index = 0; index < 15; ++index) {
        bounded.Add("P" + std::to_string(index), temp.Path() / ("P" + std::to_string(index)) / "P.xora", index);
    }
    Check(bounded.GetEntries().size() == RecentProjectList::kDefaultCapacity, "history limited to the capacity");
    Check(bounded.GetEntries().front().Name == "P14" && bounded.GetEntries().back().Name == "P5",
          "oldest entries dropped first");

    // Robustness.
    WriteText(temp.Path() / "bad.yaml", "RecentProjects: [unclosed");
    RecentProjectList broken;
    broken.Add("Keep", alpha.ProjectFile, 1);
    Check(!broken.Load(temp.Path() / "bad.yaml").Success && broken.GetEntries().empty(), "invalid history file ignored");
    RecentProjectList none;
    Check(none.Load(temp.Path() / "missing.yaml").Success && none.GetEntries().empty(), "missing history is empty");
}

// ---------------------------------------------------------------------------------------------------------------
// Isolation
// ---------------------------------------------------------------------------------------------------------------

void TestProjectIsolation() {
    XORA_LOG_INFO("TEST project isolation");
    TempDir temp;
    const auto a = Create(temp.Path(), "ProjectA", ProjectTemplate::Basic2DGame);
    const auto b = Create(temp.Path(), "ProjectB", ProjectTemplate::PhysicsDemo);
    WriteText(a.ProjectRoot / "Assets/Audio/OnlyInA.wav", "not really audio");
    WriteText(b.ProjectRoot / "Assets/Audio/OnlyInB.wav", "not really audio");

    auto loadA = Xora2D::Project::Load(a.ProjectFile);
    auto loadB = Xora2D::Project::Load(b.ProjectFile);
    if (!loadA.Success || !loadB.Success) {
        Check(false, "load isolation projects");
        return;
    }

    EditorContext context;
    int projectChanges = 0;
    int scriptResets = 0;
    context.GetHostServices().OnProjectChanged = [&projectChanges]() { ++projectChanges; };
    context.GetHostServices().ResetScriptState = [&scriptResets]() { ++scriptResets; };
    Check(!context.IsContentActive() && !context.HasProject(), "nothing is open initially");

    std::weak_ptr<Xora2D::Scene> sceneA;
    std::weak_ptr<Xora2D::Project> projectA = loadA.Loaded;
    std::weak_ptr<Xora2D::Scene> runtimeA;
    std::vector<Xora2D::AssetHandle> handlesA;
    {
        const auto open = context.OpenProject(loadA.Loaded);
        loadA.Loaded.reset();
        Check(open.Success && context.HasProject() && context.GetProject()->GetName() == "ProjectA",
              "project A opened");
        Check(context.GetAssetRegistry().FindByRelativePath("Audio/OnlyInA.wav") != nullptr &&
                  context.GetAssetRegistry().FindByRelativePath("Audio/OnlyInB.wav") == nullptr,
              "A's registry sees only A's assets");
        Check(context.GetEditScene()->FindEntityByName("Player").IsValid(), "A's startup scene loaded");
        for (const Xora2D::AssetMetadata& meta : context.GetAssetRegistry().ListAssets()) {
            handlesA.push_back(meta.Handle);
        }
        sceneA = context.GetEditScene();
        // Unsaved edit + selection + running play session + navigated content browser.
        Xora2D::Entity unsaved = context.GetEditScene()->CreateEntity("UnsavedInA");
        context.SelectEntity(unsaved);
        (void)context.GetContentBrowser().NavigateTo("Assets/Audio");
        (void)context.StartPlayMode();
        runtimeA = context.GetPlaySession().GetRuntimeScene();
        Check(context.IsPlayModeActive() && !runtimeA.expired(), "A is playing before the switch");
    }

    const auto openB = context.OpenProject(loadB.Loaded);
    loadB.Loaded.reset();
    Check(openB.Success && context.GetProject()->GetName() == "ProjectB", "switched to project B");
    Check(!context.IsPlayModeActive() && runtimeA.expired(), "switching stops Play and frees A's runtime scene");
    Check(sceneA.expired(), "A's edit scene is released (no stale scene references)");
    Check(projectA.expired(), "A's project object is released");
    Check(!context.GetSelectedEntity().IsValid() && !context.GetSelection().GetSelectedUUID().has_value(),
          "selection cleared on switch");
    Check(context.GetContentBrowser().GetCurrentDirectory() == "Assets", "content browser reset to Assets");
    Check(Xora2D::GetContentRoot() == b.ProjectRoot, "content root moved to B");
    Check(context.GetAssetRegistry().FindByRelativePath("Audio/OnlyInB.wav") != nullptr &&
              context.GetAssetRegistry().FindByRelativePath("Audio/OnlyInA.wav") == nullptr,
          "B's registry sees only B's assets");
    const bool anyStaleHandle = std::any_of(handlesA.begin(), handlesA.end(), [&context](Xora2D::AssetHandle handle) {
        const Xora2D::AssetMetadata* meta = context.GetAssetRegistry().FindByHandle(handle);
        return meta != nullptr && meta->RelativePath == "Audio/OnlyInA.wav";
    });
    Check(!anyStaleHandle, "no asset handle of A resolves to A's files in B");
    Check(!context.GetEditScene()->FindEntityByName("Player").IsValid() &&
              !context.GetEditScene()->FindEntityByName("UnsavedInA").IsValid() &&
              context.GetEditScene()->FindEntityByName("Box1").IsValid(),
          "B's scene contains only B's entities");
    Check(Xora2D::ResolveAssetPath("Audio/OnlyInA.wav") == b.ProjectRoot / "Assets/Audio/OnlyInA.wav",
          "asset paths resolve inside B");
    Check(scriptResets >= 2 && projectChanges == 2, "script state reset and host notified on every switch");

    // Each project kept its own registry file.
    Xora2D::AssetRegistry diskA;
    Xora2D::AssetRegistry diskB;
    (void)Xora2D::AssetRegistrySerializer::Load(diskA, a.ProjectRoot / "Assets/Registry/AssetRegistry.yaml");
    (void)Xora2D::AssetRegistrySerializer::Load(diskB, b.ProjectRoot / "Assets/Registry/AssetRegistry.yaml");
    Check(diskA.FindByRelativePath("Audio/OnlyInA.wav") != nullptr && diskA.FindByRelativePath("Audio/OnlyInB.wav") == nullptr,
          "A's registry file persisted with only A's assets");
    Check(diskB.FindByRelativePath("Audio/OnlyInA.wav") == nullptr, "B's registry file never received A's assets");

    // Configuration isolation.
    Xora2D::ProjectConfig configB = context.GetProject()->GetConfig();
    configB.Settings.Gravity = {0.0f, -1.0f};
    configB.Settings.WindowWidth = 640;
    Check(context.UpdateProjectConfig(configB).Success, "B's settings changed");
    const auto reloadA = Xora2D::Project::Load(a.ProjectFile);
    Check(reloadA.Success && reloadA.Loaded->GetSettings() == Xora2D::ProjectSettings{},
          "A's configuration unaffected by B's settings");

    // Scene isolation: switching back reloads A from disk (the unsaved entity is gone, B's entities absent).
    const auto backToA = context.OpenProject(reloadA.Loaded);
    Check(backToA.Success && Xora2D::GetContentRoot() == a.ProjectRoot, "switched back to A");
    Check(context.GetEditScene()->FindEntityByName("Player").IsValid() &&
              !context.GetEditScene()->FindEntityByName("UnsavedInA").IsValid() &&
              !context.GetEditScene()->FindEntityByName("Box1").IsValid(),
          "A's scene reloaded from disk without B's or unsaved entities");
    Check(context.GetProject()->GetSettings() == Xora2D::ProjectSettings{}, "A's settings are A's");

    // Closing.
    context.CloseProject();
    Check(!context.IsContentActive() && !context.HasProject() && context.GetAssetRegistry().Count() == 0,
          "closing the project clears the registry");
    Check(!Xora2D::HasContentRootOverride(), "closing the project restores the default content root");
    Check(fs::is_regular_file(a.ProjectFile) && fs::is_regular_file(b.ProjectFile) &&
              fs::is_regular_file(a.ProjectRoot / "Assets/Audio/OnlyInA.wav"),
          "closing never deletes project files");
}

// Engine samples (no project): content resolves against the engine Resources root; nothing project-specific leaks.
void TestEngineSamples() {
    XORA_LOG_INFO("TEST engine samples (no project)");
    TempDir resources;
    fs::create_directories(resources.Path() / "Assets/Audio");
    fs::create_directories(resources.Path() / "Scenes");
    WriteText(resources.Path() / "Assets/Audio/Sample.wav", "x");
    std::optional<std::string> previous;
    if (const char* value = std::getenv("XORA2D_RESOURCES")) {
        previous = value;
    }
    ::setenv("XORA2D_RESOURCES", resources.Path().string().c_str(), 1);
    {
        TempDir projects;
        const auto created = Create(projects.Path(), "WithProject", ProjectTemplate::Empty2D);
        const auto load = Xora2D::Project::Load(created.ProjectFile);
        EditorContext context;
        (void)context.OpenProject(load.Loaded);
        const auto samples = context.OpenEngineSamples();
        Check(samples.Success && context.IsContentActive() && !context.HasProject(), "engine samples open without a project");
        Check(!Xora2D::HasContentRootOverride() && Xora2D::GetContentRoot() == resources.Path(),
              "engine samples use the resource root");
        Check(context.GetAssetRegistry().FindByRelativePath("Audio/Sample.wav") != nullptr,
              "engine sample assets registered");
        Check(samples.StartupSceneMissing && context.GetEditScene()->FindEntityByName("MainCamera").IsValid(),
              "missing sample scene replaced by a starter scene");
        context.CloseProject();
        Check(fs::is_regular_file(resources.Path() / "Assets/Registry/AssetRegistry.yaml"),
              "sample registry persisted in the resource root on close");
        Check(!fs::exists(created.ProjectRoot / "Assets/Audio/Sample.wav"), "sample assets never copied into a project");
    }
    if (previous.has_value()) {
        ::setenv("XORA2D_RESOURCES", previous->c_str(), 1);
    } else {
        ::unsetenv("XORA2D_RESOURCES");
    }
}

#if XORA2D_SCRIPTING_ENABLED
// The Basic 2D Game template is playable: its script moves the player with the keyboard.
void TestBasicTemplateIsPlayable() {
    XORA_LOG_INFO("TEST basic template is playable");
    TempDir temp;
    const auto created = Create(temp.Path(), "Playable", ProjectTemplate::Basic2DGame);
    const auto load = Xora2D::Project::Load(created.ProjectFile);
    if (!load.Success) {
        Check(false, "load playable project");
        return;
    }
    Xora2D::PythonScriptEngine engine;
    if (!engine.Initialize().Success) {
        Check(false, "python engine initializes");
        return;
    }
    Xora2D::InputManager input;
    // Python's xora2d.Input reads the global Input facade (the Application attaches its InputManager there).
    Xora2D::Input::Attach(&input);
    EditorContext context;
    context.GetHostServices().ResetScriptState = [&engine]() { engine.PrepareShutdown(); };
    context.GetPlaySession().SetHooks({.WireRuntimeScene =
                                           [&](Xora2D::Scene& scene) {
                                               scene.SetPythonScriptEngine(&engine);
                                               Xora2D::ScriptServices services{};
                                               services.Input = &input;
                                               services.Assets = &context.GetAssetManager();
                                               engine.SetServices(services);
                                           },
                                       .ClearGameplayInput = {}});
    (void)context.OpenProject(load.Loaded);
    const Xora2D::UUID playerId = context.GetEditScene()->FindEntityByName("Player").GetUUID();
    const float startX =
        context.GetEditScene()->FindEntityByUUID(playerId).GetComponent<Xora2D::TransformComponent>().Translation.x;

    (void)context.StartPlayMode();
    for (int frame = 0; frame < 30; ++frame) { // land on the ground
        input.BeginFrame();
        context.UpdatePlayMode(1.0f / 60.0f);
    }
    Xora2D::KeyPressedEvent pressD(Xora2D::Key::D, 0, false);
    input.OnEvent(pressD);
    for (int frame = 0; frame < 30; ++frame) {
        input.BeginFrame();
        context.UpdatePlayMode(1.0f / 60.0f);
    }
    const float endX =
        context.GetScene()->FindEntityByUUID(playerId).GetComponent<Xora2D::TransformComponent>().Translation.x;
    Check(endX > startX + 50.0f, "holding D moves the template player right (" + std::to_string(startX) + " -> " +
                                     std::to_string(endX) + ")");
    (void)context.StopPlayMode();
    Check(context.GetEditScene()->FindEntityByUUID(playerId).GetComponent<Xora2D::TransformComponent>().Translation.x ==
              startX,
          "edit scene untouched after playing the template");
    context.CloseProject();
    Xora2D::Input::Attach(nullptr);
}
#endif

} // namespace

int RunProjectTests() {
    XORA_LOG_INFO("TEST projects started");
    const int before = g_failures;
    TestProjectCreation();
    TestTemplates();
    TestInvalidCreation();
    TestExistingDirectoryHandling();
    TestFailedCreationCleanup();
    TestProjectLoading();
    TestMissingStartupSceneAndDirectories();
    TestPathResolution();
    TestProjectSettingsPersistence();
    TestRecentProjects();
    TestProjectIsolation();
    TestEngineSamples();
#if XORA2D_SCRIPTING_ENABLED
    TestBasicTemplateIsPlayable();
#endif
    XORA_LOG_INFO("TEST projects finished with {} failure(s)", g_failures - before);
    return g_failures - before;
}
