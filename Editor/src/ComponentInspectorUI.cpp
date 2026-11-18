#include "Xora2DEditor/ComponentInspectorUI.h"

#include "Xora2DEditor/Content/EditorAssetAssignments.h"
#include "Xora2DEditor/TransformEditing.h"

#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Assets/AssetMetadata.h"
#include "Xora2D/Assets/AssetTypes.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Physics/PhysicsComponents.h"
#include "Xora2D/Physics/PhysicsTypes.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"
#include "Xora2D/Renderer/Texture2D.h"
#include "Xora2D/Scripting/ScriptTypes.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace Xora2DEditor {
namespace {

static bool DrawHeaderWithRemove(const char* title, bool* open) {
    ImGui::PushID(title);
    const bool visible = ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen);
    if (open != nullptr) {
        *open = visible;
    }
    bool remove = false;
    if (visible) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 4.0f);
        if (ImGui::SmallButton("X")) {
            remove = true;
        }
    }
    ImGui::PopID();
    return remove;
}

// Accepts a content-browser drag payload only if the dragged asset is registered, available and of the expected
// type; anything else is rejected without modifying the component.
static bool AcceptAssetHandleDragDrop(Xora2D::AssetHandle& handle, Xora2D::AssetType expectedType,
                                      const Xora2D::AssetRegistry& registry) {
    if (!ImGui::BeginDragDropTarget()) {
        return false;
    }
    bool changed = false;
    if (const ImGuiPayload* peek = ImGui::AcceptDragDropPayload(kAssetDragDropPayload, ImGuiDragDropFlags_AcceptPeekOnly)) {
        AssetDragPayload payload{};
        const bool wellFormed = peek->DataSize == static_cast<int>(sizeof(AssetDragPayload));
        if (wellFormed) {
            std::memcpy(&payload, peek->Data, sizeof(payload));
        }
        const Xora2D::AssetMetadata* meta =
            wellFormed ? registry.FindByHandle(Xora2D::AssetHandle{payload.Handle}) : nullptr;
        const bool compatible = meta != nullptr && meta->Type == expectedType && meta->Available;
        if (compatible && ImGui::AcceptDragDropPayload(kAssetDragDropPayload)) {
            handle = meta->Handle;
            changed = true;
        } else if (!compatible) {
            ImGui::SetTooltip("Incompatible asset");
        }
    }
    ImGui::EndDragDropTarget();
    return changed;
}

static void DrawAssetCombo(const char* label,
                           Xora2D::AssetHandle& handle,
                           Xora2D::AssetType filterType,
                           const Xora2D::AssetRegistry& registry) {
    const auto assets = registry.ListAssets();
    std::vector<const Xora2D::AssetMetadata*> filtered;
    filtered.reserve(assets.size() + 1);
    for (const Xora2D::AssetMetadata& meta : assets) {
        if (meta.Type == filterType && meta.Available) {
            filtered.push_back(&meta);
        }
    }
    std::sort(filtered.begin(), filtered.end(), [](const Xora2D::AssetMetadata* a, const Xora2D::AssetMetadata* b) {
        return a->Name < b->Name;
    });

    int current = -1;
    for (std::size_t i = 0; i < filtered.size(); ++i) {
        if (filtered[i]->Handle == handle) {
            current = static_cast<int>(i);
            break;
        }
    }

    const char* preview = "(None)";
    if (current >= 0) {
        preview = filtered[static_cast<std::size_t>(current)]->Name.c_str();
    } else if (handle.IsValid()) {
        if (const Xora2D::AssetMetadata* meta = registry.FindByHandle(handle); meta != nullptr) {
            preview = meta->Name.c_str();
        } else {
            preview = "(Missing)";
        }
    }

    if (ImGui::BeginCombo(label, preview)) {
        if (ImGui::Selectable("(None)", !handle.IsValid())) {
            handle = {};
        }
        for (std::size_t i = 0; i < filtered.size(); ++i) {
            const bool selected = static_cast<int>(i) == current;
            if (ImGui::Selectable(filtered[i]->Name.c_str(), selected)) {
                handle = filtered[i]->Handle;
            }
        }
        ImGui::EndCombo();
    }
}

