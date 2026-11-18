#include "Xora2D/Project/ProjectTemplates.h"

#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Assets/AssetRegistrySerializer.h"
#include "Xora2D/Core/Version.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Project/ProjectSerializer.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"
#include "Xora2D/Serialization/SceneSerializer.h"

#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

namespace Xora2D {

namespace {

constexpr const char* kMainScene = "Scenes/Main.scene.yaml";

// Game content for the Basic 2D Game template (no engine sources are copied into projects).
constexpr const char* kPlayerControllerScript = R"PY(import xora2d


class PlayerController:
    """Basic 2D Game template player: A/D or Left/Right move, Space jumps."""

    def on_create(self):
        self.speed = float(getattr(self, "speed", 5.0))
        self.jump_impulse = float(getattr(self, "jump_impulse", 6.0))

    def on_update(self, delta_time):
        if not self.entity.is_valid():
            return
        velocity = self.entity.physics.get_linear_velocity()
        vertical = float(velocity[1])

        horizontal = 0.0
        if xora2d.Input.is_key_down(xora2d.Key.A) or xora2d.Input.is_key_down(xora2d.Key.Left):
            horizontal -= self.speed
        if xora2d.Input.is_key_down(xora2d.Key.D) or xora2d.Input.is_key_down(xora2d.Key.Right):
            horizontal += self.speed
        self.entity.physics.set_linear_velocity(horizontal, vertical)

