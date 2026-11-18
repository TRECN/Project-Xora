#include "Xora2DEditor/EditorPlaySession.h"

#include "Xora2D/Core/Logger.h"
#include "Xora2D/Physics/PhysicsWorld2D.h"

namespace Xora2DEditor {

const char* ToString(EditorPlayState state) {
    switch (state) {
    case EditorPlayState::Edit:
        return "Edit";
    case EditorPlayState::Playing:
        return "Playing";
    case EditorPlayState::Paused:
        return "Paused";
    }
    return "Unknown";
}

GameplayInputGate ComputeGameplayInputGate(EditorPlayState state, bool viewportFocused, bool viewportHovered,
                                           bool textInputActive) {
    GameplayInputGate gate;
    const bool playing = state == EditorPlayState::Playing;
    gate.Keyboard = playing && viewportFocused && !textInputActive;
    gate.Mouse = playing && viewportFocused && viewportHovered;
    return gate;
}

EditorPlaySession::~EditorPlaySession() {
    (void)Stop();
}

bool EditorPlaySession::Play(const Xora2D::Scene& editScene, Xora2D::AssetManager* assets) {
    if (!CanPlay()) {
        return false;
    }
    m_runtimeScene = Xora2D::Scene::Copy(editScene);
    m_runtimeScene->SetAssetManager(assets);
    if (m_hooks.WireRuntimeScene) {
        m_hooks.WireRuntimeScene(*m_runtimeScene);
    }
    m_runtimeScene->GetPhysicsWorld().SetSimulationEnabled(true);
    m_runtimeScene->SynchronizePhysicsBodies();
    if (m_hooks.ClearGameplayInput) {
        m_hooks.ClearGameplayInput();
    }
    m_stepPending = false;
    m_simulatedFrames = 0;
    m_state = EditorPlayState::Playing;
    XORA_LOG_INFO("Play mode: started '{}' ({} entities)", m_runtimeScene->GetSceneName(),
                  m_runtimeScene->GetEntityCount());
    return true;
}

bool EditorPlaySession::Pause() {
    if (!CanPause()) {
        return false;
    }
    m_runtimeScene->PauseAudio();
    if (m_hooks.ClearGameplayInput) {
        m_hooks.ClearGameplayInput();
    }
    m_stepPending = false;
    m_state = EditorPlayState::Paused;
    return true;
}

bool EditorPlaySession::Resume() {
    if (!CanResume()) {
        return false;
    }
    m_stepPending = false;
    m_runtimeScene->ResumeAudio();
    m_state = EditorPlayState::Playing;
    return true;
}

bool EditorPlaySession::Step() {
    if (!CanStep()) {
        return false;
    }
    m_stepPending = true;
    return true;
}

bool EditorPlaySession::Stop() {
    if (!CanStop()) {
        return false;
    }
    // Shut down runtime systems while the scene and the engine services are still alive: Python on_destroy
    // callbacks run, audio voices stop, physics stops dispatching callbacks and bodies are released with the scene.
    m_runtimeScene->PrepareForShutdown();
    m_runtimeScene->Clear();
    m_runtimeScene.reset();
    if (m_hooks.ClearGameplayInput) {
        m_hooks.ClearGameplayInput();
    }
    m_stepPending = false;
    m_state = EditorPlayState::Edit;
    XORA_LOG_INFO("Play mode: stopped after {} simulation frame(s)", m_simulatedFrames);
    return true;
}

bool EditorPlaySession::Update(float deltaSeconds) {
    switch (m_state) {
    case EditorPlayState::Edit:
        return false;
    case EditorPlayState::Playing:
        m_runtimeScene->OnUpdate(deltaSeconds);
        ++m_simulatedFrames;
        return true;
    case EditorPlayState::Paused:
        if (!m_stepPending) {
            return false;
        }
        m_stepPending = false;
        // One fixed timestep: the physics accumulator holds less than one step, so this runs exactly one substep.
        m_runtimeScene->OnUpdate(m_runtimeScene->GetPhysicsWorld().GetSettings().FixedTimestep);
        // Voices started during the step (e.g. PlayOnStart) must not keep playing while paused.
        m_runtimeScene->PauseAudio();
        ++m_simulatedFrames;
        return true;
    }
    return false;
}

} // namespace Xora2DEditor