static void DrawTransformComponent(Xora2D::Entity entity) {
    auto& transform = entity.GetComponent<Xora2D::TransformComponent>();
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::PushID("Transform");
    // Edit a copy (rotation shown in degrees) and write back only the fields the user changed.
    const TransformEditValues before = ReadTransformForEditing(transform);
    TransformEditValues edited = before;
    ImGui::DragFloat2("Position", &edited.Position.x, 0.5f, 0.0f, 0.0f, "%.2f");
    ImGui::SetItemTooltip("World position in pixels (X, Y). Drag, or double-click to type.");
    ImGui::DragFloat("Rotation (deg)", &edited.RotationDegrees, 0.5f, 0.0f, 0.0f, "%.2f");
    ImGui::SetItemTooltip("Rotation in degrees (positive = counter-clockwise). Stored in radians.");
    ImGui::DragFloat2("Scale", &edited.Scale.x, 0.01f, 0.0001f, 1000.0f, "%.3f");
    ImGui::SetItemTooltip("Scale multiplier (X, Y). Drag, or double-click to type.");
    (void)ApplyTransformEdit(transform, before, edited);
    ImGui::PopID();
}

static void DrawSpriteRenderer(Xora2D::Entity entity, EditorContext& context) {
    if (!entity.HasComponent<Xora2D::SpriteRendererComponent>()) {
        return;
    }
    bool open = true;
    if (DrawHeaderWithRemove("Sprite Renderer", &open) && open) {
        if (RemoveComponentFromEntity(*context.GetScene(), entity, SceneComponentKind::SpriteRenderer) ==
            EditorCommandStatus::Success) {
            return;
        }
    }
    if (!open) {
        return;
    }

    auto& sprite = entity.GetComponent<Xora2D::SpriteRendererComponent>();
    ImGui::PushID("SpriteRenderer");
    ImGui::ColorEdit4("Color", &sprite.Color.x);
    ImGui::Checkbox("Visible", &sprite.Visible);
    ImGui::DragFloat2("Size", &sprite.Size.x, 1.0f, 1.0f, 100000.0f);
    const Xora2D::AssetHandle previous = sprite.TextureAsset;
    DrawAssetCombo("Texture", sprite.TextureAsset, Xora2D::AssetType::Texture2D, context.GetAssetRegistry());
    if (AcceptAssetHandleDragDrop(sprite.TextureAsset, Xora2D::AssetType::Texture2D, context.GetAssetRegistry())) {
        context.GetScene()->ResolveSpriteTextures();
    }
    if (sprite.TextureAsset != previous) {
        context.GetScene()->ResolveSpriteTextures();
    }
    if (sprite.TextureAsset.IsValid()) {
        context.GetAssetManager().LoadTexture(sprite.TextureAsset);
        if (const std::shared_ptr<Xora2D::Texture2D> tex = context.GetAssetManager().GetLoadedTexture(sprite.TextureAsset);
            tex != nullptr && tex->GetRendererId() != 0) {
            const ImVec2 previewSize(64.0f, 64.0f);
            const ImTextureID textureId =
                static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex->GetRendererId()));
            ImGui::Image(textureId, previewSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        }
    }
    ImGui::PopID();
}

static const char* BodyTypeLabel(Xora2D::RigidBodyType2D type) {
    switch (type) {
    case Xora2D::RigidBodyType2D::Static:
        return "Static";
    case Xora2D::RigidBodyType2D::Dynamic:
        return "Dynamic";
    case Xora2D::RigidBodyType2D::Kinematic:
        return "Kinematic";
    }
    return "Unknown";
}