        if xora2d.Input.is_key_pressed(xora2d.Key.Space) and abs(vertical) < 0.05:
            self.entity.physics.apply_linear_impulse(0.0, self.jump_impulse)
)PY";

const std::vector<ProjectTemplateInfo> kTemplates = {
    {ProjectTemplate::Empty2D, "empty-2d", "Empty 2D Project",
     "A main scene with only a camera. Start from scratch."},
    {ProjectTemplate::Basic2DGame, "basic-2d-game", "Basic 2D Game",
     "A small playable scene: camera, ground, platforms and a player moved by a Python script "
     "(A/D or arrows to move, Space to jump)."},
    {ProjectTemplate::PhysicsDemo, "physics-demo", "Physics Demo",
     "A camera, ground, walls, a ramp and a stack of dynamic boxes and balls. Press Play to watch them fall."},
};

struct CreationFailure {
    ProjectError Error = ProjectError::IoError;
    std::string Message;
};

std::string RandomSuffix() {
    std::random_device device;
    std::ostringstream stream;
    stream << std::hex << device() << device();
    return stream.str();
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << text;
    output.flush();
    return static_cast<bool>(output);
}

Entity AddCamera(Scene& scene) {
    Entity camera = scene.CreateEntity("MainCamera");
    auto& component = camera.AddComponent<CameraComponent>();
    component.Primary = true;
    // The camera position is the bottom-left corner of the view.
    camera.GetComponent<TransformComponent>().Translation = {0.0f, 0.0f};
    return camera;
}

Entity AddSprite(Scene& scene, const std::string& name, glm::vec2 position, glm::vec2 size, glm::vec4 color,
                 float rotationRadians = 0.0f) {
    Entity entity = scene.CreateEntity(name);
    auto& transform = entity.GetComponent<TransformComponent>();
    transform.Translation = position;
    transform.RotationRadians = rotationRadians;
    transform.RecalculateLocalMatrix();
    auto& sprite = entity.AddComponent<SpriteRendererComponent>();
    sprite.Size = size;
    sprite.Color = color;
    return entity;
}

void AddBoxBody(Entity& entity, RigidBodyType2D type, glm::vec2 size, float friction = 0.6f, float restitution = 0.0f,
                bool fixedRotation = false) {
    auto& body = entity.AddComponent<RigidBody2DComponent>();
    body.Type = type;
    body.FixedRotation = fixedRotation;
    auto& collider = entity.AddComponent<BoxCollider2DComponent>();
    collider.Size = size;
    collider.Friction = friction;
    collider.Restitution = restitution;
}

void BuildEmptyScene(Scene& scene) {
    AddCamera(scene);
}

// Layouts fit a ~900x500 view (the default editor viewport) and stay sensible in a 1280x720 game window. The
// engine draws sprites without a depth order, so templates avoid overlapping sprites (no background quad: the
// clear colour is the background).
void BuildBasicGameScene(Scene& scene, AssetHandle playerScript) {
    AddCamera(scene);

    Entity ground = AddSprite(scene, "Ground", {640.0f, 20.0f}, {1280.0f, 40.0f}, {0.22f, 0.45f, 0.28f, 1.0f});
    AddBoxBody(ground, RigidBodyType2D::Static, {1280.0f, 40.0f}, 0.8f);

    const glm::vec4 platformColor{0.55f, 0.42f, 0.28f, 1.0f};
    Entity platformA = AddSprite(scene, "PlatformLow", {320.0f, 150.0f}, {200.0f, 24.0f}, platformColor);
    AddBoxBody(platformA, RigidBodyType2D::Static, {200.0f, 24.0f}, 0.8f);
    Entity platformB = AddSprite(scene, "PlatformHigh", {580.0f, 260.0f}, {200.0f, 24.0f}, platformColor);
    AddBoxBody(platformB, RigidBodyType2D::Static, {200.0f, 24.0f}, 0.8f);

    const glm::vec4 coinColor{0.98f, 0.82f, 0.25f, 1.0f};
    AddSprite(scene, "Coin1", {320.0f, 200.0f}, {20.0f, 20.0f}, coinColor);
    AddSprite(scene, "Coin2", {580.0f, 310.0f}, {20.0f, 20.0f}, coinColor);
    AddSprite(scene, "Goal", {820.0f, 90.0f}, {40.0f, 100.0f}, {0.35f, 0.80f, 0.95f, 1.0f});

    Entity player = AddSprite(scene, "Player", {100.0f, 120.0f}, {48.0f, 48.0f}, {0.92f, 0.38f, 0.33f, 1.0f});
    AddBoxBody(player, RigidBodyType2D::Dynamic, {48.0f, 48.0f}, 0.2f, 0.0f, true);
    auto& script = player.AddComponent<PythonScriptComponent>();
    script.ScriptAsset = playerScript;
    script.ClassName = "PlayerController";
}

void BuildPhysicsDemoScene(Scene& scene) {
    AddCamera(scene);
    const glm::vec4 staticColor{0.36f, 0.40f, 0.48f, 1.0f};

    Entity ground = AddSprite(scene, "Ground", {460.0f, 20.0f}, {920.0f, 40.0f}, staticColor);
    AddBoxBody(ground, RigidBodyType2D::Static, {920.0f, 40.0f}, 0.8f);
    Entity leftWall = AddSprite(scene, "LeftWall", {10.0f, 300.0f}, {20.0f, 520.0f}, staticColor);
    AddBoxBody(leftWall, RigidBodyType2D::Static, {20.0f, 520.0f});
    Entity rightWall = AddSprite(scene, "RightWall", {850.0f, 300.0f}, {20.0f, 520.0f}, staticColor);
    AddBoxBody(rightWall, RigidBodyType2D::Static, {20.0f, 520.0f});
    Entity ramp = AddSprite(scene, "Ramp", {230.0f, 190.0f}, {300.0f, 20.0f}, staticColor, -0.35f);
    AddBoxBody(ramp, RigidBodyType2D::Static, {300.0f, 20.0f}, 0.3f);

    const glm::vec4 boxColors[] = {{0.38f, 0.55f, 0.90f, 1.0f}, {0.90f, 0.55f, 0.30f, 1.0f}, {0.45f, 0.80f, 0.45f, 1.0f}};
    int boxIndex = 0;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 3; ++column) {
            const glm::vec2 position{560.0f + static_cast<float>(column) * 52.0f, 70.0f + static_cast<float>(row) * 52.0f};
            Entity box = AddSprite(scene, "Box" + std::to_string(++boxIndex), position, {48.0f, 48.0f},
                                   boxColors[(row + column) % 3]);
            AddBoxBody(box, RigidBodyType2D::Dynamic, {48.0f, 48.0f}, 0.7f, 0.05f);
        }
    }

    for (int index = 0; index < 4; ++index) {
        const glm::vec2 position{90.0f + static_cast<float>(index) * 45.0f, 400.0f + static_cast<float>(index) * 30.0f};
        Entity ball = AddSprite(scene, "Ball" + std::to_string(index + 1), position, {36.0f, 36.0f},
                                {0.95f, 0.85f, 0.30f, 1.0f});
        auto& body = ball.AddComponent<RigidBody2DComponent>();
        body.Type = RigidBodyType2D::Dynamic;
        auto& collider = ball.AddComponent<CircleCollider2DComponent>();
        collider.Radius = 18.0f;
        collider.Restitution = 0.55f;
        collider.Friction = 0.3f;
    }
}

