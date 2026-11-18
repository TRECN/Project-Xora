#include "SceneAudioSystem.h"

#include "SceneRegistry.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetTypes.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Audio/AudioEngine.h"
#include "Xora2D/Audio/AudioClip.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"

namespace Xora2D {

void SceneAudioSystem::PrepareForShutdown() {
    if (m_audioEngine != nullptr) {
        for (auto& [handle, voice] : m_entityVoices) {
            (void)handle;
            if (voice.PlaybackId != kInvalidAudioPlaybackId) {
                m_audioEngine->StopPlayback(voice.PlaybackId);
            }
        }
    }
    m_entityVoices.clear();
}

void SceneAudioSystem::Clear() {
    PrepareForShutdown();
}

void SceneAudioSystem::StopEntityPlayback(std::uint32_t entityHandle) {
    const auto found = m_entityVoices.find(entityHandle);
    if (found == m_entityVoices.end()) {
        return;
    }
    if (m_audioEngine != nullptr && found->second.PlaybackId != kInvalidAudioPlaybackId) {
        m_audioEngine->StopPlayback(found->second.PlaybackId);
    }
    found->second.PlaybackId = kInvalidAudioPlaybackId;
}

void SceneAudioSystem::StopEntityPlaybacksInGroup(Scene& scene, AudioGroup group) {
    if (m_audioEngine != nullptr) {
        m_audioEngine->StopPlaybacksInGroup(group);
    }

    auto& registry = scene.m_registry->Registry;
    for (const auto entity : registry.view<AudioSourceComponent>()) {
        const auto& source = registry.get<AudioSourceComponent>(entity);
        if (source.Group != group) {
            continue;
        }
        const std::uint32_t handle = static_cast<std::uint32_t>(entt::to_integral(entity));
        const auto found = m_entityVoices.find(handle);
        if (found != m_entityVoices.end()) {
            found->second.PlaybackId = kInvalidAudioPlaybackId;
        }
    }
}

void SceneAudioSystem::StopLoopingEntityPlaybacks(Scene& scene) {
    if (m_audioEngine != nullptr) {
        m_audioEngine->StopLoopingPlaybacks();
    }

    auto& registry = scene.m_registry->Registry;
    for (const auto entity : registry.view<AudioSourceComponent>()) {
        const auto& source = registry.get<AudioSourceComponent>(entity);
        if (!source.Loop) {
            continue;
        }
        const std::uint32_t handle = static_cast<std::uint32_t>(entt::to_integral(entity));
        const auto found = m_entityVoices.find(handle);
        if (found != m_entityVoices.end()) {
            found->second.PlaybackId = kInvalidAudioPlaybackId;
        }
    }
}

void SceneAudioSystem::PauseEntityPlayback(std::uint32_t entityHandle) {
    const auto found = m_entityVoices.find(entityHandle);
    if (found == m_entityVoices.end() || m_audioEngine == nullptr) {
        return;
    }
    if (found->second.PlaybackId != kInvalidAudioPlaybackId) {
        m_audioEngine->PausePlayback(found->second.PlaybackId);
    }
}

void SceneAudioSystem::ResumeEntityPlayback(std::uint32_t entityHandle) {
    const auto found = m_entityVoices.find(entityHandle);
    if (found == m_entityVoices.end() || m_audioEngine == nullptr) {
        return;
    }
    if (found->second.PlaybackId != kInvalidAudioPlaybackId) {
        m_audioEngine->ResumePlayback(found->second.PlaybackId);
    }
}

void SceneAudioSystem::PauseAllEntityPlaybacks() {
    if (m_audioEngine == nullptr) {
        return;
    }
    for (const auto& [handle, voice] : m_entityVoices) {
        (void)handle;
        if (voice.PlaybackId != kInvalidAudioPlaybackId) {
            m_audioEngine->PausePlayback(voice.PlaybackId);
        }
    }
}

void SceneAudioSystem::ResumeAllEntityPlaybacks() {
    if (m_audioEngine == nullptr) {
        return;
    }
    for (const auto& [handle, voice] : m_entityVoices) {
        (void)handle;
        if (voice.PlaybackId != kInvalidAudioPlaybackId) {
            m_audioEngine->ResumePlayback(voice.PlaybackId);
        }
    }
}

bool SceneAudioSystem::IsEntityPlaybackActive(std::uint32_t entityHandle) const {
    const auto found = m_entityVoices.find(entityHandle);
    if (found == m_entityVoices.end() || m_audioEngine == nullptr) {
        return false;
    }
    if (found->second.PlaybackId == kInvalidAudioPlaybackId) {
        return false;
    }
    return m_audioEngine->IsPlaybackActive(found->second.PlaybackId) &&
           !m_audioEngine->IsPlaybackPaused(found->second.PlaybackId);
}

void SceneAudioSystem::OnEntityDestroyed(std::uint32_t entityHandle) {
    const auto found = m_entityVoices.find(entityHandle);
    if (found == m_entityVoices.end()) {
        return;
    }
    if (m_audioEngine != nullptr && found->second.PlaybackId != kInvalidAudioPlaybackId) {
        m_audioEngine->StopPlayback(found->second.PlaybackId);
    }
    m_entityVoices.erase(found);
}

void SceneAudioSystem::OnSceneUpdate(Scene& scene, float /*deltaSeconds*/) {
    if (m_audioEngine == nullptr || !m_audioEngine->IsInitialized()) {
        return;
    }

    auto& registry = scene.m_registry->Registry;

    AudioListener& listener = m_audioEngine->GetListener();
    bool listenerSet = false;
    for (const auto entity : registry.view<AudioListenerComponent, TransformComponent>()) {
        const auto& listenerComponent = registry.get<AudioListenerComponent>(entity);
        if (!listenerComponent.Active) {
            continue;
        }
        const auto& transform = registry.get<TransformComponent>(entity);
        listener.SetPosition(transform.Translation);
        listener.SetActive(true);
        listenerSet = true;
        break;
    }
    if (!listenerSet) {
        listener.SetActive(false);
    }

    for (const auto entity : registry.view<AudioSourceComponent>()) {
        const std::uint32_t handle = static_cast<std::uint32_t>(entt::to_integral(entity));
        const auto& source = registry.get<AudioSourceComponent>(entity);

        EntityVoice& voice = m_entityVoices[handle];
        // Forget only finished voices. A paused voice reports "not active" but must stay tracked so it can be
        // resumed or stopped later (scene pause, PauseEntityAudio).
        if (voice.PlaybackId != kInvalidAudioPlaybackId && !m_audioEngine->IsPlaybackActive(voice.PlaybackId) &&
            !m_audioEngine->IsPlaybackPaused(voice.PlaybackId)) {
            voice.PlaybackId = kInvalidAudioPlaybackId;
        }

        if (source.PlayOnStart && !voice.PlayOnStartConsumed && source.ClipAsset.IsValid()) {
            if (m_assetManager != nullptr) {
                const AssetResult loadResult = m_assetManager->LoadAudio(source.ClipAsset);
                if (loadResult.Success) {
                    std::shared_ptr<const AudioClip> clip = m_assetManager->GetLoadedAudio(source.ClipAsset);
                    if (clip != nullptr && clip->IsValid()) {
                        const float volume = source.Muted ? 0.0f : source.Volume;
                        voice.PlaybackId =
                            m_audioEngine->PlayClip(clip, source.Group, volume, source.Pitch, source.Loop);
                    }
                }
            }
            voice.PlayOnStartConsumed = true;
        }
    }
}

} // namespace Xora2D