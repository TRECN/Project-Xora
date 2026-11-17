#pragma once

#include <cstdint>

namespace Xora2D {

class AssetManager;
class Entity;
class PythonScriptEngine;
class Scene;

class ScenePythonScriptSystem {
public:
    void SetScriptEngine(PythonScriptEngine* engine) { m_scriptEngine = engine; }
    void SetAssetManager(AssetManager* assetManager) { m_assetManager = assetManager; }

    void OnSceneUpdate(Scene& scene, float deltaSeconds);
    void OnEntityDestroyed(std::uint32_t entityHandle);
    void Clear();

    void PrepareForShutdown();

private:
    PythonScriptEngine* m_scriptEngine = nullptr;
    AssetManager* m_assetManager = nullptr;
};

} // namespace Xora2D