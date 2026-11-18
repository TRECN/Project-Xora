#include "Xora2D/Animation/AnimationPlayback.h"

#include "Xora2D/Animation/AnimationClipAsset.h"
#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Scene/Entity.h"

namespace Xora2D {
namespace {

AnimationComponent* GetAnimationComponent(Entity entity) {
    if (!entity.IsValid() || !entity.HasComponent<AnimationComponent>()) {
        return nullptr;
    }
    return &entity.GetComponent<AnimationComponent>();
}

const AnimationClip* LoadClip(AssetManager& assets, AssetHandle handle) {
    if (!handle.IsValid()) {
        return nullptr;
    }
    (void)assets.LoadAnimationClip(handle);
    const std::shared_ptr<const AnimationClipAsset> asset = assets.GetLoadedAnimationClip(handle);
    if (asset == nullptr || !asset->IsValid()) {
        return nullptr;
    }
    return &asset->GetClip();
}

AssetHandle FindClipHandleByName(const AnimationComponent& animation, AssetManager& assets,
                                 const std::string& clipName) {
    for (const AssetHandle handle : animation.ClipAssets) {
        const AnimationClip* clip = LoadClip(assets, handle);
        if (clip != nullptr && clip->Name == clipName) {
            return handle;
        }
    }
    if (!animation.DefaultClipName.empty()) {
        for (const AssetHandle handle : animation.ClipAssets) {
            const AnimationClip* clip = LoadClip(assets, handle);
            if (clip != nullptr && clip->Name == animation.DefaultClipName) {
                return handle;
            }
        }
    }
    (void)clipName;
    return {};
}

} // namespace

bool AnimationPlay(Entity entity, AssetManager& assets, AssetHandle clipHandle) {
    AnimationComponent* animation = GetAnimationComponent(entity);
    if (animation == nullptr || !clipHandle.IsValid()) {
        return false;
    }
    const AnimationClip* clip = LoadClip(assets, clipHandle);
    if (clip == nullptr) {
        return false;
    }
    animation->ActiveClip = clipHandle;
    animation->Controller.AssignClip(clip);
    animation->Controller.SetLooping(animation->Looping);
    animation->Controller.SetPlaybackSpeed(animation->PlaybackSpeed);
    animation->Controller.Play();
    animation->State = animation->Controller.GetState();
    animation->PendingStartEvent = true;
    return true;
}

bool AnimationPlayByName(Entity entity, AssetManager& assets, const std::string& clipName) {
    AnimationComponent* animation = GetAnimationComponent(entity);
    if (animation == nullptr) {
        return false;
    }
    const AssetHandle handle = FindClipHandleByName(*animation, assets, clipName);
    if (!handle.IsValid()) {
        return false;
    }
    return AnimationPlay(entity, assets, handle);
}

void AnimationPause(Entity entity) {
    if (AnimationComponent* animation = GetAnimationComponent(entity)) {
        animation->Controller.Pause();
        animation->State = animation->Controller.GetState();
    }
}

void AnimationResume(Entity entity) {
    if (AnimationComponent* animation = GetAnimationComponent(entity)) {
        animation->Controller.Resume();
        animation->State = animation->Controller.GetState();
    }
}

void AnimationStop(Entity entity) {
    if (AnimationComponent* animation = GetAnimationComponent(entity)) {
        animation->Controller.Stop();
        animation->State = animation->Controller.GetState();
        animation->CurrentFrameIndex = 0;
        animation->FrameUVValid = false;
    }
}

void AnimationRestart(Entity entity) {
    if (AnimationComponent* animation = GetAnimationComponent(entity)) {
        animation->Controller.Restart();
        animation->State = animation->Controller.GetState();
        animation->PendingStartEvent = true;
    }
}

std::string AnimationGetCurrentClipName(Entity entity, AssetManager& assets) {
    const AnimationComponent* animation = GetAnimationComponent(entity);
    if (animation == nullptr || !animation->ActiveClip.IsValid()) {
        return {};
    }
    const AnimationClip* clip = LoadClip(assets, animation->ActiveClip);
    return clip != nullptr ? clip->Name : std::string{};
}

int AnimationGetCurrentFrame(Entity entity) {
    const AnimationComponent* animation = GetAnimationComponent(entity);
    return animation != nullptr ? animation->CurrentFrameIndex : 0;
}

AnimationPlaybackState AnimationGetPlaybackState(Entity entity) {
    const AnimationComponent* animation = GetAnimationComponent(entity);
    return animation != nullptr ? animation->State : AnimationPlaybackState::Stopped;
}

bool AnimationIsPlaying(Entity entity) {
    const AnimationComponent* animation = GetAnimationComponent(entity);
    return animation != nullptr && animation->State == AnimationPlaybackState::Playing;
}

} // namespace Xora2D