// Writes the template into `root` (the staging folder). Throws nothing; returns a failure description.
std::optional<CreationFailure> PopulateProject(const ProjectCreateOptions& options, const std::filesystem::path& root,
                                               const std::string& projectFileName) {
    const auto step = [&options](std::string_view name) {
        return !options.BeforeStep || options.BeforeStep(name);
    };
    const auto aborted = [](std::string_view name) {
        return CreationFailure{ProjectError::IoError, "Project creation aborted at step '" + std::string(name) + "'"};
    };

    if (!step("directories")) {
        return aborted("directories");
    }
    for (const char* relative : {"Assets/Textures", "Assets/Audio", "Assets/Scripts", "Assets/Animations",
                                 "Assets/Registry", "Scenes", "Config", "ProjectSettings"}) {
        std::error_code errorCode;
        std::filesystem::create_directories(root / relative, errorCode);
        if (errorCode) {
            return CreationFailure{ProjectError::IoError,
                                   "Cannot create folder '" + std::string(relative) + "': " + errorCode.message()};
        }
    }

    // Assets: the registry stores paths relative to Assets/, with stable path-derived handles.
    if (!step("assets")) {
        return aborted("assets");
    }
    AssetRegistry registry;
    AssetHandle playerScript{};
    if (options.Template == ProjectTemplate::Basic2DGame) {
        const std::string relative = "Scripts/PlayerController.py";
        if (!WriteTextFile(root / "Assets" / relative, kPlayerControllerScript)) {
            return CreationFailure{ProjectError::IoError, "Cannot write Assets/" + relative};
        }
        AssetMetadata metadata{};
        metadata.Handle = registry.AllocateHandleForPath(relative);
        metadata.Type = AssetType::PythonScript;
        metadata.Name = "PlayerController";
        metadata.RelativePath = relative;
        if (!registry.RegisterAsset(metadata)) {
            return CreationFailure{ProjectError::IoError, "Cannot register template script"};
        }
        playerScript = metadata.Handle;
    }
    const auto registrySave = AssetRegistrySerializer::Save(registry, root / "Assets/Registry/AssetRegistry.yaml");
    if (!registrySave.Success) {
        return CreationFailure{ProjectError::IoError, "Cannot write asset registry: " + registrySave.Message};
    }

    if (!step("scene")) {
        return aborted("scene");
    }
    std::shared_ptr<Scene> scene = Scene::Create();
    scene->SetSceneName("Main");
    switch (options.Template) {
    case ProjectTemplate::Empty2D:
        BuildEmptyScene(*scene);
        break;
    case ProjectTemplate::Basic2DGame:
        BuildBasicGameScene(*scene, playerScript);
        break;
    case ProjectTemplate::PhysicsDemo:
        BuildPhysicsDemoScene(*scene);
        break;
    }
    const SceneSerializeResult sceneSave = SceneSerializer::Serialize(*scene, root / kMainScene);
    if (!sceneSave.Success) {
        return CreationFailure{ProjectError::IoError, "Cannot write main scene: " + sceneSave.Message};
    }

    if (!step("project-file")) {
        return aborted("project-file");
    }
    ProjectConfig config{};
    config.Name = options.Name;
    config.EngineVersion = GetEngineVersionString();
    config.StartupScene = kMainScene;
    const ProjectFileResult saved = ProjectSerializer::SaveFile(config, root / projectFileName);
    if (!saved.Success) {
        return CreationFailure{saved.Error, saved.Message};
    }
    return std::nullopt;
}

} // namespace

const std::vector<ProjectTemplateInfo>& GetProjectTemplates() {
    return kTemplates;
}

const ProjectTemplateInfo& GetProjectTemplateInfo(ProjectTemplate projectTemplate) {
    for (const ProjectTemplateInfo& info : kTemplates) {
        if (info.Template == projectTemplate) {
            return info;
        }
    }
    return kTemplates.front();
}

