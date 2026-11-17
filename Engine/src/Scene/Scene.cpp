#include "Xora2D/Scene/Scene.h"

#include "Xora2D/Physics/PhysicsWorld2D.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/SceneCamera.h"
#include "Xora2D/Renderer/Renderer2D.h"

#include "SceneRegistry.h"

#include <entt/entt.hpp>

namespace Xora2D {
namespace {

std::uint32_t FromEntt(entt::entity entity) {
    return static_cast<std::uint32_t>(entt::to_integral(entity));
}

Transform2D ToRendererTransform(const TransformComponent& transform, const SpriteRendererComponent& sprite) {
    Transform2D output{};
    output.Position = transform.Translation;
    output.Size = sprite.Size * transform.Scale;
    output.RotationRadians = transform.RotationRadians;
    return output;
}

} // namespace

std::shared_ptr<Scene> Scene::Create() {
    return std::shared_ptr<Scene>(new Scene());
}

Scene::Scene()
    : m_registry(std::make_unique<Registry>()), m_physics(std::make_unique<PhysicsWorld2D>()) {}

Scene::~Scene() {
    PrepareForShutdown();
    m_physics.reset();
    m_uuidIndex.clear();
    m_nameIndex.clear();
}

Entity Scene::CreateEntity(const std::string& name) {
    if (m_nameIndex.contains(name)) {
        XORA_LOG_WARN("Duplicate entity name '{}'; FindEntityByName returns the most recently created entity",
                      name);
    }
    const entt::entity created = m_registry->Registry.create();
    const std::uint32_t handle = FromEntt(created);

    const UUID uuid = UUID::Generate();
    m_registry->Emplace<IDComponent>(handle, IDComponent{uuid});
    m_registry->Emplace<TagComponent>(handle, TagComponent{name});

    auto& transform = m_registry->Emplace<TransformComponent>(handle);
    transform.RecalculateLocalMatrix();

    IndexEntity(handle);
    return Entity(this, handle);
}

Entity Scene::CreateEntityWithUUID(const std::string& name, const UUID& uuid) {
    if (!uuid.IsValid()) {
        throw std::runtime_error("CreateEntityWithUUID requires a valid UUID");
    }
    if (m_uuidIndex.contains(uuid)) {
        throw std::runtime_error("CreateEntityWithUUID: duplicate UUID");
    }
    if (m_nameIndex.contains(name)) {
        XORA_LOG_WARN("Duplicate entity name '{}'; FindEntityByName returns the most recently created entity",
                      name);
    }
    const entt::entity created = m_registry->Registry.create();
    const std::uint32_t handle = FromEntt(created);

    UUID::RegisterUsed(uuid);
    m_registry->Emplace<IDComponent>(handle, IDComponent{uuid});
    m_registry->Emplace<TagComponent>(handle, TagComponent{name});

    auto& transform = m_registry->Emplace<TransformComponent>(handle);
    transform.RecalculateLocalMatrix();

    IndexEntity(handle);
    return Entity(this, handle);
}

void Scene::ReplaceContentsFrom(Scene& source) {
    if (this == &source) {
        return;
    }
    PrepareForShutdown();
    source.PrepareForShutdown();

    m_sceneName = source.m_sceneName;
    m_viewportWidth = source.m_viewportWidth;
    m_viewportHeight = source.m_viewportHeight;
    m_registry = std::move(source.m_registry);
    m_uuidIndex = std::move(source.m_uuidIndex);
    m_nameIndex = std::move(source.m_nameIndex);
    m_physics = std::make_unique<PhysicsWorld2D>();

    source.m_registry = std::make_unique<Registry>();
    source.m_uuidIndex.clear();
    source.m_nameIndex.clear();
    source.m_physics = std::make_unique<PhysicsWorld2D>();
    source.m_sceneName = "Untitled";
}

void Scene::Clear() {
    PrepareForShutdown();
    std::vector<std::uint32_t> handles;
    handles.reserve(m_uuidIndex.size());
    for (const auto& entry : m_uuidIndex) {
        handles.push_back(entry.second);
    }
    for (const std::uint32_t handle : handles) {
        UnindexEntity(handle);
        m_registry->Destroy(handle);
    }
    m_uuidIndex.clear();
    m_nameIndex.clear();
    m_physics = std::make_unique<PhysicsWorld2D>();
}

void Scene::DestroyEntity(Entity entity) {
    if (!IsValid(entity)) {
        return;
    }
    m_physics->OnEntityDestroyed(*this, entity);
    UnindexEntity(entity.m_handle);
    m_registry->Destroy(entity.m_handle);
}

bool Scene::IsValid(const Entity& entity) const {
    return entity.m_scene == this && m_registry->Valid(entity.m_handle);
}

Entity Scene::FindEntityByUUID(const UUID& uuid) const {
    const auto found = m_uuidIndex.find(uuid);
    if (found == m_uuidIndex.end()) {
        return {};
    }
    return Entity(const_cast<Scene*>(this), found->second);
}

Entity Scene::FindEntityByName(const std::string& name) const {
    const auto found = m_nameIndex.find(name);
    if (found == m_nameIndex.end()) {
        return {};
    }
    return Entity(const_cast<Scene*>(this), found->second);
}

void Scene::SetViewportSize(float width, float height) {
    m_viewportWidth = std::max(width, 1.0f);
    m_viewportHeight = std::max(height, 1.0f);
}

void Scene::PrepareForShutdown() {
    if (!m_physics) {
        return;
    }
    m_physics->SetSimulationEnabled(false);
    m_physics->PrepareShutdown();
}

void Scene::OnUpdate(float deltaSeconds) {
    m_physics->Step(*this, deltaSeconds);
}

PhysicsWorld2D& Scene::GetPhysicsWorld() {
    return *m_physics;
}

const PhysicsWorld2D& Scene::GetPhysicsWorld() const {
    return *m_physics;
}

void Scene::SetPhysicsEventCallback(std::function<void(const PhysicsCollisionEvent&)> callback) {
    m_physics->SetEventCallback(std::move(callback));
}

void Scene::OnRender(Renderer2D& renderer) {
    OrthographicCamera camera;
    const SceneCamera::Result cameraResult = SceneCamera::BuildPrimaryCamera(*this, camera);
    if (cameraResult == SceneCamera::Result::NoPrimaryCamera) {
        XORA_LOG_WARN("Scene::OnRender skipped: no enabled primary camera entity");
        return;
    }
    if (cameraResult == SceneCamera::Result::MultiplePrimaryCameras) {
        XORA_LOG_WARN("Scene::OnRender: multiple primary cameras; using the first one discovered");
    }

    renderer.BeginScene(camera);

    auto& registry = m_registry->Registry;
    for (auto [entity, transform, sprite] : registry.view<TransformComponent, SpriteRendererComponent>().each()) {
        (void)entity;
        if (!sprite.Visible || sprite.Texture) {
            continue;
        }
        renderer.DrawQuad(ToRendererTransform(transform, sprite), sprite.Color);
    }
    for (auto [entity, transform, sprite] : registry.view<TransformComponent, SpriteRendererComponent>().each()) {
        (void)entity;
        if (!sprite.Visible || sprite.Texture == nullptr) {
            continue;
        }
        renderer.DrawQuad(ToRendererTransform(transform, sprite), *sprite.Texture, sprite.Color);
    }

    renderer.EndScene();
}

std::size_t Scene::GetEntityCount() const {
    return m_uuidIndex.size();
}

std::vector<UUID> Scene::GetEntityUUIDs() const {
    std::vector<UUID> uuids;
    uuids.reserve(m_uuidIndex.size());
    for (const auto& entry : m_uuidIndex) {
        uuids.push_back(entry.first);
    }
    return uuids;
}

void Scene::IndexEntity(std::uint32_t handle) {
    const IDComponent& id = m_registry->Get<IDComponent>(handle);
    const TagComponent& tag = m_registry->Get<TagComponent>(handle);
    m_uuidIndex[id.ID] = handle;
    m_nameIndex[tag.Tag] = handle;
}

void Scene::UnindexEntity(std::uint32_t handle) {
    if (m_registry->Has<IDComponent>(handle)) {
        m_uuidIndex.erase(m_registry->Get<IDComponent>(handle).ID);
    }
    if (m_registry->Has<TagComponent>(handle)) {
        m_nameIndex.erase(m_registry->Get<TagComponent>(handle).Tag);
    }
}

SceneCamera::Result SceneCamera::BuildPrimaryCamera(const Scene& scene, OrthographicCamera& camera) {
    const auto& registry = scene.m_registry->Registry;

    entt::entity primary = entt::null;
    std::size_t primaryCount = 0;

    for (const auto entity : registry.view<CameraComponent, TransformComponent>()) {
        const CameraComponent& cameraComponent = registry.get<CameraComponent>(entity);
        if (!cameraComponent.Enabled || !cameraComponent.Primary) {
            continue;
        }
        ++primaryCount;
        if (primary == entt::null) {
            primary = entity;
        }
    }

    if (primary == entt::null) {
        return Result::NoPrimaryCamera;
    }

    const TransformComponent& transform = registry.get<TransformComponent>(primary);
    const CameraComponent& cameraComponent = registry.get<CameraComponent>(primary);

    camera.SetViewportSize(scene.GetViewportWidth(), scene.GetViewportHeight());
    camera.SetPosition(transform.Translation);
    camera.SetRotationRadians(transform.RotationRadians);
    camera.SetZoom(cameraComponent.Zoom);

    if (primaryCount > 1) {
        return Result::MultiplePrimaryCameras;
    }
    return Result::Ok;
}

void Scene::ReindexEntityName(std::uint32_t handle) {
    for (auto iterator = m_nameIndex.begin(); iterator != m_nameIndex.end();) {
        if (iterator->second == handle) {
            iterator = m_nameIndex.erase(iterator);
        } else {
            ++iterator;
        }
    }
    m_nameIndex[m_registry->Get<TagComponent>(handle).Tag] = handle;
}

} // namespace Xora2D