static void DrawRigidBody2D(Xora2D::Entity entity, EditorContext& context) {
    if (!entity.HasComponent<Xora2D::RigidBody2DComponent>()) {
        return;
    }
    bool open = true;
    if (DrawHeaderWithRemove("Rigid Body 2D", &open) && open) {
        if (RemoveComponentFromEntity(*context.GetScene(), entity, SceneComponentKind::RigidBody2D) ==
            EditorCommandStatus::Success) {
            return;
        }
    }
    if (!open) {
        return;
    }

    auto& body = entity.GetComponent<Xora2D::RigidBody2DComponent>();
    ImGui::PushID("RigidBody2D");
    bool physicsTouched = false;
    const int typeIndex = static_cast<int>(body.Type);
    if (ImGui::BeginCombo("Body Type", BodyTypeLabel(body.Type))) {
        for (int i = 0; i < 3; ++i) {
            const auto candidate = static_cast<Xora2D::RigidBodyType2D>(i);
            if (ImGui::Selectable(BodyTypeLabel(candidate), typeIndex == i)) {
                body.Type = candidate;
                physicsTouched = true;
            }
        }
        ImGui::EndCombo();
    }
    physicsTouched |= ImGui::Checkbox("Fixed Rotation", &body.FixedRotation);
    physicsTouched |= ImGui::DragFloat("Gravity Scale", &body.GravityScale, 0.05f, -10.0f, 10.0f);
    physicsTouched |= ImGui::DragFloat("Linear Damping", &body.LinearDamping, 0.01f, 0.0f, 10.0f);
    physicsTouched |= ImGui::DragFloat("Angular Damping", &body.AngularDamping, 0.01f, 0.0f, 10.0f);
    physicsTouched |= ImGui::Checkbox("Allow Sleep", &body.AllowSleep);
    physicsTouched |= ImGui::Checkbox("Bullet", &body.Bullet);
    physicsTouched |= ImGui::Checkbox("Enabled", &body.Enabled);
    physicsTouched |= ImGui::Checkbox("Drive Transform From ECS", &body.DriveTransformFromECS);
    ImGui::SetItemTooltip("When enabled, the ECS transform drives Static/Kinematic bodies before each step.");
    if (physicsTouched) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    ImGui::PopID();
}

static void DrawBoxCollider2D(Xora2D::Entity entity, EditorContext& context) {
    if (!entity.HasComponent<Xora2D::BoxCollider2DComponent>()) {
        return;
    }
    bool open = true;
    if (DrawHeaderWithRemove("Box Collider 2D", &open) && open) {
        if (RemoveComponentFromEntity(*context.GetScene(), entity, SceneComponentKind::BoxCollider2D) ==
            EditorCommandStatus::Success) {
            return;
        }
    }
    if (!open) {
        return;
    }

    auto& box = entity.GetComponent<Xora2D::BoxCollider2DComponent>();
    ImGui::PushID("BoxCollider2D");
    if (ImGui::DragFloat2("Offset", &box.Offset.x, 0.5f)) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    if (ImGui::DragFloat2("Size", &box.Size.x, 0.5f, 0.01f, 100000.0f)) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    if (ImGui::DragFloat("Density", &box.Density, 0.05f, 0.001f, 100.0f)) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    if (ImGui::DragFloat("Friction", &box.Friction, 0.01f, 0.0f, 2.0f)) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    if (ImGui::DragFloat("Restitution", &box.Restitution, 0.01f, 0.0f, 2.0f)) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    if (ImGui::DragFloat("Restitution Threshold", &box.RestitutionThreshold, 0.1f, 0.0f, 100.0f)) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    if (ImGui::Checkbox("Sensor", &box.IsSensor)) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    ImGui::PopID();
}

