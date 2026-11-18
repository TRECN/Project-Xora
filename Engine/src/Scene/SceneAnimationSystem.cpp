#include "SceneAnimationSystem.h"

#include "../Scene/SceneRegistry.h"

#include "Xora2D/Animation/AnimationClipAsset.h"
#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Animation/AnimationEvents.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Core/UUID.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"

#include <vector>

namespace Xora2D {
namespace {

void DispatchEvent(const std::function<void(const AnimationEvent&)>& callback, const AnimationEvent& event) {
    if (callback) {
        callback(event);
    }
}

const AnimationClip* ResolveClip(const AnimationComponent& component, AssetManager& assets, AssetHandle handle) {
    if (!handle.IsValid()) {
        return nullptr;
    }
    const AssetResult loaded = assets.LoadAnimationClip(handle);
    if (!loaded.Success) {
        return nullptr;
    }
    const std::shared_ptr<const AnimationClipAsset> clipAsset = assets.GetLoadedAnimationClip(handle);
    if (clipAsset == nullptr || !clipAsset->IsValid()) {
        return nullptr;
    }
    return &clipAsset->GetClip();
}

void EnsurePlaybackInitialized(AnimationComponent& animation, AssetManager& assets) {
    if (animation.ActiveClip.IsValid()) {
        return;
    }
    if (!animation.DefaultClip.IsValid() && !animation.ClipAssets.empty()) {
        animation.DefaultClip = animation.ClipAssets.front();
    }
    if (!animation.DefaultClip.IsValid()) {
        return;
    }
    (void)assets.LoadAnimationClip(animation.DefaultClip);
    const AnimationClip* clip = ResolveClip(animation, assets, animation.DefaultClip);
    if (clip == nullptr) {
        return;
    }
    animation.ActiveClip = animation.DefaultClip;
    animation.Controller.AssignClip(clip);
    animation.Controller.SetLooping(animation.Looping);
    animation.Controller.SetPlaybackSpeed(animation.PlaybackSpeed);
    if (animation.Autoplay) {
        animation.Controller.Play();
        animation.PendingStartEvent = true;
    }
}

} // namespace

void SceneAnimationSystem::OnSceneUpdate(Scene& scene, float deltaSeconds) {
    if (m_assetManager == nullptr) {
        return;
    }

    auto& registry = scene.m_registry->Registry;
    std::vector<std::uint32_t> animatedEntities;
    animatedEntities.reserve(32);
    for (const auto entity : registry.view<AnimationComponent>()) {
        animatedEntities.push_back(static_cast<std::uint32_t>(entt::to_integral(entity)));
    }

    for (const std::uint32_t handle : animatedEntities) {
        if (!scene.m_registry->Valid(handle)) {
            continue;
        }
        Entity entity(&scene, handle);
        if (!entity.IsValid()) {
            continue;
        }

        auto& animation = scene.m_registry->Get<AnimationComponent>(handle);
        EnsurePlaybackInitialized(animation, *m_assetManager);

        if (animation.PendingStartEvent) {
            animation.PendingStartEvent = false;
            AnimationEvent event{};
            event.Type = AnimationEventType::Started;
            event.EntityId = entity.GetUUID();
            const AnimationClip* clip = animation.Controller.GetCurrentFrame() != nullptr ? ResolveClip(
                                            animation, *m_assetManager, animation.ActiveClip) : nullptr;
            event.ClipName = clip != nullptr ? clip->Name : "";
            DispatchEvent(m_eventCallback, event);
        }

        bool frameChanged = false;
        bool started = false;
        bool markerFired = false;
        std::string marker;
        const bool completed = animation.Controller.Update(deltaSeconds, frameChanged, started, markerFired, marker);
        (void)started;
        animation.State = animation.Controller.GetState();
        animation.CurrentFrameIndex = animation.Controller.GetCurrentFrameIndex();

        const AnimationFrame* frame = animation.Controller.GetCurrentFrame();
        if (frame != nullptr) {
            animation.FrameUV = frame->UVRect;
            animation.FrameUVValid = true;
        }

        if (frameChanged) {
            AnimationEvent event{};
            event.Type = AnimationEventType::FrameChanged;
            event.EntityId = entity.GetUUID();
            const AnimationClip* clip = ResolveClip(animation, *m_assetManager, animation.ActiveClip);
            event.ClipName = clip != nullptr ? clip->Name : "";
            event.FrameIndex = animation.CurrentFrameIndex;
            DispatchEvent(m_eventCallback, event);
        }

        if (markerFired) {
            AnimationEvent event{};
            event.Type = AnimationEventType::FrameMarker;
            event.EntityId = entity.GetUUID();
            const AnimationClip* clip = ResolveClip(animation, *m_assetManager, animation.ActiveClip);
            event.ClipName = clip != nullptr ? clip->Name : "";
            event.FrameIndex = animation.CurrentFrameIndex;
            event.Marker = marker;
            DispatchEvent(m_eventCallback, event);
        }

        if (completed) {
            AnimationEvent event{};
            event.Type = AnimationEventType::Completed;
            event.EntityId = entity.GetUUID();
            const AnimationClip* clip = ResolveClip(animation, *m_assetManager, animation.ActiveClip);
            event.ClipName = clip != nullptr ? clip->Name : "";
            DispatchEvent(m_eventCallback, event);
        }
    }
}

void SceneAnimationSystem::OnEntityDestroyed(std::uint32_t /*entityHandle*/) {}

void SceneAnimationSystem::Clear() { PrepareForShutdown(); }

void SceneAnimationSystem::PrepareForShutdown() {}

} // namespace Xora2D