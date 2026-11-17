#include "Xora2D/Core/Application.h"
#include "Xora2D/Core/Logger.h"

#include <glm/vec4.hpp>

#include <cstdlib>
#include <exception>

namespace {

class SandboxApp : public Xora2D::Application {
public:
    SandboxApp() {
        // Dark blue-gray. Change this to recolor the cleared framebuffer.
        SetClearColor(glm::vec4{0.08f, 0.09f, 0.12f, 1.0f});

        const Xora2D::GraphicsInfo& info = GetGraphicsInfo();
        XORA_LOG_INFO("Sandbox graphics vendor: {}", info.vendor);
        XORA_LOG_INFO("Sandbox graphics renderer: {}", info.renderer);
        XORA_LOG_INFO("Sandbox graphics version: {}", info.version);
    }
};

} // namespace

int main() {
    try {
        XORA_LOG_INFO("Sandbox starting");
        int exitCode = EXIT_FAILURE;
        {
            SandboxApp application;
            exitCode = application.Run();
        }
        XORA_LOG_INFO("Sandbox stopped");
        return exitCode;
    } catch (const std::exception& exception) {
        XORA_LOG_ERROR("{}", exception.what());
        return EXIT_FAILURE;
    }
}
