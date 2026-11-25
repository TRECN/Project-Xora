#include "Xora2D/Scene/Scene.h"

#include "Xora2D/Prefab/PrefabComponents.h"
#include "Xora2D/Tilemap/TilemapComponent.h"
#include "Xora2D/Tilemap/TilemapRendering.h"
#include "Xora2D/Tilemap/Tileset.h"

#include "SceneAnimationSystem.h"
#include "SceneAudioSystem.h"
#include "Scripting/ScenePythonScriptSystem.h"
#include "Particles/SceneParticleSystem.h"
#include "Xora2D/Particles/ParticleSystemComponent.h"

#include "Xora2D/Animation/AnimationClipAsset.h"
#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Animation/SpriteSheetLayout.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Audio/AudioEngine.h"
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

template <typename Component>
void CopyComponentIfPresent(const entt::registry& source, entt::entity from, entt::registry& target, entt::entity to) {
    if (const Component* component = source.try_get<Component>(from)) {
        target.emplace_or_replace<Component>(to, *component);
    }
}

// Every component type a scene can hold besides IDComponent/TagComponent (created with the entity).
template <typename... Components>
void CopyComponents(const entt::registry& source, entt::entity from, entt::registry& target, entt::entity to) {
    (CopyComponentIfPresent<Components>(source, from, target, to), ...);
}

} // namespace

std::shared_ptr<Scene> Scene::Create() {
    return std::shared_ptr<Scene>(new Scene());
}

std::shared_ptr<Scene> Scene::Copy(const Scene& source) {
    std::shared_ptr<Scene> copy = Scene::Create();
    copy->m_sceneName = source.m_sceneName;
    copy->m_viewportWidth = source.m_viewportWidth;
    copy->m_viewportHeight = source.m_viewportHeight;
    copy->m_physics->Configure(source.m_physics->GetSettings());

    const entt::registry& from = source.m_registry->Registry;
    entt::registry& to = copy->m_registry->Registry;

    // Views iterate newest-first; create in reverse so the copy keeps the source's creation (and draw) order.
    std::vector<entt::entity> entities;
    entities.reserve(source.m_uuidIndex.size());
    for (const entt::entity entity : from.view<IDComponent>()) {
        entities.push_back(entity);
    }
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        const entt::entity sourceEntity = *it;
        const entt::entity created = to.create();
        const std::uint32_t handle = FromEntt(created);
        const UUID uuid = from.get<IDComponent>(sourceEntity).ID;
        to.emplace<IDComponent>(created, IDComponent{uuid});
        const TagComponent* tag = from.try_get<TagComponent>(sourceEntity);
        to.emplace<TagComponent>(created, TagComponent{tag != nullptr ? tag->Tag : std::string("Entity")});
        CopyComponents<TransformComponent, SpriteRendererComponent, CameraComponent, RigidBody2DComponent,
                       BoxCollider2DComponent, CircleCollider2DComponent, AudioSourceComponent, AudioListenerComponent,
                       PythonScriptComponent, AnimationComponent, PrefabInstanceComponent,
                       TilemapComponent, ParticleSystemComponent>(from, sourceEntity, to, created);
        if (!to.all_of<TransformComponent>(created)) {
            to.emplace<TransformComponent>(created).RecalculateLocalMatrix();
        }
        copy->IndexEntity(handle);
    }
    return copy;
}

Scene::Scene()
    : m_registry(std::make_unique<Registry>()),
      m_physics(std::make_unique<PhysicsWorld2D>()),
      m_audioSystem(std::make_unique<SceneAudioSystem>()),
      m_scriptSystem(std::make_unique<ScenePythonScriptSystem>()),
      m_animationSystem(std::make_unique<SceneAnimationSystem>()),
      m_particleSystem(std::make_unique<SceneParticleSystem>()) {}

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
    if (m_audioSystem) {
        m_audioSystem->Clear();
    }
    if (m_scriptSystem) {
        m_scriptSystem->Clear();
    }
    if (m_animationSystem) {
        m_animationSystem->Clear();
    }
    if (source.m_particleSystem) {
        source.m_particleSystem->Clear();
    }
    if (m_particleSystem) {
        m_particleSystem->Clear();
    }
}

void Scene::PauseParticles() {
    if (!m_particleSystem) return;
    auto& registry = m_registry->Registry;
    for (auto [entity, comp] : registry.view<ParticleSystemComponent>().each()) {
        comp.Pause();
    }
}

void Scene::ResumeParticles() {
    if (!m_particleSystem) return;
    auto& registry = m_registry->Registry;
    for (auto [entity, comp] : registry.view<ParticleSystemComponent>().each()) {
        comp.Resume();
    }
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
    if (m_audioSystem) {
        m_audioSystem->Clear();
    }
    if (m_scriptSystem) {
        m_scriptSystem->Clear();
    }
    if (m_animationSystem) {
        m_animationSystem->Clear();
    }
    if (m_particleSystem) {
        m_particleSystem->Clear();
    }
}