static void DrawCircleCollider2D(Xora2D::Entity entity, EditorContext& context) {
    if (!entity.HasComponent<Xora2D::CircleCollider2DComponent>()) {
        return;
    }
    bool open = true;
    if (DrawHeaderWithRemove("Circle Collider 2D", &open) && open) {
        if (RemoveComponentFromEntity(*context.GetScene(), entity, SceneComponentKind::CircleCollider2D) ==
            EditorCommandStatus::Success) {
            return;
        }
    }
    if (!open) {
        return;
    }

    auto& circle = entity.GetComponent<Xora2D::CircleCollider2DComponent>();
    ImGui::PushID("CircleCollider2D");
    if (ImGui::DragFloat2("Offset", &circle.Offset.x, 0.5f)) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    if (ImGui::DragFloat("Radius", &circle.Radius, 0.5f, 0.01f, 100000.0f)) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    if (ImGui::DragFloat("Density", &circle.Density, 0.05f, 0.001f, 100.0f)) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    if (ImGui::DragFloat("Friction", &circle.Friction, 0.01f, 0.0f, 2.0f)) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    if (ImGui::DragFloat("Restitution", &circle.Restitution, 0.01f, 0.0f, 2.0f)) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    if (ImGui::DragFloat("Restitution Threshold", &circle.RestitutionThreshold, 0.1f, 0.0f, 100.0f)) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    if (ImGui::Checkbox("Sensor", &circle.IsSensor)) {
        SyncPhysicsWithScene(*context.GetScene());
    }
    ImGui::PopID();
}

static void DrawCamera(Xora2D::Entity entity, EditorContext& context) {
    if (!entity.HasComponent<Xora2D::CameraComponent>()) {
        return;
    }
    bool open = true;
    if (DrawHeaderWithRemove("Camera", &open) && open) {
        if (RemoveComponentFromEntity(*context.GetScene(), entity, SceneComponentKind::Camera) ==
            EditorCommandStatus::Success) {
            return;
        }
    }
    if (!open) {
        return;
    }

    auto& camera = entity.GetComponent<Xora2D::CameraComponent>();
    ImGui::PushID("Camera");
    ImGui::Checkbox("Enabled", &camera.Enabled);
    if (ImGui::Checkbox("Primary", &camera.Primary) && camera.Primary) {
        EnsureSinglePrimaryCamera(*context.GetScene(), entity.GetUUID());
    }
    ImGui::DragFloat("Zoom", &camera.Zoom, 0.01f, 0.01f, 10.0f);
    ImGui::SetItemTooltip("Orthographic zoom factor for the primary camera.");
    ImGui::PopID();
}

static const char* AudioGroupLabel(Xora2D::AudioGroup group) {
    switch (group) {
    case Xora2D::AudioGroup::Music:
        return "Music";
    case Xora2D::AudioGroup::SoundEffects:
        return "Sound Effects";
    }
    return "Unknown";
}

