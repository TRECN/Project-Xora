#include "ScenePythonScriptSystem.h"

#include "../Scene/SceneRegistry.h"

#include "Xora2D/Scripting/PythonScriptComponent.h"
#include "Xora2D/Scripting/PythonScriptEngine.h"
#include "Xora2D/Scene/Scene.h"

namespace Xora2D {

void ScenePythonScriptSystem::OnSceneUpdate(Scene& scene, float deltaSeconds) {
    if (m_scriptEngine == nullptr || !m_scriptEngine->IsAvailable() || m_assetManager == nullptr) {
        return;
    }

    m_scriptEngine->OnSceneScriptsBegin(scene);

    auto& registry = scene.m_registry->Registry;
    std::vector<std::uint32_t> scriptEntities;
    scriptEntities.reserve(32);
    for (const auto entity : registry.view<PythonScriptComponent>()) {
        scriptEntities.push_back(static_cast<std::uint32_t>(entt::to_integral(entity)));
    }

    for (const std::uint32_t handle : scriptEntities) {
        if (!scene.m_registry->Valid(handle)) {
            continue;
        }
        const Entity entity(&scene, handle);
        if (!entity.IsValid()) {
            continue;
        }
        const auto& script = scene.m_registry->Get<PythonScriptComponent>(handle);
        if (!script.Enabled) {
            m_scriptEngine->DestroyInstance(handle);
            continue;
        }
        (void)m_scriptEngine->EnsureInstance(scene, entity, script, *m_assetManager);
        m_scriptEngine->UpdateInstance(scene, handle, deltaSeconds, script);
    }

    m_scriptEngine->OnSceneScriptsEnd(scene);
}

void ScenePythonScriptSystem::OnEntityDestroyed(std::uint32_t entityHandle) {
    if (m_scriptEngine != nullptr) {
        m_scriptEngine->DestroyInstance(entityHandle);
    }
}

void ScenePythonScriptSystem::Clear() {
    PrepareForShutdown();
}

void ScenePythonScriptSystem::PrepareForShutdown() {
    if (m_scriptEngine != nullptr) {
        m_scriptEngine->PrepareShutdown();
    }
}

} // namespace Xora2D