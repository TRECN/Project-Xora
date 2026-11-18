#pragma once

#include "Xora2D/Animation/AnimationEvents.h"

#include <cstdint>
#include <functional>

namespace Xora2D {

class AssetManager;
class Scene;

class SceneAnimationSystem {
public:
    void SetAssetManager(AssetManager* assetManager) { m_assetManager = assetManager; }
    void SetEventCallback(std::function<void(const AnimationEvent&)> callback) { m_eventCallback = std::move(callback); }

    void OnSceneUpdate(Scene& scene, float deltaSeconds);
    void OnEntityDestroyed(std::uint32_t entityHandle);
    void Clear();
    void PrepareForShutdown();

private:
    AssetManager* m_assetManager = nullptr;
    std::function<void(const AnimationEvent&)> m_eventCallback;
};

} // namespace Xora2D