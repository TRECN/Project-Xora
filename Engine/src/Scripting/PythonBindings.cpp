#if XORA2D_SCRIPTING_ENABLED

#include "PythonBindingsInternal.h"

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Audio/AudioEngine.h"
#include "Xora2D/Audio/AudioTypes.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Input/Input.h"
#include "Xora2D/Input/InputCodes.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Physics/PhysicsWorld2D.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"

#include <glm/vec2.hpp>

#include <pybind11/embed.h>
#include <pybind11/operators.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace Xora2D {
namespace {

ScriptServices* g_scriptServices = nullptr;

std::shared_ptr<Scene> LockScene(const std::weak_ptr<Scene>& scene) {
    return scene.lock();
}

bool ResolveEntity(const std::weak_ptr<Scene>& sceneWeak, const UUID& entityId, Entity& outEntity) {
    const std::shared_ptr<Scene> scene = sceneWeak.lock();
    if (scene == nullptr) {
        return false;
    }
    outEntity = scene->FindEntityByUUID(entityId);
    return outEntity.IsValid();
}

class ScriptEntityRef {
public:
    ScriptEntityRef(std::weak_ptr<Scene> scene, UUID entityId)
        : m_scene(std::move(scene)), m_entityId(entityId), m_transform(this), m_physics(this) {}

    ScriptEntityRef(const ScriptEntityRef& other)
        : m_scene(other.m_scene), m_entityId(other.m_entityId), m_transform(this), m_physics(this) {}

    ScriptEntityRef(ScriptEntityRef&& other) noexcept
        : m_scene(std::move(other.m_scene)), m_entityId(other.m_entityId), m_transform(this), m_physics(this) {}

    ScriptEntityRef& operator=(const ScriptEntityRef&) = delete;
    ScriptEntityRef& operator=(ScriptEntityRef&&) = delete;

    [[nodiscard]] UUID GetUuid() const { return m_entityId; }

    [[nodiscard]] std::string GetName() const {
        Entity entity;
        if (!ResolveEntity(m_scene, m_entityId, entity)) {
            return {};
        }
        return entity.GetName();
    }

    [[nodiscard]] bool IsValid() const {
        Entity entity;
        return ResolveEntity(m_scene, m_entityId, entity);
    }

    struct ScriptVec2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    class TransformBinding {
    public:
        explicit TransformBinding(ScriptEntityRef* owner)
            : m_owner(owner) {}

        ScriptVec2 GetPosition() const {
            Entity entity;
            ScriptVec2 value{};
            if (!m_owner->Resolve(entity)) {
                return value;
            }
            const auto& transform = entity.GetComponent<TransformComponent>();
            value.x = transform.Translation.x;
            value.y = transform.Translation.y;
            return value;
        }

        float GetRotation() const {
            Entity entity;
            if (!m_owner->Resolve(entity)) {
                return 0.0f;
            }
            return entity.GetComponent<TransformComponent>().RotationRadians;
        }

        ScriptVec2 GetScale() const {
            Entity entity;
            ScriptVec2 value{1.0f, 1.0f};
            if (!m_owner->Resolve(entity)) {
                return value;
            }
            const auto& transform = entity.GetComponent<TransformComponent>();
            value.x = transform.Scale.x;
            value.y = transform.Scale.y;
            return value;
        }

        void SetPosition(float x, float y) {
            Entity entity;
            if (!m_owner->Resolve(entity)) {
                return;
            }
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.Translation = {x, y};
            transform.RecalculateLocalMatrix();
        }

        void SetRotation(float radians) {
            Entity entity;
            if (!m_owner->Resolve(entity)) {
                return;
            }
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.RotationRadians = radians;
            transform.RecalculateLocalMatrix();
        }

        void SetScale(float x, float y) {
            Entity entity;
            if (!m_owner->Resolve(entity)) {
                return;
            }
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.Scale = {x, y};
            transform.RecalculateLocalMatrix();
        }

    private:
        ScriptEntityRef* m_owner = nullptr;
    };

    class PhysicsBinding {
    public:
        explicit PhysicsBinding(ScriptEntityRef* owner)
            : m_owner(owner) {}

