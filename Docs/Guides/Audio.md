# Audio System (Milestone 10)

Xora2D audio uses **miniaudio** behind an internal backend interface. No miniaudio or SDL types appear in public headers.

## Architecture

- `Application` owns `AudioEngine`, initializes it at startup, calls `Update` each frame, and stops playback on shutdown.
- `AudioClip` holds decoded PCM (main-thread decode via `AudioClip::LoadFromFile` or `AssetManager::LoadAudio`).
- `AudioSource` wraps one playback instance (`AudioPlaybackId` inside the engine).
- `SceneAudioSystem` (internal) drives ECS `AudioSourceComponent` / `AudioListenerComponent`.
- Backends: miniaudio device engine (default) or null backend when `AudioEngineSettings::Backend = Null` or device init fails.

## Volume groups

| Group | Bus |
| --- | --- |
| `AudioGroup::Music` | Music volume × master |
| `AudioGroup::SoundEffects` | SFX volume × master |

## Example

```cpp
auto& audio = GetAudioEngine();
auto clip = assetManager.GetLoadedAudio(handle);
AudioSource source = audio.CreateSource();
source.Play(clip, AudioGroup::SoundEffects, 0.8f, 1.0f, false);
audio.SetMasterVolume(0.7f);
```

## ECS

`AudioSourceComponent` references `ClipAsset` (stable handle). `PlayOnStart` triggers once per scene instance. Runtime playback IDs are **not** stored on the component.

## Serialization

Scene format v2 adds optional `AudioSourceComponent` / `AudioListenerComponent` blocks. Older scenes without audio load unchanged.

## Threading

Decode and `ma_sound` control run on the **main thread**. Do not release a clip until all voices using it are stopped. `PrepareShutdown` stops all voices before backend teardown.

## Future backends

Implement `AudioBackend` in `Engine/src/Audio/` and select it from `AudioEngine::Initialize`.