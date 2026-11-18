#include "Xora2DEditor/ImGuiIntegration.h"

#include "Xora2D/Core/Logger.h"
#include "Xora2D/Renderer/GraphicsContext.h"

#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl3.h>

namespace Xora2DEditor {

namespace {

void ApplyEditorTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 4.0f;
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.13f, 0.16f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.22f, 0.28f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.21f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.22f, 0.28f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.26f, 0.32f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.22f, 0.33f, 1.0f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.18f, 0.26f, 0.38f, 0.85f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.34f, 0.48f, 0.90f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.36f, 0.52f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.18f, 0.26f, 0.38f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.34f, 0.48f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.40f, 0.56f, 1.0f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.45f, 0.70f, 0.70f);
}

} // namespace

ImGuiIntegration::~ImGuiIntegration() {
    Shutdown();
}

bool ImGuiIntegration::Initialize(Xora2D::Window& window, Xora2D::GraphicsContext& context,
                                const std::filesystem::path& iniPath) {
    if (m_initialized) {
        return true;
    }

    m_iniPath = iniPath;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (!m_iniPath.empty()) {
        // ImGui keeps the pointer, so it must reference storage that outlives the context (not a temporary).
        m_iniFilename = m_iniPath.string();
        io.IniFilename = m_iniFilename.c_str();
        std::filesystem::create_directories(m_iniPath.parent_path());
    }
    ApplyEditorTheme();

    auto* sdlWindow = static_cast<SDL_Window*>(window.GetNativeWindowHandle());
    if (sdlWindow == nullptr) {
        XORA_LOG_ERROR("ImGui SDL init failed: window handle is null");
        return false;
    }

    context.MakeCurrent();
    if (!ImGui_ImplSDL3_InitForOpenGL(sdlWindow, SDL_GL_GetCurrentContext())) {
        XORA_LOG_ERROR("ImGui_ImplSDL3_InitForOpenGL failed");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        XORA_LOG_ERROR("ImGui_ImplOpenGL3_Init failed");
        return false;
    }

    window.SetSdlEventHook([dropHandler = m_fileDropHandler](const void* sdlEvent) {
        if (sdlEvent == nullptr) {
            return;
        }
        const auto* event = static_cast<const SDL_Event*>(sdlEvent);
        if (event->type == SDL_EVENT_DROP_FILE && event->drop.data != nullptr && *dropHandler) {
            (*dropHandler)(std::filesystem::path(event->drop.data));
        }
        ImGui_ImplSDL3_ProcessEvent(event);
    });

    m_initialized = true;
    return true;
}

void ImGuiIntegration::SetFileDropHandler(std::function<void(const std::filesystem::path&)> handler) {
    *m_fileDropHandler = std::move(handler);
}

void ImGuiIntegration::Shutdown() {
    if (!m_initialized) {
        return;
    }
    SaveLayout();
    // Caller should clear the window SDL hook when tearing down.
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

void ImGuiIntegration::NewFrame() {
    if (!m_initialized) {
        return;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiIntegration::RenderDrawData() {
    if (!m_initialized) {
        return;
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiIntegration::SetIniPath(const std::filesystem::path& iniPath) {
    m_iniPath = iniPath;
    if (m_initialized) {
        m_iniFilename = m_iniPath.string();
        ImGui::GetIO().IniFilename = m_iniFilename.empty() ? nullptr : m_iniFilename.c_str();
    }
}

void ImGuiIntegration::SaveLayout() {
    if (!m_initialized || m_iniPath.empty()) {
        return;
    }
    ImGui::SaveIniSettingsToDisk(m_iniPath.string().c_str());
}

bool ImGuiIntegration::WantsKeyboardCapture() const {
    return m_initialized && ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiIntegration::WantsMouseCapture() const {
    return m_initialized && ImGui::GetIO().WantCaptureMouse;
}

} // namespace Xora2DEditor