#pragma once

#include "Xora2D/Scene/Scene.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace Xora2D {
class AssetManager;
}

namespace Xora2DEditor {

enum class EditorPlayState : std::uint8_t { Edit, Playing, Paused };

[[nodiscard]] const char* ToString(EditorPlayState state);

// Which gameplay input the editor forwards to the InputManager this frame. Keyboard: only while Playing with the
// viewport focused and no text field active. Mouse: additionally only while the cursor is over the game view.
// Everything is blocked in Edit and Paused mode, so clicks and keys aimed at editor panels never reach the game.
struct GameplayInputGate {
    bool Keyboard = false;
    bool Mouse = false;
};

[[nodiscard]] GameplayInputGate ComputeGameplayInputGate(EditorPlayState state, bool viewportFocused,
                                                         bool viewportHovered, bool textInputActive);

// Host callbacks so the session stays independent of EditorApplication (and testable without a window).
struct PlaySessionHooks {
    // Connect engine services (audio engine, Python script engine, ...) to a freshly created runtime scene.
    std::function<void(Xora2D::Scene&)> WireRuntimeScene;
    // Release held keys / mouse buttons so nothing "sticks" across pause, stop or focus changes.
    std::function<void()> ClearGameplayInput;
};

// Owns the runtime copy of the edited scene and drives Play / Pause / Step / Stop.
//
// - Play clones the editable scene (Scene::Copy) and simulates only the clone. The editable scene is never
//   updated while a session is active, and edit mode never simulates at all.
// - Pause stops all simulation (physics, scripts, animation, audio source updates) and pauses runtime audio.
// - Step (only while paused) advances exactly one simulation frame of one physics fixed timestep on the next Update.
// - Stop shuts down the runtime scene (script on_destroy, audio voices, physics bodies) and drops it.
class EditorPlaySession {
public:
    EditorPlaySession() = default;
    ~EditorPlaySession();

    EditorPlaySession(const EditorPlaySession&) = delete;
    EditorPlaySession& operator=(const EditorPlaySession&) = delete;

    void SetHooks(PlaySessionHooks hooks) { m_hooks = std::move(hooks); }

    [[nodiscard]] EditorPlayState GetState() const { return m_state; }
    [[nodiscard]] bool IsActive() const { return m_state != EditorPlayState::Edit; }
    [[nodiscard]] const std::shared_ptr<Xora2D::Scene>& GetRuntimeScene() const { return m_runtimeScene; }

    // Each returns false (and changes nothing) when the transition is not valid in the current state.
    bool Play(const Xora2D::Scene& editScene, Xora2D::AssetManager* assets);
    bool Pause();
    bool Resume();
    bool Step();
    bool Stop();

    [[nodiscard]] bool CanPlay() const { return m_state == EditorPlayState::Edit; }
    [[nodiscard]] bool CanPause() const { return m_state == EditorPlayState::Playing; }
    [[nodiscard]] bool CanResume() const { return m_state == EditorPlayState::Paused; }
    [[nodiscard]] bool CanStep() const { return m_state == EditorPlayState::Paused && !m_stepPending; }
    [[nodiscard]] bool CanStop() const { return m_state != EditorPlayState::Edit; }

    // Advances the runtime scene for this editor frame. Returns true if a simulation frame ran.
    bool Update(float deltaSeconds);

    // Simulation frames run in the current session (reset on Play).
    [[nodiscard]] std::uint64_t GetSimulatedFrameCount() const { return m_simulatedFrames; }

private:
    PlaySessionHooks m_hooks;
    EditorPlayState m_state = EditorPlayState::Edit;
    std::shared_ptr<Xora2D::Scene> m_runtimeScene;
    bool m_stepPending = false;
    std::uint64_t m_simulatedFrames = 0;
};

} // namespace Xora2DEditor
