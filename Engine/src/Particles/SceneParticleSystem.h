#pragma once

#include <cstdint>
#include <glm/vec2.hpp>

namespace Xora2D {

class AssetManager;
class Renderer2D;
class Scene;
class OrthographicCamera;

class SceneParticleSystem {
public:
    void SetAssetManager(AssetManager* assetManager) { m_assetManager = assetManager; }

    // Advance all particle emitters in the scene.
    void OnSceneUpdate(Scene& scene, float deltaSeconds);

    // Render all active particle systems after sprites.
    void OnSceneRender(Scene& scene, Renderer2D& renderer, const OrthographicCamera& camera);

    void OnEntityDestroyed(std::uint32_t entityHandle);
    void Clear();
    void PrepareForShutdown();

private:
    void EnsurePool(Scene& scene);
    void SimulateEmitter(Scene& scene, std::uint32_t handle, float dt);
    void EmitParticle(Scene& scene, std::uint32_t handle, const glm::vec2& emitterPosition);

    AssetManager* m_assetManager = nullptr;
};

} // namespace Xora2D
