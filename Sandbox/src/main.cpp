#include "Xora2D/Core/Application.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Events/Event.h"
#include "Xora2D/Events/KeyEvent.h"
#include "Xora2D/Events/WindowEvent.h"
#include "Xora2D/Input/KeyCodes.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Physics/PhysicsEvents.h"
#include "Xora2D/Physics/PhysicsWorld2D.h"
#include "Xora2D/Renderer/Renderer2D.h"
#include "Xora2D/Renderer/Texture2D.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Serialization/SceneSerializer.h"

#include <glm/vec2.hpp>

#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr float kCameraSpeed = 420.0f;

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

        m_logoTexture = Xora2D::Texture2D::CreateFromFile(
            Xora2D::ResolveResourcePath("Textures/Xora2D_Logo.png").string());

        m_scene->SetPhysicsEventCallback([](const Xora2D::PhysicsCollisionEvent& event) {
            const char* kind = event.Kind == Xora2D::PhysicsCollisionKind::Sensor ? "Sensor" : "Collision";
            const char* phase = event.Phase == Xora2D::PhysicsCollisionPhase::Begin ? "begin" : "end";
            XORA_LOG_INFO("{} {} — {} / {}", kind, phase, event.EntityA.ToString(), event.EntityB.ToString());
        });

        BuildScene();
        m_scene->SetSceneName("SandboxPhysicsDemo");
        DemonstrateSceneSerialization();
        XORA_LOG_INFO("Physics sandbox: {} entities", m_scene->GetEntityCount());
    }

protected:
    void OnShutdownRequested() override {
        if (m_scene) {
            m_scene->PrepareForShutdown();
        }
    }

    void OnUpdate(double deltaSeconds) override {
        m_scene->OnUpdate(static_cast<float>(deltaSeconds));
        UpdateCamera(static_cast<float>(deltaSeconds));
        m_logCooldown -= static_cast<float>(deltaSeconds);
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

        Xora2D::EventDispatcher<Xora2D::KeyPressedEvent> keyPressed(event);
        keyPressed.Dispatch([this](Xora2D::KeyPressedEvent& pressed) {
            SetMovementKey(pressed.GetKey(), true);
            return false;
        });

        Xora2D::EventDispatcher<Xora2D::KeyReleasedEvent> keyReleased(event);
        keyReleased.Dispatch([this](Xora2D::KeyReleasedEvent& released) {
            SetMovementKey(released.GetKey(), false);
            return false;
        });
    }

private:
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
        logoSprite.Texture = m_logoTexture;
        logoSprite.TexturePath = "Textures/Xora2D_Logo.png";
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
        const Xora2D::SceneSerializeResult loadResult = Xora2D::SceneSerializer::Deserialize(*loadedScene, scenePath);
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
        m_cameraEntity = m_scene->FindEntityByUUID(cameraUuid);
        if (!m_cameraEntity.IsValid()) {
            m_cameraEntity = m_scene->FindEntityByName("MainCamera");
        }
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

    void UpdateCamera(float delta) {
        glm::vec2 velocity{0.0f};
        if (m_moveLeft) {
            velocity.x -= kCameraSpeed * delta;
        }
        if (m_moveRight) {
            velocity.x += kCameraSpeed * delta;
        }
        if (m_moveDown) {
            velocity.y -= kCameraSpeed * delta;
        }
        if (m_moveUp) {
            velocity.y += kCameraSpeed * delta;
        }
        auto& transform = m_cameraEntity.GetComponent<Xora2D::TransformComponent>();
        transform.Translation += velocity;
        transform.RecalculateLocalMatrix();
    }

    void SetMovementKey(Xora2D::Key key, bool down) {
        switch (key) {
        case Xora2D::Key::A:
        case Xora2D::Key::Left:
            m_moveLeft = down;
            break;
        case Xora2D::Key::D:
        case Xora2D::Key::Right:
            m_moveRight = down;
            break;
        case Xora2D::Key::S:
        case Xora2D::Key::Down:
            m_moveDown = down;
            break;
        case Xora2D::Key::W:
        case Xora2D::Key::Up:
            m_moveUp = down;
            break;
        default:
            break;
        }
    }

    std::shared_ptr<Xora2D::Scene> m_scene;
    std::shared_ptr<Xora2D::Texture2D> m_logoTexture;
    Xora2D::Entity m_cameraEntity;
    float m_logCooldown = 0.0f;
    bool m_moveLeft = false;
    bool m_moveRight = false;
    bool m_moveUp = false;
    bool m_moveDown = false;
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