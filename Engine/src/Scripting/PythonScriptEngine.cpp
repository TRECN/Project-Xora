#if XORA2D_SCRIPTING_ENABLED

#include "Xora2D/Scripting/PythonScriptEngine.h"

#include "PythonBindingsBridge.h"
#include "PythonBindingsInternal.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetTypes.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Scripting/PythonScriptAsset.h"

#include <pybind11/embed.h>
#include <pybind11/eval.h>

namespace py = pybind11;

namespace Xora2D {
namespace {

std::string DefaultClassName(AssetManager& assets, AssetHandle handle, const PythonScriptComponent& component) {
    if (!component.ClassName.empty()) {
        return component.ClassName;
    }
    if (const AssetMetadata* metadata = assets.GetRegistry().FindByHandle(handle)) {
        return metadata->Name;
    }
    return "Script";
}

void ApplyProperties(py::object& instance, const PythonScriptComponent& component) {
    for (const ScriptProperty& property : component.Properties) {
        try {
            switch (property.Type) {
            case ScriptPropertyType::Number:
                instance.attr(property.Name.c_str()) = property.NumberValue;
                break;
            case ScriptPropertyType::Boolean:
                instance.attr(property.Name.c_str()) = property.BooleanValue;
                break;
            case ScriptPropertyType::String:
                instance.attr(property.Name.c_str()) = property.StringValue;
                break;
            }
        } catch (py::error_already_set& exception) {
            exception.restore();
            XORA_LOG_WARN("[Python] Failed to apply property '{}' to script instance", property.Name);
        }
    }
}

std::string FormatPythonError(const std::string& context) {
    if (!PyErr_Occurred()) {
        return context;
    }
    PyErr_Print();
    PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    std::string message = context;
    if (value != nullptr) {
        py::handle valueHandle(value);
        message += ": ";
        message += py::str(valueHandle);
    }
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(traceback);
    PyErr_Clear();
    return message;
}

void InvokeNoArg(py::object& instance, const char* method) {
    if (!instance) {
        return;
    }
    try {
        if (py::hasattr(instance, method)) {
            (void)instance.attr(method)();
        }
    } catch (py::error_already_set& exception) {
        exception.restore();
        XORA_LOG_ERROR("[Python] {}", FormatPythonError(method));
    }
}

void InvokeUpdate(py::object& instance, float deltaSeconds, const UUID& entityId, const std::string& scriptPath) {
    if (!instance) {
        return;
    }
    try {
        if (py::hasattr(instance, "on_update")) {
            (void)instance.attr("on_update")(deltaSeconds);
        }
    } catch (py::error_already_set& exception) {
        exception.restore();
        const std::string context = "on_update entity " + entityId.ToString() + " (" + scriptPath + ")";
        XORA_LOG_ERROR("[Python] {}", FormatPythonError(context));
    }
}

} // namespace

struct InstanceRecord {
    py::object Instance;
    bool Created = false;
    AssetHandle ScriptAsset{};
    std::size_t Generation = 0;
    UUID EntityId{};
    std::weak_ptr<Scene> Scene;
};

struct PythonScriptEngine::Impl {
    std::unique_ptr<py::scoped_interpreter> Interpreter;
    std::unordered_map<std::string, py::dict> ModuleNamespaces;
    std::unordered_map<AssetHandle, std::size_t> ReloadGeneration;
    std::unordered_map<std::uint32_t, InstanceRecord> Instances;
};

PythonScriptEngine::PythonScriptEngine()
    : m_impl(std::make_unique<Impl>()) {}

PythonScriptEngine::~PythonScriptEngine() {
    Shutdown();
}

bool PythonScriptEngine::IsScriptingCompiledIn() const {
    return true;
}

bool PythonScriptEngine::IsAvailable() const {
    return m_initialized;
}

ScriptLoadResult PythonScriptEngine::Initialize() {
    ScriptLoadResult result{};
    if (m_initialized) {
        result.Success = true;
        result.Message = "Python interpreter already initialized";
        return result;
    }

    try {
        m_impl->Interpreter = std::make_unique<py::scoped_interpreter>();
        py::module::import("xora2d");
        m_initialized = true;
        result.Success = true;
    } catch (py::error_already_set& exception) {
        exception.restore();
        result.Message = FormatPythonError("Python interpreter initialization failed");
        XORA_LOG_ERROR("{}", result.Message);
    }

    return result;
}

void PythonScriptEngine::Shutdown() {
    PrepareShutdown();
    m_impl->Interpreter.reset();
    m_initialized = false;
}

void PythonScriptEngine::PrepareShutdown() {
    if (!m_initialized) {
        return;
    }

    py::gil_scoped_acquire acquire;
    for (auto& [handle, record] : m_impl->Instances) {
        (void)handle;
        if (record.Created && record.Instance) {
            InvokeNoArg(record.Instance, "on_destroy");
        }
        record.Instance = py::object();
        record.Created = false;
    }
    m_impl->Instances.clear();
    m_impl->ModuleNamespaces.clear();
}

void PythonScriptEngine::SetServices(ScriptServices services) {
    m_services = services;
    SetPythonScriptServices(&m_services);
}

void PythonScriptEngine::OnSceneScriptsBegin(Scene& scene) {
    m_services.Scene = &scene;
    SetPythonScriptServices(&m_services);
}

void PythonScriptEngine::OnSceneScriptsEnd(Scene&) {
    m_services.Scene = nullptr;
    SetPythonScriptServices(&m_services);
}

bool PythonScriptEngine::EnsureInstance(Scene& scene, Entity entity, const PythonScriptComponent& component,
                                        AssetManager& assets) {
    if (!m_initialized || !component.Enabled || !component.ScriptAsset.IsValid()) {
        return false;
    }

    const std::uint32_t handle = entity.m_handle;
    const UUID entityId = entity.GetUUID();
    const std::shared_ptr<Scene> scenePtr = scene.shared_from_this();

    py::gil_scoped_acquire acquire;

    InstanceRecord& record = m_impl->Instances[handle];
    const std::size_t generation = m_impl->ReloadGeneration[component.ScriptAsset];
    const bool needsCreate = !record.Created || record.ScriptAsset != component.ScriptAsset ||
                             record.Generation != generation || record.EntityId != entityId;

    if (!needsCreate) {
        return true;
    }

    if (record.Created && record.Instance) {
        InvokeNoArg(record.Instance, "on_destroy");
        record.Instance = py::object();
        record.Created = false;
    }

    const AssetResult loadResult = assets.LoadPythonScript(component.ScriptAsset);
    if (!loadResult.Success) {
        XORA_LOG_WARN("[Python] Failed to load script {} for entity {}", component.ScriptAsset.ToString(),
                      entityId.ToString());
        return false;
    }
    const auto script = assets.GetLoadedPythonScript(component.ScriptAsset);
    if (script == nullptr || !script->IsValid()) {
        return false;
    }

    const std::string moduleKey = component.ScriptAsset.ToString();
    if (!m_impl->ModuleNamespaces.contains(moduleKey)) {
        py::dict moduleNamespace;
        moduleNamespace["__builtins__"] = py::module_::import("builtins");
        m_impl->ModuleNamespaces.emplace(moduleKey, std::move(moduleNamespace));
    }
    py::dict& moduleNamespace = m_impl->ModuleNamespaces.at(moduleKey);

    try {
        py::exec(script->GetSource(), moduleNamespace, moduleNamespace);
    } catch (py::error_already_set& exception) {
        exception.restore();
        XORA_LOG_ERROR("[Python] Failed to compile script for entity {} — {}", entityId.ToString(),
                       FormatPythonError(script->GetRelativePath()));
        return false;
    }

    const std::string className = DefaultClassName(assets, component.ScriptAsset, component);
    if (!moduleNamespace.contains(className.c_str())) {
        XORA_LOG_ERROR("[Python] Class '{}' not found in {}", className, script->GetRelativePath());
        return false;
    }

    try {
        py::object scriptClass = moduleNamespace[className.c_str()];
        record.Instance = scriptClass();
        record.Instance.attr("entity") = CreatePyEntityBinding(scenePtr, entityId);
        ApplyProperties(record.Instance, component);
        record.ScriptAsset = component.ScriptAsset;
        record.Generation = generation;
        record.EntityId = entityId;
        record.Scene = scenePtr;
        InvokeNoArg(record.Instance, "on_create");
        record.Created = true;
        return true;
    } catch (py::error_already_set& exception) {
        exception.restore();
        XORA_LOG_ERROR("[Python] {}", FormatPythonError("on_create failed for " + entityId.ToString()));
        record.Instance = py::object();
        record.Created = false;
        return false;
    }
}

void PythonScriptEngine::DestroyInstance(std::uint32_t entityHandle) {
    if (!m_initialized) {
        m_impl->Instances.erase(entityHandle);
        return;
    }

    const auto found = m_impl->Instances.find(entityHandle);
    if (found == m_impl->Instances.end()) {
        return;
    }

    py::gil_scoped_acquire acquire;
    if (found->second.Created && found->second.Instance) {
        InvokeNoArg(found->second.Instance, "on_destroy");
    }
    m_impl->Instances.erase(found);
}

void PythonScriptEngine::UpdateInstance(Scene& scene, std::uint32_t entityHandle, float deltaSeconds,
                                        const PythonScriptComponent& component) {
    (void)scene;
    if (!m_initialized || !component.Enabled) {
        return;
    }

    const auto found = m_impl->Instances.find(entityHandle);
    if (found == m_impl->Instances.end() || !found->second.Created) {
        return;
    }

    py::gil_scoped_acquire acquire;
    std::string scriptPath = "unknown";
    if (m_services.Assets != nullptr) {
        if (const AssetMetadata* metadata = m_services.Assets->GetRegistry().FindByHandle(found->second.ScriptAsset)) {
            scriptPath = metadata->RelativePath;
        }
    }
    InvokeUpdate(found->second.Instance, deltaSeconds, found->second.EntityId, scriptPath);
}

ScriptReloadResult PythonScriptEngine::ReloadScript(AssetHandle handle, AssetManager& assets) {
    ScriptReloadResult result{};
    if (!m_initialized || !handle.IsValid()) {
        result.Message = "Python runtime unavailable";
        return result;
    }

    const AssetMetadata* metadata = assets.GetRegistry().FindByHandle(handle);
    if (metadata == nullptr || metadata->Type != AssetType::PythonScript) {
        result.Message = "Asset is not a Python script";
        return result;
    }

    const std::filesystem::path absolute = ResolveAssetPath(metadata->RelativePath);
    PythonScriptAsset candidate;
    const ScriptLoadResult loadResult =
        PythonScriptAsset::LoadFromFile(absolute, metadata->RelativePath, candidate);
    if (!loadResult.Success) {
        result.KeptPreviousVersion = true;
        result.Message = loadResult.Message;
        return result;
    }

    py::gil_scoped_acquire acquire;
    py::dict trial;
    trial["__builtins__"] = py::module_::import("builtins");
    try {
        py::exec(candidate.GetSource(), trial, trial);
    } catch (py::error_already_set& exception) {
        exception.restore();
        result.KeptPreviousVersion = true;
        result.Message = FormatPythonError("Reload compile failed for " + metadata->RelativePath);
        XORA_LOG_ERROR("[Python] {}", result.Message);
        return result;
    }

    assets.UnloadAsset(handle);
    (void)assets.LoadPythonScript(handle);
    {
        py::dict fresh;
        fresh["__builtins__"] = py::module_::import("builtins");
        m_impl->ModuleNamespaces[handle.ToString()] = std::move(fresh);
    }
    ++m_impl->ReloadGeneration[handle];

  for (auto& [entityHandle, record] : m_impl->Instances) {
        (void)entityHandle;
        if (record.ScriptAsset == handle && record.Created && record.Instance) {
            InvokeNoArg(record.Instance, "on_destroy");
            record.Instance = py::object();
            record.Created = false;
        }
    }

    result.Success = true;
    result.Message = "Script reloaded";
    return result;
}

} // namespace Xora2D

#endif