void Scene::DestroyEntity(Entity entity) {
    if (!IsValid(entity)) {
        return;
    }
    if (m_audioSystem) {
        m_audioSystem->OnEntityDestroyed(entity.m_handle);
    }
    if (m_scriptSystem) {
        m_scriptSystem->OnEntityDestroyed(entity.m_handle);
    }
    if (m_animationSystem) {
        m_animationSystem->OnEntityDestroyed(entity.m_handle);
    }
    if (m_particleSystem) {
        m_particleSystem->OnEntityDestroyed(entity.m_handle);
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
    if (m_audioSystem) {
        m_audioSystem->PrepareForShutdown();
    }
    if (m_scriptSystem) {
        m_scriptSystem->PrepareForShutdown();
    }
    if (m_animationSystem) {
        m_animationSystem->PrepareForShutdown();
    }
    if (m_particleSystem) {
        m_particleSystem->PrepareForShutdown();
    }
    if (!m_physics) {
        return;
    }
    m_physics->SetSimulationEnabled(false);
    m_physics->PrepareShutdown();
}

void Scene::OnUpdate(float deltaSeconds) {
    m_physics->Step(*this, deltaSeconds);
    if (m_audioSystem) {
        m_audioSystem->OnSceneUpdate(*this, deltaSeconds);
    }
    if (m_animationSystem) {
        m_animationSystem->OnSceneUpdate(*this, deltaSeconds);
    }
    if (m_scriptSystem) {
        m_scriptSystem->OnSceneUpdate(*this, deltaSeconds);
    }
    if (m_particleSystem) {
        m_particleSystem->OnSceneUpdate(*this, deltaSeconds);
    }
}

void Scene::SetAudioEngine(AudioEngine* audioEngine) {
    if (m_audioSystem) {
        m_audioSystem->SetAudioEngine(audioEngine);
    }
}

void Scene::SetPythonScriptEngine(PythonScriptEngine* scriptEngine) {
    if (m_scriptSystem) {
        m_scriptSystem->SetScriptEngine(scriptEngine);
    }
}

void Scene::SetAnimationEventCallback(std::function<void(const AnimationEvent&)> callback) {
    if (m_animationSystem) {
        m_animationSystem->SetEventCallback(std::move(callback));
    }
}

void Scene::SetAssetManager(AssetManager* assetManager) {
    m_assetManager = assetManager;
    if (m_audioSystem) {
        m_audioSystem->SetAssetManager(assetManager);
    }
    if (m_scriptSystem) {
        m_scriptSystem->SetAssetManager(assetManager);
    }
    if (m_animationSystem) {
        m_animationSystem->SetAssetManager(assetManager);
    }
    if (m_particleSystem) {
        m_particleSystem->SetAssetManager(assetManager);
    }
}

void Scene::StopAudioInGroup(AudioGroup group) {
    if (m_audioSystem) {
        m_audioSystem->StopEntityPlaybacksInGroup(*this, group);
    }
}

void Scene::StopLoopingAudio() {
    if (m_audioSystem) {
        m_audioSystem->StopLoopingEntityPlaybacks(*this);
    }
}

void Scene::StopEntityAudio(const Entity& entity) {
    if (m_audioSystem && IsValid(entity)) {
        m_audioSystem->StopEntityPlayback(entity.m_handle);
    }
}

void Scene::ReleasePythonScriptInstance(const Entity& entity) {
    if (m_scriptSystem && IsValid(entity)) {
        m_scriptSystem->OnEntityDestroyed(entity.m_handle);
    }
}

void Scene::SynchronizePhysicsBodies() {
    m_physics->SyncSceneBodies(*this);
}

void Scene::PauseAudio() {
    if (m_audioSystem) {
        m_audioSystem->PauseAllEntityPlaybacks();
    }
}

void Scene::ResumeAudio() {
    if (m_audioSystem) {
        m_audioSystem->ResumeAllEntityPlaybacks();
    }
}

void Scene::PauseEntityAudio(const Entity& entity) {
    if (m_audioSystem && IsValid(entity)) {
        m_audioSystem->PauseEntityPlayback(entity.m_handle);
    }
}

void Scene::ResumeEntityAudio(const Entity& entity) {
    if (m_audioSystem && IsValid(entity)) {
        m_audioSystem->ResumeEntityPlayback(entity.m_handle);
    }
}

bool Scene::IsEntityAudioPlaying(const Entity& entity) const {
    if (!m_audioSystem || !IsValid(entity)) {
        return false;
    }
    return m_audioSystem->IsEntityPlaybackActive(entity.m_handle);
}

void Scene::ResolveSpriteTextures() {
    if (m_assetManager == nullptr) {
        return;
    }
    auto& registry = m_registry->Registry;
    for (const auto entity : registry.view<SpriteRendererComponent>()) {
        auto& sprite = registry.get<SpriteRendererComponent>(entity);
        if (!sprite.Visible) {
            continue;
        }
        if (registry.all_of<AnimationComponent>(entity)) {
            auto& animation = registry.get<AnimationComponent>(entity);
            AssetHandle textureHandle = sprite.TextureAsset;
            if (animation.ActiveClip.IsValid()) {
                if (const std::shared_ptr<const AnimationClipAsset> clipAsset =
                        m_assetManager->GetLoadedAnimationClip(animation.ActiveClip)) {
                    textureHandle = clipAsset->GetClip().TextureAsset;
                }
            } else if (animation.DefaultClip.IsValid()) {
                textureHandle = animation.DefaultClip;
                if (const std::shared_ptr<const AnimationClipAsset> clipAsset =
                        m_assetManager->GetLoadedAnimationClip(animation.DefaultClip)) {
                    textureHandle = clipAsset->GetClip().TextureAsset;
                }
            }
            if (textureHandle.IsValid()) {
                sprite.TextureAsset = textureHandle;
            }
        }
        if (!sprite.TextureAsset.IsValid()) {
            continue;
        }
        if (sprite.Texture != nullptr) {
            continue;
        }
        const AssetResult loaded = m_assetManager->LoadTexture(sprite.TextureAsset);
        if (!loaded.Success) {
            XORA_LOG_WARN("Sprite '{}' failed to resolve texture asset {}: {}", registry.get<TagComponent>(entity).Tag,
                          sprite.TextureAsset.ToString(), loaded.Message);
            continue;
        }
        sprite.Texture = m_assetManager->GetLoadedTexture(sprite.TextureAsset);
    }
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
    ResolveSpriteTextures();
    OrthographicCamera camera;
    const SceneCamera::Result cameraResult = SceneCamera::BuildPrimaryCamera(*this, camera);
    if (cameraResult == SceneCamera::Result::NoPrimaryCamera) {
        XORA_LOG_WARN("Scene::OnRender skipped: no enabled primary camera entity");
        return;
    }
    if (cameraResult == SceneCamera::Result::MultiplePrimaryCameras) {
        XORA_LOG_WARN("Scene::OnRender: multiple primary cameras; using the first one discovered");
    }
    OnRender(renderer, camera);
}

void Scene::OnRender(Renderer2D& renderer, const OrthographicCamera& camera) {
    ResolveSpriteTextures();
    renderer.BeginScene(camera);

    // Tilemaps: layers below sprites first, sprites, then layers marked AboveSprites.
    m_tilemapStats = {};
    TilemapRenderResources tilemapResources{};
    tilemapResources.GetTileset = [this](AssetHandle handle) -> const Tileset* {
        if (m_assetManager == nullptr || !handle.IsValid() || !m_assetManager->LoadTileset(handle).Success) {
            return nullptr;
        }
        return m_assetManager->GetLoadedTileset(handle).get();
    };
    tilemapResources.GetTexture = [this](AssetHandle handle) -> const Texture2D* {
        if (m_assetManager == nullptr || !handle.IsValid() || !m_assetManager->LoadTexture(handle).Success) {
            return nullptr;
        }
        return m_assetManager->GetLoadedTexture(handle).get();
    };
    const auto drawTilemaps = [&](bool aboveSprites) {
        for (auto [entity, transform, tilemap] : m_registry->Registry.view<TransformComponent, TilemapComponent>().each()) {
            (void)entity;
            RenderTilemap(renderer, tilemap, transform.ComputeMatrix(), camera, aboveSprites, tilemapResources,
                          &m_tilemapStats);
        }
    };
    drawTilemaps(false);

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
        const Transform2D drawTransform = ToRendererTransform(transform, sprite);
        if (registry.all_of<AnimationComponent>(entity)) {
            const auto& animation = registry.get<AnimationComponent>(entity);
            if (animation.FrameUVValid) {
                glm::vec2 uvCorners[4];
                BuildQuadUvsFromRect(animation.FrameUV, uvCorners);
                renderer.DrawQuad(drawTransform, *sprite.Texture, sprite.Color, uvCorners);
                continue;
            }
        }
        renderer.DrawQuad(drawTransform, *sprite.Texture, sprite.Color);
    }

    drawTilemaps(true);

    // Particle systems (rendered after all sprites and foreground tiles)
    if (m_particleSystem) {
        m_particleSystem->OnSceneRender(*this, renderer, camera);
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