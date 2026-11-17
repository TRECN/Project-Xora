#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetRegistrySerializer.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Audio/AudioEngine.h"
#include "Xora2D/Audio/AudioTypes.h"
#include "Xora2D/Core/Application.h"
#include "Xora2D/Input/KeyCodes.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Events/Event.h"
#include "Xora2D/Events/KeyEvent.h"
#include "Xora2D/Events/WindowEvent.h"
#include "Xora2D/Input/Input.h"
#include "Xora2D/Input/InputMapping.h"
#include "Xora2D/Input/InputMappingSerializer.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Physics/PhysicsEvents.h"
#include "Xora2D/Physics/PhysicsWorld2D.h"
#include "Xora2D/Renderer/Renderer2D.h"
#include "Xora2D/Renderer/Texture2D.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"
#include "Xora2D/Scripting/PythonScriptEngine.h"
#include "Xora2D/Scripting/ScriptServices.h"
#include "Xora2D/Scripting/ScriptTypes.h"
#include "Xora2D/Serialization/SceneSerializer.h"

#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr float kPlayerSpeedMetersPerSecond = 4.5f;
constexpr float kJumpImpulseMetersPerSecond = 7.0f;
constexpr float kCameraZoomStep = 0.08f;

bool IsDemoDigitPressed(int digit) {
    if (digit < 1 || digit > 9) {
        return false;
    }
    const auto digitKey = static_cast<Xora2D::Key>(static_cast<std::uint16_t>(Xora2D::Key::Digit1) + (digit - 1));
    const auto keypadKey =
        static_cast<Xora2D::Key>(static_cast<std::uint16_t>(Xora2D::Key::Keypad1) + (digit - 1));
    return Xora2D::Input::IsKeyPressed(digitKey) || Xora2D::Input::IsKeyPressed(keypadKey);
}

class SandboxApp : public Xora2D::Application {
public:
    SandboxApp() {
        m_scene = Xora2D::Scene::Create();
        m_scene->SetViewportSize(static_cast<float>(GetWindow().GetFramebufferWidth()),
                                 static_cast<float>(GetWindow().GetFramebufferHeight()));

        auto& physics = m_scene->GetPhysicsWorld();
        Xora2D::PhysicsWorld2D::Settings settings = physics.GetSettings();
        settings.PixelsPerMeter = 50.0f;
        settings.Gravity = {0.0f, -9.81f};
#ifndef NDEBUG
        settings.EnableContactDiagnostics = true;
#endif
        physics.Configure(settings);

        InitializeAssetSystem();
        m_scene->SetAssetManager(&m_assetManager);
        m_scene->SetAudioEngine(&GetAudioEngine());
        m_scene->SetPythonScriptEngine(&GetPythonScriptEngine());

        m_scene->SetPhysicsEventCallback([](const Xora2D::PhysicsCollisionEvent& event) {
            const char* kind = event.Kind == Xora2D::PhysicsCollisionKind::Sensor ? "Sensor" : "Collision";
            const char* phase = event.Phase == Xora2D::PhysicsCollisionPhase::Begin ? "begin" : "end";
            XORA_LOG_INFO("{} {} — {} / {}", kind, phase, event.EntityA.ToString(), event.EntityB.ToString());
        });

        LoadInputMapping();
        BuildScene();
        InitializePythonDemo();
        InitializeAudioDemo();
        m_scene->SetSceneName("SandboxPhysicsDemo");
        DemonstrateSceneSerialization();
        XORA_LOG_INFO("Physics sandbox: {} entities", m_scene->GetEntityCount());
    }

protected:
    void OnShutdownRequested() override {
        m_playerEntity = {};
        m_assetManager.PrepareShutdown();
        if (m_scene) {
            m_scene->PrepareForShutdown();
        }
    }

    void OnUpdate(double deltaSeconds) override {
        const float delta = static_cast<float>(deltaSeconds);

        if (GetPythonScriptEngine().IsAvailable()) {
            Xora2D::ScriptServices services{};
            services.Scene = m_scene.get(); // NOLINT
            services.Input = &GetInputManager();
            services.Audio = &GetAudioEngine();
            services.Assets = &m_assetManager;
            GetPythonScriptEngine().SetServices(services);
        }

        // Step physics first so SyncSceneBodies creates bodies before gameplay applies forces.
        m_scene->OnUpdate(delta);
        UpdateInputGameplay(delta);
        UpdateAudioDemo();
        UpdateCamera(delta);
        m_logCooldown -= delta;
    }

