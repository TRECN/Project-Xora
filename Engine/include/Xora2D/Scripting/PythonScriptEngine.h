#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"
#include "Xora2D/Scripting/ScriptServices.h"
#include "Xora2D/Scripting/ScriptTypes.h"

#include <cstdint>
#include <memory>

namespace Xora2D {

class AssetManager;
class Entity;
class Scene;

// Owns the embedded CPython interpreter (one per application).
class PythonScriptEngine {
public:
    PythonScriptEngine();
    ~PythonScriptEngine();

    PythonScriptEngine(const PythonScriptEngine&) = delete;
    PythonScriptEngine& operator=(const PythonScriptEngine&) = delete;

    [[nodiscard]] bool IsScriptingCompiledIn() const;
    [[nodiscard]] bool IsAvailable() const;

    ScriptLoadResult Initialize();
    void Shutdown();
    void PrepareShutdown();

    void SetServices(ScriptServices services);

    void OnSceneScriptsBegin(Scene& scene);
    void OnSceneScriptsEnd(Scene& scene);

    bool EnsureInstance(Scene& scene, Entity entity, const PythonScriptComponent& component, AssetManager& assets);
    void DestroyInstance(std::uint32_t entityHandle);
    void UpdateInstance(Scene& scene, std::uint32_t entityHandle, float deltaSeconds, const PythonScriptComponent& component);

    ScriptReloadResult ReloadScript(AssetHandle handle, AssetManager& assets);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_initialized = false;
    ScriptServices m_services{};
};

} // namespace Xora2D