        void ApplyLinearImpulse(float x, float y) {
            Entity entity;
            if (!m_owner->Resolve(entity) || g_scriptServices == nullptr || g_scriptServices->Scene == nullptr) {
                return;
            }
            g_scriptServices->Scene->GetPhysicsWorld().ApplyLinearImpulse(entity, {x, y});
        }

        void SetLinearVelocity(float x, float y) {
            Entity entity;
            if (!m_owner->Resolve(entity) || g_scriptServices == nullptr || g_scriptServices->Scene == nullptr) {
                return;
            }
            g_scriptServices->Scene->GetPhysicsWorld().SetLinearVelocity(entity, {x, y});
        }

        py::tuple GetLinearVelocity() const {
            Entity entity;
            if (!m_owner->Resolve(entity) || g_scriptServices == nullptr || g_scriptServices->Scene == nullptr) {
                return py::make_tuple(0.0, 0.0);
            }
            const glm::vec2 velocity =
                g_scriptServices->Scene->GetPhysicsWorld().GetLinearVelocity(entity);
            return py::make_tuple(velocity.x, velocity.y);
        }

        void SetGravityScale(float value) {
            Entity entity;
            if (!m_owner->Resolve(entity) || g_scriptServices == nullptr || g_scriptServices->Scene == nullptr) {
                return;
            }
            g_scriptServices->Scene->GetPhysicsWorld().SetGravityScale(entity, value);
        }

    private:
        ScriptEntityRef* m_owner = nullptr;
    };

    [[nodiscard]] TransformBinding& Transform() { return m_transform; }
    [[nodiscard]] PhysicsBinding& Physics() { return m_physics; }

private:
    [[nodiscard]] bool Resolve(Entity& entity) const { return ResolveEntity(m_scene, m_entityId, entity); }

    std::weak_ptr<Scene> m_scene;
    UUID m_entityId;
    TransformBinding m_transform;
    PhysicsBinding m_physics;
};

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;
};

} // namespace

void SetPythonScriptServices(ScriptServices* services) {
    g_scriptServices = services;
}