    void OnRender() override {
        m_scene->SetViewportSize(static_cast<float>(GetWindow().GetFramebufferWidth()),
                                 static_cast<float>(GetWindow().GetFramebufferHeight()));

        GetRenderer2D().ResetStats();
        m_scene->OnRender(GetRenderer2D());

        if (m_logCooldown <= 0.0f) {
            const auto& stats = GetRenderer2D().GetStats();
            XORA_LOG_INFO("Renderer2D frame stats — drawCalls: {}, quads: {}", stats.drawCalls, stats.quadCount);
            m_logCooldown = 1.0f;
        }
    }

    void OnEvent(Xora2D::Event& event) override {
        Xora2D::EventDispatcher<Xora2D::WindowResizeEvent> resizeDispatcher(event);
        resizeDispatcher.Dispatch([this](Xora2D::WindowResizeEvent&) {
            m_scene->SetViewportSize(static_cast<float>(GetWindow().GetFramebufferWidth()),
                                     static_cast<float>(GetWindow().GetFramebufferHeight()));
            return false;
        });
    }

private:
    void InitializeAssetSystem() {
        const std::filesystem::path registryPath = Xora2D::DefaultRegistryFilePath();
        const auto loadResult = Xora2D::AssetRegistrySerializer::Load(m_assetRegistry, registryPath);
        if (!loadResult.Success) {
            XORA_LOG_WARN("Asset registry load: {}", loadResult.Message);
        } else {
            XORA_LOG_INFO("Asset registry loaded from {} ({} assets)", registryPath.string(),
                          m_assetRegistry.Count());
        }

        const std::filesystem::path logoSource = Xora2D::ResolveResourcePath("Textures/Xora2D_Logo.png");
        const Xora2D::AssetResult importResult = m_assetManager.ImportTextureFile(logoSource, "Xora2D_Logo");
        if (!importResult.Success) {
            XORA_LOG_WARN("Logo texture import failed: {}", importResult.Message);
        } else {
            m_logoAssetHandle = importResult.Handle;
            XORA_LOG_INFO("Texture asset handle: {}", m_logoAssetHandle.ToString());
            const Xora2D::AssetResult loadTexture = m_assetManager.LoadTexture(m_logoAssetHandle);
            if (loadTexture.Success) {
                XORA_LOG_INFO("Texture loaded");
                if (m_assetManager.LoadTexture(m_logoAssetHandle).Success) {
                    XORA_LOG_INFO("Cached texture reused");
                }
            }
        }

        const auto saveResult = Xora2D::AssetRegistrySerializer::Save(m_assetRegistry, registryPath);
        if (!saveResult.Success) {
            XORA_LOG_WARN("Asset registry save failed: {}", saveResult.Message);
        }
    }

