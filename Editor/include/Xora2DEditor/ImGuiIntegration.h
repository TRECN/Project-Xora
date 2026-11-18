#pragma once

#include "Xora2D/Platform/Window.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace Xora2D {
class GraphicsContext;
}

namespace Xora2DEditor {

class ImGuiIntegration {
public:
    ImGuiIntegration() = default;
    ~ImGuiIntegration();

    ImGuiIntegration(const ImGuiIntegration&) = delete;
    ImGuiIntegration& operator=(const ImGuiIntegration&) = delete;

    bool Initialize(Xora2D::Window& window, Xora2D::GraphicsContext& context,
                    const std::filesystem::path& iniPath);
    void Shutdown();

    void NewFrame();
    void RenderDrawData();

    void SetIniPath(const std::filesystem::path& iniPath);
    void SaveLayout();

    // Files dropped onto the window from the OS (SDL_EVENT_DROP_FILE). Invoked on the main thread.
    void SetFileDropHandler(std::function<void(const std::filesystem::path&)> handler);

    [[nodiscard]] bool WantsKeyboardCapture() const;
    [[nodiscard]] bool WantsMouseCapture() const;

private:
    bool m_initialized = false;
    std::shared_ptr<std::function<void(const std::filesystem::path&)>> m_fileDropHandler =
        std::make_shared<std::function<void(const std::filesystem::path&)>>();
    std::filesystem::path m_iniPath;
    std::string m_iniFilename; // backing storage for ImGuiIO::IniFilename
};

} // namespace Xora2DEditor