PYBIND11_EMBEDDED_MODULE(xora2d, module) {
    py::class_<ScriptEntityRef::ScriptVec2>(module, "Vec2")
        .def(py::init<float, float>(), py::arg("x") = 0.0f, py::arg("y") = 0.0f)
        .def_readwrite("x", &ScriptEntityRef::ScriptVec2::x)
        .def_readwrite("y", &ScriptEntityRef::ScriptVec2::y);

    py::class_<Vector2>(module, "Vector2")
        .def(py::init<float, float>(), py::arg("x") = 0.0f, py::arg("y") = 0.0f)
        .def_readwrite("x", &Vector2::x)
        .def_readwrite("y", &Vector2::y);

    py::enum_<Key>(module, "Key")
        .value("Unknown", Key::Unknown)
        .value("A", Key::A)
        .value("D", Key::D)
        .value("W", Key::W)
        .value("S", Key::S)
        .value("Space", Key::Space)
        .value("Escape", Key::Escape)
        .value("Left", Key::Left)
        .value("Right", Key::Right)
        .value("Up", Key::Up)
        .value("Down", Key::Down)
        .value("Digit1", Key::Digit1)
        .value("Digit2", Key::Digit2)
        .value("Digit3", Key::Digit3)
        .value("Digit4", Key::Digit4)
        .export_values();

    py::class_<ScriptEntityRef::TransformBinding>(module, "Transform")
        .def_property_readonly("position", &ScriptEntityRef::TransformBinding::GetPosition)
        .def_property_readonly("scale", &ScriptEntityRef::TransformBinding::GetScale)
        .def_property("rotation", &ScriptEntityRef::TransformBinding::GetRotation,
                      &ScriptEntityRef::TransformBinding::SetRotation)
        .def("set_position", &ScriptEntityRef::TransformBinding::SetPosition)
        .def("set_rotation", &ScriptEntityRef::TransformBinding::SetRotation)
        .def("set_scale", &ScriptEntityRef::TransformBinding::SetScale);

    py::class_<ScriptEntityRef::PhysicsBinding>(module, "Physics")
        .def("apply_linear_impulse", &ScriptEntityRef::PhysicsBinding::ApplyLinearImpulse)
        .def("set_linear_velocity", &ScriptEntityRef::PhysicsBinding::SetLinearVelocity)
        .def("get_linear_velocity", &ScriptEntityRef::PhysicsBinding::GetLinearVelocity)
        .def("set_gravity_scale", &ScriptEntityRef::PhysicsBinding::SetGravityScale);

    py::class_<ScriptEntityRef>(module, "Entity")
        .def_property_readonly("uuid",
                              [](const ScriptEntityRef& self) { return self.GetUuid().ToString(); })
        .def_property_readonly("name", &ScriptEntityRef::GetName)
        .def("is_valid", &ScriptEntityRef::IsValid)
        .def_property_readonly("transform", [](ScriptEntityRef& entity) -> ScriptEntityRef::TransformBinding& {
            return entity.Transform();
        })
        .def_property_readonly("physics", [](ScriptEntityRef& entity) -> ScriptEntityRef::PhysicsBinding& {
            return entity.Physics();
        });

    py::module input = module.def_submodule("Input", "Input state for the active frame");
    input.def("is_key_down", [](Key key) { return Input::IsKeyDown(key); });
    input.def("is_key_pressed", [](Key key) { return Input::IsKeyPressed(key); });
    input.def("is_key_released", [](Key key) { return Input::IsKeyReleased(key); });
    input.def("is_mouse_button_down",
              [](int button) { return Input::IsMouseButtonDown(static_cast<MouseButton>(button)); });
    input.def("get_mouse_position", []() {
        const glm::vec2 position = Input::GetMousePosition();
        return py::make_tuple(position.x, position.y);
    });

    py::module logModule = module.def_submodule("Log", "Engine logging");
    logModule.def("info", [](const std::string& message) { XORA_LOG_INFO("[Python] {}", message); });
    logModule.def("warn", [](const std::string& message) { XORA_LOG_WARN("[Python] {}", message); });
    logModule.def("error", [](const std::string& message) { XORA_LOG_ERROR("[Python] {}", message); });

    py::module audio = module.def_submodule("Audio", "Gameplay audio playback");
    audio.def("play", [](const std::string& relativePath) {
        if (g_scriptServices == nullptr || g_scriptServices->Assets == nullptr ||
            g_scriptServices->Audio == nullptr) {
            return;
        }
        std::string normalized = relativePath;
        if (normalized.rfind("Assets/", 0) == 0) {
            normalized = normalized.substr(7);
        }
        const std::optional<std::string> assetRelative = NormalizeAssetRelativePath(normalized);
        if (!assetRelative.has_value()) {
            XORA_LOG_WARN("[Python] Invalid audio path '{}'", relativePath);
            return;
        }
        AssetManager& assets = *g_scriptServices->Assets;
        const AssetResult registration = assets.RegisterAudioAtRelativePath(*assetRelative);
        if (!registration.Success && registration.Handle.IsValid()) {
            // already registered path attempt
        }
        AssetHandle handle = registration.Handle;
        if (!handle.IsValid()) {
            if (const AssetMetadata* existing = assets.GetRegistry().FindByRelativePath(*assetRelative)) {
                handle = existing->Handle;
            }
        }
        if (!handle.IsValid()) {
            XORA_LOG_WARN("[Python] Audio asset not found for '{}'", relativePath);
            return;
        }
        (void)assets.LoadAudio(handle);
        if (auto clip = assets.GetLoadedAudio(handle)) {
            (void)g_scriptServices->Audio->PlayClip(clip, AudioGroup::SoundEffects, 1.0f, 1.0f, false);
        }
    });

}

py::object CreatePyEntityBinding(const std::shared_ptr<Scene>& scene, const UUID& entityId) {
    return py::cast(ScriptEntityRef(scene, entityId));
}

} // namespace Xora2D

#else

#include "PythonBindingsInternal.h"

namespace Xora2D {

void SetPythonScriptServices(ScriptServices* /*services*/) {}

} // namespace Xora2D

#endif