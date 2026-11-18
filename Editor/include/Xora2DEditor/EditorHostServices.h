#pragma once

#include <filesystem>
#include <functional>

namespace Xora2D {

class AudioEngine;
class Window;

} // namespace Xora2D

namespace Xora2DEditor {

struct EditorHostServices {
    Xora2D::AudioEngine* AudioEngine = nullptr;
    Xora2D::Window* MainWindow = nullptr;
    std::function<void(const std::filesystem::path& sceneFile)> OpenScene;
    // Called after a project / content root is opened, closed or its configuration changed (window title, vsync...).
    std::function<void()> OnProjectChanged;
    // Drops cached script modules so scripts of the previous project can never run in the next one.
    std::function<void()> ResetScriptState;
};

} // namespace Xora2DEditor