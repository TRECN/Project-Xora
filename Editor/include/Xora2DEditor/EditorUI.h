#pragma once

#include "Xora2DEditor/Content/EditorFileDialog.h"
#include "Xora2DEditor/EditorWorkspace.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Xora2DEditor {

class EditorContext;
class EditorApplication;

// Main menu, toolbar, dock host, and status bar.
class EditorUI {
public:
    explicit EditorUI(EditorWorkspace& workspace);

    void OnImGui(EditorContext& context, EditorApplication& application);
    void ShowStatusMessage(std::string message);
    [[nodiscard]] bool ConsumeCloseRequest();

    // When true, the next dockspace frame applies the built-in layout (first launch or reset).
    void SetApplyDefaultLayoutOnNextFrame(bool apply);
    void RequestResetLayout();
    void RequestDeferredLayoutSave();

    // Save Scene (Cmd/Ctrl+S); asks for a path when the scene has no file yet or `saveAs` is set.
    void RequestSaveScene(EditorContext& context, bool saveAs);
    // Shows the "startup scene missing" dialog after a project opened without its startup scene.
    void NotifyStartupSceneMissing(EditorContext& context, std::string missingScene);

private:
    void PersistLayoutIfNeeded();

    EditorWorkspace& m_workspace;
    void DrawMainMenu(EditorContext& context, EditorApplication& application);
    void DrawToolbar(EditorContext& context);
    void DrawDockspace();
    void DrawStatusBar(EditorContext& context, const EditorApplication& application);
    void DrawScenePathPopup(EditorContext& context);
    void DrawStartupScenePopup(EditorContext& context);
    void ReportSceneResult(bool success, const std::string& message);

    std::string m_statusMessage;
    bool m_applyDefaultLayout = false;
    bool m_deferredLayoutSave = false;
    bool m_requestClose = false;

    EditorFileDialog m_projectDialog;

    // "New Scene" / "Save Scene As" popup.
    enum class ScenePathMode : std::uint8_t { None, NewScene, SaveAs };
    ScenePathMode m_scenePathMode = ScenePathMode::None;
    bool m_openScenePathPopup = false;
    std::array<char, 256> m_scenePathBuffer{};
    std::string m_scenePathError;

    // Startup scene missing dialog.
    bool m_openStartupScenePopup = false;
    std::string m_missingStartupScene;
    std::vector<std::string> m_availableScenes;
    int m_selectedScene = -1;
    std::array<char, 256> m_newStartupSceneBuffer{};
    std::string m_startupSceneError;
};

} // namespace Xora2DEditor