    void BuildScene() {
        m_cameraEntity = m_scene->CreateEntity("MainCamera");
        auto& camera = m_cameraEntity.AddComponent<Xora2D::CameraComponent>();
        camera.Primary = true;
        auto& cameraTransform = m_cameraEntity.GetComponent<Xora2D::TransformComponent>();
        cameraTransform.Translation = {640.0f, 360.0f};

        CreateStaticPlatform("Ground", {640.0f, 30.0f}, {1400.0f, 60.0f}, {0.18f, 0.20f, 0.28f, 1.0f});

        CreateStaticPlatform("LeftWall", {40.0f, 360.0f}, {60.0f, 900.0f}, {0.25f, 0.28f, 0.36f, 1.0f});
        CreateStaticPlatform("RightWall", {1240.0f, 360.0f}, {60.0f, 900.0f}, {0.25f, 0.28f, 0.36f, 1.0f});
        CreateStaticPlatform("UpperPlatform", {520.0f, 220.0f}, {260.0f, 24.0f}, {0.30f, 0.42f, 0.56f, 1.0f});

        CreateSensor("DropSensor", {900.0f, 120.0f}, {180.0f, 120.0f});

        CreateDynamicCircle("Player", {640.0f, 120.0f}, 24.0f, 0.0f, 0.85f);
        m_playerEntity = m_scene->FindEntityByName("Player");
        if (m_playerEntity.IsValid()) {
            auto& playerSprite = m_playerEntity.GetComponent<Xora2D::SpriteRendererComponent>();
            playerSprite.Color = {0.20f, 0.85f, 0.55f, 1.0f};
        }

        for (int index = 0; index < 4; ++index) {
            const float x = 260.0f + static_cast<float>(index) * 95.0f;
            const float restitution = index % 2 == 0 ? 0.05f : 0.65f;
            const float friction = index % 2 == 0 ? 0.85f : 0.2f;
            CreateDynamicBox("Box" + std::to_string(index + 1), {x, 420.0f + index * 15.0f}, {48.0f, 48.0f},
                             restitution, friction);
        }

        for (int index = 0; index < 4; ++index) {
            const float x = 760.0f + static_cast<float>(index) * 70.0f;
            CreateDynamicCircle("Ball" + std::to_string(index + 1), {x, 500.0f + index * 20.0f}, 22.0f + index * 4.0f,
                                index == 0 ? 0.8f : 0.2f, 0.35f);
        }

        Xora2D::Entity logo = m_scene->CreateEntity("Logo");
        logo.AddComponent<Xora2D::SpriteRendererComponent>();
        auto& logoSprite = logo.GetComponent<Xora2D::SpriteRendererComponent>();
        logoSprite.TextureAsset = m_logoAssetHandle;
        logoSprite.Size = {120.0f, 120.0f};
        logo.GetComponent<Xora2D::TransformComponent>().Translation = {1150.0f, 620.0f};
    }

    void DemonstrateSceneSerialization() {
        const std::filesystem::path scenePath =
            Xora2D::ResolveResourcePath("Scenes/SandboxPhysicsDemo.scene.yaml");
        const std::size_t entityCountBeforeSave = m_scene->GetEntityCount();
        const Xora2D::UUID cameraUuid = m_cameraEntity.GetUUID();

        const Xora2D::SceneSerializeResult saveResult = Xora2D::SceneSerializer::Serialize(*m_scene, scenePath);
        if (!saveResult.Success) {
            XORA_LOG_WARN("Scene save failed: {}", saveResult.Message);
            return;
        }
        XORA_LOG_INFO("Scene saved to {} ({} entities)", scenePath.string(), entityCountBeforeSave);

        auto loadedScene = Xora2D::Scene::Create();
        loadedScene->SetViewportSize(m_scene->GetViewportWidth(), m_scene->GetViewportHeight());
        const Xora2D::SceneSerializeResult loadResult =
            Xora2D::SceneSerializer::Deserialize(*loadedScene, scenePath, &m_assetManager);
        if (!loadResult.Success) {
            XORA_LOG_WARN("Scene load failed: {}", loadResult.Message);
            return;
        }

        XORA_LOG_INFO("Scene loaded: {} entities (before save: {})", loadedScene->GetEntityCount(),
                      entityCountBeforeSave);
        const Xora2D::Entity loadedCamera = loadedScene->FindEntityByUUID(cameraUuid);
        if (!loadedCamera.IsValid()) {
            XORA_LOG_WARN("Loaded scene did not preserve MainCamera UUID {}", cameraUuid.ToString());
        }

        m_scene->ReplaceContentsFrom(*loadedScene);

        {
            auto& physics = m_scene->GetPhysicsWorld();
            Xora2D::PhysicsWorld2D::Settings settings = physics.GetSettings();
            settings.PixelsPerMeter = 50.0f;
            settings.Gravity = {0.0f, -9.81f};
#ifndef NDEBUG
            settings.EnableContactDiagnostics = true;
#endif
            physics.Configure(settings);
        }

        m_cameraEntity = m_scene->FindEntityByUUID(cameraUuid);
        if (!m_cameraEntity.IsValid()) {
            m_cameraEntity = m_scene->FindEntityByName("MainCamera");
        }
        m_playerEntity = m_scene->FindEntityByName("Player");
        if (m_playerEntity.IsValid()) {
            auto& playerSprite = m_playerEntity.GetComponent<Xora2D::SpriteRendererComponent>();
            playerSprite.Color = {0.20f, 0.85f, 0.55f, 1.0f};
            m_playerPythonControl = m_playerEntity.HasComponent<Xora2D::PythonScriptComponent>();
        }
        m_ecsMusicEntity = m_scene->FindEntityByName("EcsMusicEmitter");
        XORA_LOG_INFO("Active scene after reload: {} entities, camera UUID match: {}", m_scene->GetEntityCount(),
                      m_cameraEntity.GetUUID() == cameraUuid);
    }

