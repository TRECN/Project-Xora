#pragma once

#include "Xora2DEditor/EditorPanel.h"
#include "Xora2DEditor/EditorPlaySession.h"

#include "Xora2D/Renderer/ColorRenderTarget.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <memory>

namespace Xora2DEditor {

class EditorApplication;

class ViewportPanel final : public EditorPanel {
public:
    [[nodiscard]] const char* GetTitle() const override { return "Viewport"; }
    [[nodiscard]] std::string GetId() const override { return "Viewport"; }

    void OnImGui(EditorContext& context) override;
    void RenderScene(EditorApplication& application, EditorContext& context);

    [[nodiscard]] int GetRenderWidth() const;
    [[nodiscard]] int GetRenderHeight() const;
    [[nodiscard]] bool HasValidTarget() const;
    // Game-view pixel (origin bottom-left) -> world position through the active scene's primary camera.
    [[nodiscard]] static glm::vec2 ViewToWorld(EditorContext& context, const glm::vec2& viewPixels);

private:
    std::unique_ptr<Xora2D::ColorRenderTarget> m_renderTarget;
    int m_lastWidth = 0;
    int m_lastHeight = 0;
    EditorPlayState m_lastPlayState = EditorPlayState::Edit;
    glm::vec4 m_clearColor{0.11f, 0.12f, 0.16f, 1.0f};
};

} // namespace Xora2DEditor