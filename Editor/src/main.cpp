#include "Xora2DEditor/EditorApplication.h"

#include "Xora2D/Core/Logger.h"

#include <cstdlib>
#include <exception>
#include <string_view>

int main(int argc, char** argv) {
    try {
        XORA_LOG_INFO("Xora2D Editor starting");
        Xora2DEditor::EditorLaunchOptions options{};
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--project" && index + 1 < argc) {
                options.ProjectFile = argv[++index];
            } else if (argument.ends_with(".xora")) {
                options.ProjectFile = std::string(argument);
            }
        }
        Xora2DEditor::EditorApplication application(std::move(options));
        const int exitCode = application.Run();
        XORA_LOG_INFO("Xora2D Editor stopped");
        return exitCode;
    } catch (const std::exception& exception) {
        XORA_LOG_ERROR("{}", exception.what());
        return EXIT_FAILURE;
    }
}