    void CreateStaticPlatform(const std::string& name, const glm::vec2& position, const glm::vec2& size,
                              const glm::vec4& color) {
        Xora2D::Entity entity = m_scene->CreateEntity(name);
        auto& body = entity.AddComponent<Xora2D::RigidBody2DComponent>();
        body.Type = Xora2D::RigidBodyType2D::Static;
        auto& collider = entity.AddComponent<Xora2D::BoxCollider2DComponent>();
        collider.Size = size;
        collider.Friction = 0.7f;
        auto& sprite = entity.AddComponent<Xora2D::SpriteRendererComponent>();
        sprite.Color = color;
        sprite.Size = size;
        entity.GetComponent<Xora2D::TransformComponent>().Translation = position;
    }

    void CreateSensor(const std::string& name, const glm::vec2& position, const glm::vec2& size) {
        Xora2D::Entity entity = m_scene->CreateEntity(name);
        auto& body = entity.AddComponent<Xora2D::RigidBody2DComponent>();
        body.Type = Xora2D::RigidBodyType2D::Static;
        auto& collider = entity.AddComponent<Xora2D::BoxCollider2DComponent>();
        collider.Size = size;
        collider.IsSensor = true;
        auto& sprite = entity.AddComponent<Xora2D::SpriteRendererComponent>();
        sprite.Color = {0.85f, 0.70f, 0.20f, 0.35f};
        sprite.Size = size;
        entity.GetComponent<Xora2D::TransformComponent>().Translation = position;
    }

    void CreateDynamicBox(const std::string& name, const glm::vec2& position, const glm::vec2& size, float restitution,
                          float friction) {
        Xora2D::Entity entity = m_scene->CreateEntity(name);
        auto& body = entity.AddComponent<Xora2D::RigidBody2DComponent>();
        body.Type = Xora2D::RigidBodyType2D::Dynamic;
        auto& collider = entity.AddComponent<Xora2D::BoxCollider2DComponent>();
        collider.Size = size;
        collider.Restitution = restitution;
        collider.Friction = friction;
        auto& sprite = entity.AddComponent<Xora2D::SpriteRendererComponent>();
        sprite.Color = {0.35f + restitution * 0.5f, 0.55f, 0.90f, 1.0f};
        sprite.Size = size;
        entity.GetComponent<Xora2D::TransformComponent>().Translation = position;
    }

    void CreateDynamicCircle(const std::string& name, const glm::vec2& position, float radius, float restitution,
                             float friction) {
        Xora2D::Entity entity = m_scene->CreateEntity(name);
        auto& body = entity.AddComponent<Xora2D::RigidBody2DComponent>();
        body.Type = Xora2D::RigidBodyType2D::Dynamic;
        body.FixedRotation = false;
        auto& collider = entity.AddComponent<Xora2D::CircleCollider2DComponent>();
        collider.Radius = radius;
        collider.Restitution = restitution;
        collider.Friction = friction;
        auto& sprite = entity.AddComponent<Xora2D::SpriteRendererComponent>();
        sprite.Color = {0.92f, 0.40f + restitution * 0.3f, 0.28f, 1.0f};
        sprite.Size = {radius * 2.0f, radius * 2.0f};
        entity.GetComponent<Xora2D::TransformComponent>().Translation = position;
    }

