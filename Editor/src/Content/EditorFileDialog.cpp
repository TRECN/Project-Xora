#include "Xora2DEditor/Content/EditorFileDialog.h"

#include "Xora2D/Platform/Window.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>

namespace Xora2DEditor {
namespace {

void SDLCALL OnOpenFileDialog(void* userdata, const char* const* filelist, int) {
    // Takes ownership of the heap-allocated shared_ptr handed to SDL.
    std::unique_ptr<std::shared_ptr<EditorFileDialog::SharedState>> owner(
        static_cast<std::shared_ptr<EditorFileDialog::SharedState>*>(userdata));
    EditorFileDialog::SharedState& state = **owner;

    std::scoped_lock lock(state.Mutex);
    state.RequestInFlight = false;
    if (filelist == nullptr) {
        state.Error = std::string("File dialog failed: ") + SDL_GetError();
        return;
    }
    for (int index = 0; filelist[index] != nullptr; ++index) {
        state.Completed.emplace_back(filelist[index]);
    }
}

} // namespace

EditorFileDialog::EditorFileDialog()
    : m_state(std::make_shared<SharedState>()) {}

bool EditorFileDialog::BeginRequest() {
    std::scoped_lock lock(m_state->Mutex);
    if (m_state->RequestInFlight) {
        return false;
    }
    m_state->RequestInFlight = true;
    return true;
}

void EditorFileDialog::RequestOpenProjectFile(Xora2D::Window& window) {
    if (!BeginRequest()) {
        return;
    }
    static const SDL_DialogFileFilter kFilters[] = {{"Xora2D projects (*.xora)", "xora"}};
    auto* userdata = new std::shared_ptr<SharedState>(m_state);
    SDL_ShowOpenFileDialog(OnOpenFileDialog, userdata, static_cast<SDL_Window*>(window.GetNativeWindowHandle()),
                           kFilters, static_cast<int>(std::size(kFilters)), nullptr, false);
}

void EditorFileDialog::RequestOpenFolder(Xora2D::Window& window, const std::filesystem::path& defaultLocation) {
    if (!BeginRequest()) {
        return;
    }
    const std::string location = defaultLocation.string();
    auto* userdata = new std::shared_ptr<SharedState>(m_state);
    SDL_ShowOpenFolderDialog(OnOpenFileDialog, userdata, static_cast<SDL_Window*>(window.GetNativeWindowHandle()),
                             location.empty() ? nullptr : location.c_str(), false);
}

void EditorFileDialog::RequestOpenFiles(Xora2D::Window& window, bool allowMultiple) {
    if (!BeginRequest()) {
        return;
    }

    static const SDL_DialogFileFilter kFilters[] = {
        {"All supported assets", "png;jpg;jpeg;wav;mp3;py;yaml"},
        {"Images (PNG, JPEG)", "png;jpg;jpeg"},
        {"Audio (WAV, MP3)", "wav;mp3"},
        {"Python scripts", "py"},
        {"Scenes / animation clips (YAML)", "yaml"},
    };
    auto* userdata = new std::shared_ptr<SharedState>(m_state);
    SDL_ShowOpenFileDialog(OnOpenFileDialog, userdata, static_cast<SDL_Window*>(window.GetNativeWindowHandle()),
                           kFilters, static_cast<int>(std::size(kFilters)), nullptr, allowMultiple);
}

std::vector<std::filesystem::path> EditorFileDialog::ConsumeCompletedSelections() {
    std::scoped_lock lock(m_state->Mutex);
    std::vector<std::filesystem::path> result = std::move(m_state->Completed);
    m_state->Completed.clear();
    return result;
}

bool EditorFileDialog::HasPendingRequest() const {
    std::scoped_lock lock(m_state->Mutex);
    return m_state->RequestInFlight;
}

std::string EditorFileDialog::ConsumeError() {
    std::scoped_lock lock(m_state->Mutex);
    std::string error = std::move(m_state->Error);
    m_state->Error.clear();
    return error;
}

} // namespace Xora2DEditor