static void DrawAudioSource(Xora2D::Entity entity, EditorContext& context) {
    if (!entity.HasComponent<Xora2D::AudioSourceComponent>()) {
        return;
    }
    bool open = true;
    if (DrawHeaderWithRemove("Audio Source", &open) && open) {
        if (RemoveComponentFromEntity(*context.GetScene(), entity, SceneComponentKind::AudioSource) ==
            EditorCommandStatus::Success) {
            return;
        }
    }
    if (!open) {
        return;
    }

    auto& audio = entity.GetComponent<Xora2D::AudioSourceComponent>();
    ImGui::PushID("AudioSource");
    DrawAssetCombo("Clip", audio.ClipAsset, Xora2D::AssetType::Audio, context.GetAssetRegistry());
    (void)AcceptAssetHandleDragDrop(audio.ClipAsset, Xora2D::AssetType::Audio, context.GetAssetRegistry());
    ImGui::DragFloat("Volume", &audio.Volume, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Pitch", &audio.Pitch, 0.01f, 0.1f, 4.0f);
    ImGui::Checkbox("Loop", &audio.Loop);
    ImGui::Checkbox("Play On Start", &audio.PlayOnStart);
    ImGui::Checkbox("Muted", &audio.Muted);
    const int groupIndex = static_cast<int>(audio.Group);
    if (ImGui::BeginCombo("Group", AudioGroupLabel(audio.Group))) {
        for (int i = 0; i < 2; ++i) {
            const auto group = static_cast<Xora2D::AudioGroup>(i);
            if (ImGui::Selectable(AudioGroupLabel(group), groupIndex == i)) {
                audio.Group = group;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Spatial Audio", &audio.SpatialAudio);
    ImGui::PopID();
}

static void DrawAudioListener(Xora2D::Entity entity, EditorContext& context) {
    if (!entity.HasComponent<Xora2D::AudioListenerComponent>()) {
        return;
    }
    bool open = true;
    if (DrawHeaderWithRemove("Audio Listener", &open) && open) {
        if (RemoveComponentFromEntity(*context.GetScene(), entity, SceneComponentKind::AudioListener) ==
            EditorCommandStatus::Success) {
            return;
        }
    }
    if (!open) {
        return;
    }

    auto& listener = entity.GetComponent<Xora2D::AudioListenerComponent>();
    ImGui::PushID("AudioListener");
    ImGui::Checkbox("Active", &listener.Active);
    ImGui::PopID();
}

#if XORA2D_SCRIPTING_ENABLED
static void DrawPythonScript(Xora2D::Entity entity, EditorContext& context) {
    if (!entity.HasComponent<Xora2D::PythonScriptComponent>()) {
        return;
    }
    bool open = true;
    if (DrawHeaderWithRemove("Python Script", &open) && open) {
        if (RemoveComponentFromEntity(*context.GetScene(), entity, SceneComponentKind::PythonScript) ==
            EditorCommandStatus::Success) {
            return;
        }
    }
    if (!open) {
        return;
    }

    auto& script = entity.GetComponent<Xora2D::PythonScriptComponent>();
    ImGui::PushID("PythonScript");
    DrawAssetCombo("Script Asset", script.ScriptAsset, Xora2D::AssetType::PythonScript, context.GetAssetRegistry());
    (void)AcceptAssetHandleDragDrop(script.ScriptAsset, Xora2D::AssetType::PythonScript, context.GetAssetRegistry());
    char className[256];
    std::memset(className, 0, sizeof(className));
    std::strncpy(className, script.ClassName.c_str(), sizeof(className) - 1);
    if (ImGui::InputText("Class Name", className, sizeof(className))) {
        script.ClassName = className;
    }
    ImGui::Checkbox("Enabled", &script.Enabled);

    for (std::size_t i = 0; i < script.Properties.size(); ++i) {
        Xora2D::ScriptProperty& prop = script.Properties[i];
        ImGui::PushID(static_cast<int>(i));
        switch (prop.Type) {
        case Xora2D::ScriptPropertyType::Number:
            ImGui::InputDouble(prop.Name.c_str(), &prop.NumberValue, 0.1, 1.0, "%.3f");
            break;
        case Xora2D::ScriptPropertyType::Boolean:
            ImGui::Checkbox(prop.Name.c_str(), &prop.BooleanValue);
            break;
        case Xora2D::ScriptPropertyType::String: {
            char buffer[256];
            std::memset(buffer, 0, sizeof(buffer));
            std::strncpy(buffer, prop.StringValue.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText(prop.Name.c_str(), buffer, sizeof(buffer))) {
                prop.StringValue = buffer;
            }
            break;
        }
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}
#endif

static void DrawAnimation(Xora2D::Entity entity, EditorContext& context) {
    if (!entity.HasComponent<Xora2D::AnimationComponent>()) {
        return;
    }
    bool open = true;
    if (DrawHeaderWithRemove("Animation", &open) && open) {
        if (RemoveComponentFromEntity(*context.GetScene(), entity, SceneComponentKind::Animation) ==
            EditorCommandStatus::Success) {
            return;
        }
    }
    if (!open) {
        return;
    }

    auto& anim = entity.GetComponent<Xora2D::AnimationComponent>();
    ImGui::PushID("Animation");
    DrawAssetCombo("Default Clip", anim.DefaultClip, Xora2D::AssetType::AnimationClip, context.GetAssetRegistry());
    if (AcceptAssetHandleDragDrop(anim.DefaultClip, Xora2D::AssetType::AnimationClip, context.GetAssetRegistry()) &&
        std::find(anim.ClipAssets.begin(), anim.ClipAssets.end(), anim.DefaultClip) == anim.ClipAssets.end()) {
        anim.ClipAssets.push_back(anim.DefaultClip);
    }
    char defaultClipName[256];
    std::memset(defaultClipName, 0, sizeof(defaultClipName));
    std::strncpy(defaultClipName, anim.DefaultClipName.c_str(), sizeof(defaultClipName) - 1);
    if (ImGui::InputText("Default Clip Name", defaultClipName, sizeof(defaultClipName))) {
        anim.DefaultClipName = defaultClipName;
    }
    ImGui::DragFloat("Playback Speed", &anim.PlaybackSpeed, 0.05f, 0.0f, 10.0f);
    ImGui::Checkbox("Looping", &anim.Looping);
    ImGui::Checkbox("Autoplay", &anim.Autoplay);
    ImGui::PopID();
}

static void DrawAddComponentEntry(const char* label, SceneComponentKind kind, Xora2D::Entity entity, EditorContext& context) {
    const bool canAdd = CanAddComponent(entity, kind);
    if (!canAdd) {
        ImGui::BeginDisabled();
    }
    if (ImGui::MenuItem(label, nullptr, false, canAdd)) {
        (void)AddComponentToEntity(*context.GetScene(), entity, kind);
    }
    if (!canAdd) {
        ImGui::EndDisabled();
    }
}

} // namespace

void DrawEntityInspector(Xora2D::Entity entity, EditorContext& context) {
    if (!entity.IsValid()) {
        return;
    }

    // Scope every widget ID to this entity so per-widget ImGui state (active drag, text-edit buffer) can never carry
    // over to a different entity that happens to use the same labels.
    ImGui::PushID(entity.GetUUID().ToString().c_str());

    char nameBuffer[256];
    std::memset(nameBuffer, 0, sizeof(nameBuffer));
    const std::string& currentName = entity.GetName();
    std::strncpy(nameBuffer, currentName.c_str(), sizeof(nameBuffer) - 1);
    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
        (void)RenameEntity(entity, nameBuffer);
    }

    char uuidBuffer[64];
    std::memset(uuidBuffer, 0, sizeof(uuidBuffer));
    const std::string uuidText = entity.GetUUID().ToString();
    std::strncpy(uuidBuffer, uuidText.c_str(), sizeof(uuidBuffer) - 1);
    ImGui::InputText("UUID", uuidBuffer, sizeof(uuidBuffer), ImGuiInputTextFlags_ReadOnly);

    ImGui::Separator();

    DrawTransformComponent(entity);
    DrawSpriteRenderer(entity, context);
    DrawCamera(entity, context);
    DrawRigidBody2D(entity, context);
    DrawBoxCollider2D(entity, context);
    DrawCircleCollider2D(entity, context);
    DrawAudioSource(entity, context);
    DrawAudioListener(entity, context);
#if XORA2D_SCRIPTING_ENABLED
    DrawPythonScript(entity, context);
#endif
    DrawAnimation(entity, context);

    ImGui::Spacing();
    DrawAddComponentMenu(entity, context);
    ImGui::PopID();
}

void DrawAddComponentMenu(Xora2D::Entity entity, EditorContext& context) {
    if (!entity.IsValid()) {
        return;
    }
    ImGui::PushID(entity.GetUUID().ToString().c_str());
    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    if (ImGui::BeginPopup("AddComponentPopup")) {
        DrawAddComponentEntry("Sprite Renderer", SceneComponentKind::SpriteRenderer, entity, context);
        DrawAddComponentEntry("Camera", SceneComponentKind::Camera, entity, context);
        DrawAddComponentEntry("Rigid Body 2D", SceneComponentKind::RigidBody2D, entity, context);
        DrawAddComponentEntry("Box Collider 2D", SceneComponentKind::BoxCollider2D, entity, context);
        DrawAddComponentEntry("Circle Collider 2D", SceneComponentKind::CircleCollider2D, entity, context);
        DrawAddComponentEntry("Audio Source", SceneComponentKind::AudioSource, entity, context);
        DrawAddComponentEntry("Audio Listener", SceneComponentKind::AudioListener, entity, context);
#if XORA2D_SCRIPTING_ENABLED
        DrawAddComponentEntry("Python Script", SceneComponentKind::PythonScript, entity, context);
#endif
        DrawAddComponentEntry("Animation", SceneComponentKind::Animation, entity, context);
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

} // namespace Xora2DEditor