    void InitializePythonDemo() {
        if (!GetPythonScriptEngine().IsScriptingCompiledIn()) {
            XORA_LOG_INFO("Python scripting not compiled into this build");
            return;
        }
        if (!GetPythonScriptEngine().IsAvailable()) {
            XORA_LOG_WARN("Python scripting unavailable — C++ sandbox controls remain active");
            return;
        }

        const auto registerScript = [this](const char* relative, const char* displayName) {
            return m_assetManager.RegisterPythonScriptAtRelativePath(relative, displayName);
        };

        const Xora2D::AssetResult playerScript = registerScript("Scripts/PlayerController.py", "PlayerController");
        const Xora2D::AssetResult rotatorScript = registerScript("Scripts/Rotator.py", "Rotator");
        const Xora2D::AssetResult physicsScript = registerScript("Scripts/PhysicsController.py", "PhysicsController");
        const Xora2D::AssetResult audioScript = registerScript("Scripts/AudioTrigger.py", "AudioTrigger");

        if (playerScript.Success) {
            (void)m_assetManager.LoadPythonScript(playerScript.Handle);
        }

        if (m_playerEntity.IsValid() && playerScript.Success) {
            auto& script = m_playerEntity.AddComponent<Xora2D::PythonScriptComponent>();
            script.ScriptAsset = playerScript.Handle;
            script.ClassName = "PlayerController";
            script.Properties = {
                {Xora2D::ScriptProperty{.Name = "speed", .Type = Xora2D::ScriptPropertyType::Number,
                                        .NumberValue = kPlayerSpeedMetersPerSecond}},
                {Xora2D::ScriptProperty{.Name = "jump_impulse", .Type = Xora2D::ScriptPropertyType::Number,
                                .NumberValue = kJumpImpulseMetersPerSecond}},
                {Xora2D::ScriptProperty{.Name = "display_name", .Type = Xora2D::ScriptPropertyType::String,
                                .StringValue = "SandboxPlayer"}},
            };
            (void)physicsScript;
            m_playerPythonControl = true;
        }

        Xora2D::Entity rotatorA = m_scene->FindEntityByName("Box1");
        Xora2D::Entity rotatorB = m_scene->FindEntityByName("Box2");
        for (Xora2D::Entity entity : {rotatorA, rotatorB}) {
            if (!entity.IsValid() || !rotatorScript.Success) {
                continue;
            }
            auto& script = entity.AddComponent<Xora2D::PythonScriptComponent>();
            script.ScriptAsset = rotatorScript.Handle;
            script.ClassName = "Rotator";
            script.Properties = {
                {Xora2D::ScriptProperty{.Name = "radians_per_second", .Type = Xora2D::ScriptPropertyType::Number,
                                .NumberValue = entity.GetName() == "Box1" ? 1.2 : -0.9}},
            };
        }

        if (audioScript.Success) {
            Xora2D::Entity listener = m_scene->CreateEntity("AudioTriggerEntity");
            auto& script = listener.AddComponent<Xora2D::PythonScriptComponent>();
            script.ScriptAsset = audioScript.Handle;
            script.ClassName = "AudioTrigger";
        }

        XORA_LOG_INFO("Python gameplay demo attached (WASD physics move + Space jump + rotating boxes)");
    }

    void InitializeAudioDemo() {
        const Xora2D::AssetResult beepReg = m_assetManager.RegisterAudioAtRelativePath("Audio/sfx_beep.wav", "SfxBeep");
        const Xora2D::AssetResult clickReg =
            m_assetManager.RegisterAudioAtRelativePath("Audio/sfx_click.wav", "SfxClick");
        const Xora2D::AssetResult musicReg =
            m_assetManager.RegisterAudioAtRelativePath("Audio/music_loop.wav", "MusicLoop");
        if (beepReg.Success) {
            m_sfxBeepHandle = beepReg.Handle;
        }
        if (clickReg.Success) {
            m_sfxClickHandle = clickReg.Handle;
        }
        if (musicReg.Success) {
            m_musicHandle = musicReg.Handle;
        }

        if (m_sfxBeepHandle.IsValid()) {
            (void)m_assetManager.LoadAudio(m_sfxBeepHandle);
        }
        if (m_sfxClickHandle.IsValid()) {
            (void)m_assetManager.LoadAudio(m_sfxClickHandle);
        }
        if (m_musicHandle.IsValid()) {
            (void)m_assetManager.LoadAudio(m_musicHandle);
        }

        m_musicSource = GetAudioEngine().CreateSource();
        m_sfxSource = GetAudioEngine().CreateSource();

        if (m_cameraEntity.IsValid()) {
            m_cameraEntity.AddComponent<Xora2D::AudioListenerComponent>();
        }

        m_ecsMusicEntity = m_scene->CreateEntity("EcsMusicEmitter");
        auto& ecsMusic = m_ecsMusicEntity.AddComponent<Xora2D::AudioSourceComponent>();
        ecsMusic.ClipAsset = m_musicHandle;
        ecsMusic.Loop = true;
        ecsMusic.PlayOnStart = false;
        ecsMusic.Group = Xora2D::AudioGroup::Music;
        ecsMusic.Volume = 0.35f;

        XORA_LOG_INFO("Audio demo: 1=beep, 2=music play/pause, 3=stop loops, 4=toggle beep loop");
    }

