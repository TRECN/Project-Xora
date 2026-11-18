#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Xora2D {

class Window;

} // namespace Xora2D

namespace Xora2DEditor {

// Native "open files" dialog via SDL3 (asynchronous). SDL may invoke the completion callback on another thread,
// so results are stored in shared state and consumed on the main thread.
class EditorFileDialog {
public:
    EditorFileDialog();

    void RequestOpenFiles(Xora2D::Window& window, bool allowMultiple);
    // Single *.xora project file.
    void RequestOpenProjectFile(Xora2D::Window& window);
    // Single folder (e.g. the parent folder for a new project).
    void RequestOpenFolder(Xora2D::Window& window, const std::filesystem::path& defaultLocation = {});
    [[nodiscard]] std::vector<std::filesystem::path> ConsumeCompletedSelections();
    [[nodiscard]] bool HasPendingRequest() const;
    [[nodiscard]] std::string ConsumeError();

    struct SharedState {
        std::mutex Mutex;
        bool RequestInFlight = false;
        std::vector<std::filesystem::path> Completed;
        std::string Error;
    };

private:
    bool BeginRequest();

    // Shared with the in-flight SDL callback so a late callback never touches a destroyed object.
    std::shared_ptr<SharedState> m_state;
};

} // namespace Xora2DEditor