std::optional<ProjectTemplate> FindProjectTemplate(std::string_view id) {
    for (const ProjectTemplateInfo& info : kTemplates) {
        if (id == info.Id) {
            return info.Template;
        }
    }
    return std::nullopt;
}

std::string ValidateProjectCreateOptions(const ProjectCreateOptions& options, ProjectError* error) {
    const auto fail = [error](ProjectError code, std::string message) {
        if (error != nullptr) {
            *error = code;
        }
        return message;
    };
    if (error != nullptr) {
        *error = ProjectError::None;
    }
    std::string nameError;
    if (!IsValidProjectName(options.Name, &nameError)) {
        return fail(ProjectError::InvalidName, nameError);
    }
    if (options.ParentDirectory.empty()) {
        return fail(ProjectError::InvalidLocation, "Choose a location for the project");
    }
    std::error_code errorCode;
    if (!options.ParentDirectory.is_absolute()) {
        return fail(ProjectError::InvalidLocation, "Location must be an absolute folder path");
    }
    if (!std::filesystem::is_directory(options.ParentDirectory, errorCode)) {
        return fail(ProjectError::InvalidLocation, "Location does not exist or is not a folder: " +
                                                       options.ParentDirectory.string());
    }
    const std::filesystem::path target = options.ParentDirectory / options.Name;
    if (std::filesystem::exists(target, errorCode)) {
        if (!std::filesystem::is_directory(target, errorCode)) {
            return fail(ProjectError::AlreadyExists, "A file named '" + options.Name + "' already exists there");
        }
        if (!std::filesystem::is_empty(target, errorCode) || errorCode) {
            return fail(ProjectError::AlreadyExists,
                        "Folder '" + target.string() + "' already exists and is not empty");
        }
    }
    return {};
}

ProjectCreateResult CreateProject(const ProjectCreateOptions& options) {
    ProjectCreateResult result{};
    ProjectError validationError = ProjectError::None;
    if (std::string message = ValidateProjectCreateOptions(options, &validationError); !message.empty()) {
        result.Error = validationError;
        result.Message = std::move(message);
        return result;
    }

    const std::filesystem::path parent = std::filesystem::weakly_canonical(options.ParentDirectory);
    const std::filesystem::path target = parent / options.Name;
    const std::string projectFileName = options.Name + std::string(kProjectFileExtension);
    // Everything is written into a staging folder that only this call owns, so cleanup after a failure can never
    // touch pre-existing user files.
    const std::filesystem::path staging = parent / (".xora_create_" + RandomSuffix());

    std::error_code errorCode;
    if (!std::filesystem::create_directory(staging, errorCode) || errorCode) {
        result.Error = ProjectError::IoError;
        result.Message = "Cannot write to " + parent.string() + (errorCode ? ": " + errorCode.message() : "");
        return result;
    }
    const auto cleanup = [&staging]() {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
    };

    if (const std::optional<CreationFailure> failure = PopulateProject(options, staging, projectFileName)) {
        cleanup();
        result.Error = failure->Error;
        result.Message = failure->Message;
        return result;
    }

    if (options.BeforeStep && !options.BeforeStep("finalize")) {
        cleanup();
        result.Error = ProjectError::IoError;
        result.Message = "Project creation aborted at step 'finalize'";
        return result;
    }
    // An empty folder with the project name may be reused (e.g. created by a folder picker). remove() only ever
    // deletes empty directories.
    if (std::filesystem::is_directory(target, errorCode)) {
        std::filesystem::remove(target, errorCode);
    }
    std::filesystem::rename(staging, target, errorCode);
    if (errorCode) {
        cleanup();
        result.Error = std::filesystem::exists(target) ? ProjectError::AlreadyExists : ProjectError::IoError;
        result.Message = "Cannot create project folder " + target.string() + ": " + errorCode.message();
        return result;
    }

    result.Success = true;
    result.ProjectRoot = target;
    result.ProjectFile = target / projectFileName;
    result.Message = "Created project '" + options.Name + "' (" + GetProjectTemplateInfo(options.Template).DisplayName +
                     ") at " + target.string();
    return result;
}

} // namespace Xora2D