    void StopContinuousDemoAudio() {
        m_musicSource.Stop();
        m_sfxSource.Stop();
        m_scene->StopLoopingAudio();

        m_ecsMusicEntity = m_scene->FindEntityByName("EcsMusicEmitter");
        if (m_ecsMusicEntity.IsValid()) {
            auto& comp = m_ecsMusicEntity.GetComponent<Xora2D::AudioSourceComponent>();
            comp.Loop = false;
            comp.PlayOnStart = false;
        }
        XORA_LOG_INFO("Stopped looping audio (key 3)");
    }

    void UpdateAudioDemo() {
        auto& engine = GetAudioEngine();
        if (!engine.IsInitialized()) {
            return;
        }

        if (IsDemoDigitPressed(1)) {
            if (auto clip = m_assetManager.GetLoadedAudio(m_sfxBeepHandle)) {
                m_sfxSource.Play(clip, Xora2D::AudioGroup::SoundEffects, 0.9f, 1.0f, m_sfxLooping);
            }
        }
        if (IsDemoDigitPressed(2)) {
            if (auto clip = m_assetManager.GetLoadedAudio(m_musicHandle)) {
                if (!m_musicSource.IsValid() || !m_musicSource.IsPlaying() || m_musicSource.IsPaused()) {
                    m_musicSource.Play(clip, Xora2D::AudioGroup::Music, 0.5f, 1.0f, true);
                } else {
                    m_musicSource.Pause();
                }
            }
        }
        if (IsDemoDigitPressed(3)) {
            StopContinuousDemoAudio();
        }
        if (IsDemoDigitPressed(4)) {
            m_sfxLooping = !m_sfxLooping;
            XORA_LOG_INFO("SFX looping {}", m_sfxLooping ? "enabled" : "disabled");
        }
        if (Xora2D::Input::IsKeyPressed(Xora2D::Key::PageUp)) {
            engine.SetMasterVolume(engine.GetMasterVolume() + 0.05f);
        }
        if (Xora2D::Input::IsKeyPressed(Xora2D::Key::PageDown)) {
            engine.SetMasterVolume(engine.GetMasterVolume() - 0.05f);
        }
        if (Xora2D::Input::IsKeyPressed(Xora2D::Key::Home)) {
            engine.SetMusicVolume(engine.GetMusicVolume() + 0.05f);
        }
        if (Xora2D::Input::IsKeyPressed(Xora2D::Key::End)) {
            engine.SetSoundEffectsVolume(engine.GetSoundEffectsVolume() - 0.05f);
        }
    }

    void LoadInputMapping() {
        const std::filesystem::path mappingPath = Xora2D::ResolveResourcePath("Input/SandboxInput.yaml");
        const auto result = Xora2D::InputMappingSerializer::Load(m_inputMapping, mappingPath);
        if (!result.Success) {
            XORA_LOG_WARN("Input mapping load failed: {}", result.Message);
            m_inputMapping.BindKey("MoveLeft", Xora2D::Key::A);
            m_inputMapping.BindKey("MoveRight", Xora2D::Key::D);
            m_inputMapping.BindKey("MoveUp", Xora2D::Key::W);
            m_inputMapping.BindKey("MoveDown", Xora2D::Key::S);
            m_inputMapping.BindKey("Jump", Xora2D::Key::Space);
            m_inputMapping.BindKey("Jump", Xora2D::Key::Up);
            m_inputMapping.BindKey("Quit", Xora2D::Key::Escape);
            m_inputMapping.BindMouseButton("Fire", Xora2D::MouseButton::Left);
        } else {
            XORA_LOG_INFO("Input mapping loaded from {}", mappingPath.string());
        }
    }

