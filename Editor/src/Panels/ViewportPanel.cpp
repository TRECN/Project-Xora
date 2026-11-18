#include "Xora2DEditor/Panels/ViewportPanel.h"

#include "Xora2DEditor/EditorApplication.h"
#include "Xora2DEditor/EditorContext.h"

#include "Xora2D/Renderer/Renderer2D.h"
#include "Xora2D/Renderer/RendererAPI.h"
#include "Xora2D/Scene/Scene.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Xora2DEditor {

void ViewportPanel::OnImGui(EditorContext& context) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const EditorPlayState playState = context.GetPlayState();
    if (m_lastPlayState == EditorPlayState::Edit && playState == EditorPlayState::Playing) {
        // Entering Play: give the game view keyboard focus so gameplay input works without an extra click.
        ImGui::SetNextWindowFocus();
    }
    m_lastPlayState = playState;

    if (ImGui::Begin(GetTitle(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const int width = std::max(1, static_cast<int>(std::floor(available.x)));
        const int height = std::max(1, static_cast<int>(std::floor(available.y)));
        m_lastWidth = width;
        m_lastHeight = height;

        if (playState == EditorPlayState::Edit) {
            ImGui::TextDisabled("Viewport %dx%d  |  Edit Mode", width, height);
        } else {
            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            const ImVec4 color = playState == EditorPlayState::Playing ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f)
                                                                      : ImVec4(0.95f, 0.75f, 0.25f, 1.0f);
            ImGui::TextColored(color, "%s", playState == EditorPlayState::Playing ? "PLAYING" : "PAUSED");
            ImGui::SameLine();
            ImGui::TextDisabled("Viewport %dx%d  |  %s", width, height,
                                focused ? "game input active" : "click the viewport to send input to the game");
        }
        ImGui::Separator();

        const ImVec2 imageSize(static_cast<float>(width), static_cast<float>(height - 4.0f));
        if (m_renderTarget != nullptr && m_renderTarget->IsValid()) {
            const Xora2D::Texture2D& texture = m_renderTarget->GetColorTexture();
            const ImTextureID textureId =
                static_cast<ImTextureID>(static_cast<std::uintptr_t>(texture.GetRendererId()));
            ImGui::Image(textureId, imageSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            const bool imageHovered = ImGui::IsItemHovered();
            if (playState != EditorPlayState::Edit) {
                const ImU32 border = playState == EditorPlayState::Playing ? IM_COL32(90, 215, 115, 255)
                                                                          : IM_COL32(242, 190, 64, 255);
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), border, 0.0f, 0,
                                                    2.0f);
            }
            context.SetViewportInputState(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), imageHovered);
        } else {
            ImGui::Dummy(imageSize);
            ImGui::SetCursorScreenPos(ImGui::GetItemRectMin());
            ImGui::TextUnformatted("No viewport target");
        }

        if (auto scene = context.GetScene()) {
            ImGui::TextDisabled("Scene: %s (%zu entities)", scene->GetSceneName().c_str(), scene->GetEntityCount());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void ViewportPanel::RenderScene(EditorApplication& application, EditorContext& context) {
    if (m_lastWidth <= 0 || m_lastHeight <= 20) {
        return;
    }

    constexpr int kViewportHeaderPx = 28;
    const int renderHeight = std::max(1, m_lastHeight - kViewportHeaderPx);
    if (m_renderTarget == nullptr) {
        m_renderTarget = Xora2D::ColorRenderTarget::Create();
    }
    (void)m_renderTarget->Resize(m_lastWidth, renderHeight);

    auto scene = context.GetScene();
    if (scene == nullptr || !m_renderTarget->IsValid()) {
        return;
    }

    application.RenderSceneIntoTarget(*m_renderTarget, m_clearColor);
}

int ViewportPanel::GetRenderWidth() const {
    return m_renderTarget != nullptr ? m_renderTarget->GetWidth() : 0;
}

int ViewportPanel::GetRenderHeight() const {
    return m_renderTarget != nullptr ? m_renderTarget->GetHeight() : 0;
}

bool ViewportPanel::HasValidTarget() const {
    return m_renderTarget != nullptr && m_renderTarget->IsValid();
}

} // namespace Xora2DEditor