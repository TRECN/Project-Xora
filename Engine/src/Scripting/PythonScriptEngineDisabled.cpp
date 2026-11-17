#include "Xora2D/Scripting/PythonScriptEngine.h"

#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"

namespace Xora2D {

struct PythonScriptEngine::Impl {};

PythonScriptEngine::PythonScriptEngine() = default;

PythonScriptEngine::~PythonScriptEngine() = default;

bool PythonScriptEngine::IsScriptingCompiledIn() const {
    return false;
}

bool PythonScriptEngine::IsAvailable() const {
    return false;
}

ScriptLoadResult PythonScriptEngine::Initialize() {
    return ScriptLoadResult{false, "Python scripting is disabled in this build"};
}

void PythonScriptEngine::Shutdown() {}

void PythonScriptEngine::PrepareShutdown() {}

void PythonScriptEngine::SetServices(ScriptServices) {}

void PythonScriptEngine::OnSceneScriptsBegin(Scene&) {}

void PythonScriptEngine::OnSceneScriptsEnd(Scene&) {}

bool PythonScriptEngine::EnsureInstance(Scene&, Entity, const PythonScriptComponent&, AssetManager&) {
    return false;
}

void PythonScriptEngine::DestroyInstance(std::uint32_t) {}

void PythonScriptEngine::UpdateInstance(Scene&, std::uint32_t, float, const PythonScriptComponent&) {}

ScriptReloadResult PythonScriptEngine::ReloadScript(AssetHandle, AssetManager&) {
    return ScriptReloadResult{false, false, "Python scripting is disabled in this build"};
}

} // namespace Xora2D