    void UpdateInputGameplay(float /*delta*/) {
        auto& input = GetInputManager();
        if (m_inputMapping.IsActionPressed(input, "Quit")) {
            Close();
        }
        if (m_inputMapping.IsActionPressed(input, "Fire")) {
            XORA_LOG_INFO("Fire action (left click) at {},{}", Xora2D::Input::GetMouseX(), Xora2D::Input::GetMouseY());
        }
        if (!m_playerEntity.IsValid() || !m_scene->GetPhysicsWorld().HasPhysicsBody(m_playerEntity)) {
            return;
        }
        if (m_playerPythonControl) {
            return;
        }

        auto& physics = m_scene->GetPhysicsWorld();
        glm::vec2 velocity = physics.GetLinearVelocity(m_playerEntity);
        float targetX = 0.0f;
        if (m_inputMapping.IsActionDown(input, "MoveLeft")) {
            targetX -= kPlayerSpeedMetersPerSecond;
        }
        if (m_inputMapping.IsActionDown(input, "MoveRight")) {
            targetX += kPlayerSpeedMetersPerSecond;
        }
        velocity.x = targetX;
        if (m_inputMapping.IsActionDown(input, "MoveUp")) {
            velocity.y = kPlayerSpeedMetersPerSecond;
        } else if (m_inputMapping.IsActionDown(input, "MoveDown")) {
            velocity.y = -kPlayerSpeedMetersPerSecond;
        }
        physics.SetLinearVelocity(m_playerEntity, velocity);

        if (m_inputMapping.IsActionPressed(input, "Jump")) {
            m_scene->GetPhysicsWorld().ApplyLinearImpulse(m_playerEntity, {0.0f, kJumpImpulseMetersPerSecond});
        }
    }

    void UpdateCamera(float /*delta*/) {
        const glm::vec2 scroll = Xora2D::Input::GetScrollDelta();
        if (m_cameraEntity.IsValid() && scroll.y != 0.0f) {
            auto& camera = m_cameraEntity.GetComponent<Xora2D::CameraComponent>();
            camera.Zoom = std::clamp(camera.Zoom + scroll.y * kCameraZoomStep, 0.35f, 2.5f);
        }
        if (m_playerEntity.IsValid() && m_cameraEntity.IsValid()) {
            const auto& camera = m_cameraEntity.GetComponent<Xora2D::CameraComponent>();
            const float zoom = std::max(camera.Zoom, 0.01f);
            const float viewWidth = m_scene->GetViewportWidth() / zoom;
            const float viewHeight = m_scene->GetViewportHeight() / zoom;
            const glm::vec2 playerPosition = m_playerEntity.GetComponent<Xora2D::TransformComponent>().Translation;

            // OrthographicCamera treats position as the bottom-left of the visible region (+Y up).
            auto& cameraTransform = m_cameraEntity.GetComponent<Xora2D::TransformComponent>();
            cameraTransform.Translation = playerPosition - glm::vec2(viewWidth * 0.5f, viewHeight * 0.5f);
            cameraTransform.RecalculateLocalMatrix();
        }

        if (m_logCooldown <= 0.0f) {
            XORA_LOG_INFO("Mouse position (window space): {},{}", Xora2D::Input::GetMouseX(),
                          Xora2D::Input::GetMouseY());
        }
    }

    Xora2D::AssetRegistry m_assetRegistry;
    Xora2D::AssetManager m_assetManager{m_assetRegistry};
    Xora2D::AssetHandle m_logoAssetHandle;
    std::shared_ptr<Xora2D::Scene> m_scene;
    Xora2D::Entity m_cameraEntity;
    Xora2D::Entity m_playerEntity;
    Xora2D::Entity m_ecsMusicEntity;
    Xora2D::InputMapping m_inputMapping;
    Xora2D::AudioSource m_musicSource;
    Xora2D::AudioSource m_sfxSource;
    Xora2D::AssetHandle m_sfxBeepHandle;
    Xora2D::AssetHandle m_sfxClickHandle;
    Xora2D::AssetHandle m_musicHandle;
    bool m_sfxLooping = false;
    bool m_playerPythonControl = false;
    float m_logCooldown = 0.0f;
};

} // namespace

int main() {
    try {
        XORA_LOG_INFO("Sandbox starting");
        int exitCode = EXIT_FAILURE;
        {
            SandboxApp application;
            exitCode = application.Run();
        }
        XORA_LOG_INFO("Sandbox stopped");
        return exitCode;
    } catch (const std::exception& exception) {
        XORA_LOG_ERROR("{}", exception.what());
        return EXIT_FAILURE;
    }
}