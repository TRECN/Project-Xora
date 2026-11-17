#pragma once

#include "Xora2D/Audio/AudioTypes.h"
#include "Xora2D/Core/UUID.h"
#include "Xora2D/Scene/Entity.h"

#include "Xora2D/Physics/PhysicsEvents.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace Xora2D {

class AssetManager;
class AudioEngine;
class PhysicsWorld2D;
class SceneAudioSystem;
struct PhysicsWorld2DDetail;
class Renderer2D;
class SceneSerializer;

// Owns the ECS registry and entity lifecycle for one logical level.
class Scene : public std::enable_shared_from_this<Scene> {
public:
    static std::shared_ptr<Scene> Create();

    Scene();
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    Entity CreateEntity(const std::string& name = "Entity");
    Entity CreateEntityWithUUID(const std::string& name, const UUID& uuid);
    void DestroyEntity(Entity entity);

    void Clear();
    void ReplaceContentsFrom(Scene& source);

    bool IsValid(const Entity& entity) const;

    Entity FindEntityByUUID(const UUID& uuid) const;
    Entity FindEntityByName(const std::string& name) const;

    void SetViewportSize(float width, float height);
    float GetViewportWidth() const { return m_viewportWidth; }
    float GetViewportHeight() const { return m_viewportHeight; }

    void OnUpdate(float deltaSeconds);
    void OnRender(Renderer2D& renderer);

    // Stops physics simulation and suppresses gameplay collision callbacks (e.g. on application quit).
    void PrepareForShutdown();

    std::size_t GetEntityCount() const;
    std::vector<UUID> GetEntityUUIDs() const;

    PhysicsWorld2D& GetPhysicsWorld();
    const PhysicsWorld2D& GetPhysicsWorld() const;

    void SetPhysicsEventCallback(std::function<void(const PhysicsCollisionEvent&)> callback);

    void SetAssetManager(AssetManager* assetManager);
    void SetAudioEngine(AudioEngine* audioEngine);
    void SetPythonScriptEngine(PythonScriptEngine* scriptEngine);
    void StopAudioInGroup(AudioGroup group);
    void StopLoopingAudio();
    void StopEntityAudio(const Entity& entity);
    void PauseEntityAudio(const Entity& entity);
    void ResumeEntityAudio(const Entity& entity);
    [[nodiscard]] bool IsEntityAudioPlaying(const Entity& entity) const;
    void ResolveSpriteTextures();

    const std::string& GetSceneName() const { return m_sceneName; }
    void SetSceneName(std::string name) { m_sceneName = std::move(name); }

private:
    friend class Entity;
    friend class SceneCamera;
    friend class PhysicsWorld2D;
    friend struct PhysicsWorld2DDetail;
    friend class SceneSerializer;
    friend class SceneAudioSystem;
    friend class ScenePythonScriptSystem;

    template <typename Component, typename... Args>
    Component& EmplaceComponent(std::uint32_t handle, Args&&... args);

    template <typename Component>
    Component& GetComponent(std::uint32_t handle);

    template <typename Component>
    const Component& GetComponent(std::uint32_t handle) const;

    template <typename Component>
    bool HasComponent(std::uint32_t handle) const;

    template <typename Component>
    void RemoveComponent(std::uint32_t handle);

    void IndexEntity(std::uint32_t handle);
    void UnindexEntity(std::uint32_t handle);
    void ReindexEntityName(std::uint32_t handle);

    struct Registry;
    std::unique_ptr<Registry> m_registry;

    std::unordered_map<UUID, std::uint32_t> m_uuidIndex;
    std::unordered_map<std::string, std::uint32_t> m_nameIndex;

    float m_viewportWidth = 1280.0f;
    float m_viewportHeight = 720.0f;

    std::unique_ptr<PhysicsWorld2D> m_physics;
    std::string m_sceneName = "Untitled";
    AssetManager* m_assetManager = nullptr;
    std::unique_ptr<SceneAudioSystem> m_audioSystem;
    std::unique_ptr<ScenePythonScriptSystem> m_scriptSystem;
};

} // namespace Xora2D