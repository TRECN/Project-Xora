#pragma once

#include "Xora2D/Animation/AnimationTypes.h"
#include "Xora2D/Assets/AssetHandle.h"

#include <string>

namespace Xora2D {

class AssetManager;
class Entity;

bool AnimationPlay(Entity entity, AssetManager& assets, AssetHandle clipHandle);
bool AnimationPlayByName(Entity entity, AssetManager& assets, const std::string& clipName);
void AnimationPause(Entity entity);
void AnimationResume(Entity entity);
void AnimationStop(Entity entity);
void AnimationRestart(Entity entity);

[[nodiscard]] std::string AnimationGetCurrentClipName(Entity entity, AssetManager& assets);
[[nodiscard]] int AnimationGetCurrentFrame(Entity entity);
[[nodiscard]] AnimationPlaybackState AnimationGetPlaybackState(Entity entity);
[[nodiscard]] bool AnimationIsPlaying(Entity entity);

} // namespace Xora2D