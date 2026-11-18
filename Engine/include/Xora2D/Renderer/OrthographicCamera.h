#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

namespace Xora2D {

// 2D camera in pixel coordinates. The origin is the bottom-left corner of the
// viewport. +X points right and +Y points up.
class OrthographicCamera {
public:
    OrthographicCamera() = default;

    void SetViewportSize(float width, float height);
    void SetPosition(const glm::vec2& position) { m_position = position; Recalculate(); }
    void SetRotationRadians(float rotation) { m_rotation = rotation; Recalculate(); }
    void SetZoom(float zoom);

    const glm::vec2& GetPosition() const { return m_position; }
    float GetRotationRadians() const { return m_rotation; }
    float GetZoom() const { return m_zoom; }
    float GetViewportWidth() const { return m_viewportWidth; }
    float GetViewportHeight() const { return m_viewportHeight; }

    const glm::mat4& GetViewMatrix() const { return m_viewMatrix; }
    const glm::mat4& GetProjectionMatrix() const { return m_projectionMatrix; }
    const glm::mat4& GetViewProjectionMatrix() const { return m_viewProjectionMatrix; }

private:
    void Recalculate();

    float m_viewportWidth = 1280.0f;
    float m_viewportHeight = 720.0f;
    glm::vec2 m_position{0.0f};
    float m_rotation = 0.0f;
    float m_zoom = 1.0f;

    glm::mat4 m_viewMatrix{1.0f};
    glm::mat4 m_projectionMatrix{1.0f};
    glm::mat4 m_viewProjectionMatrix{1.0f};
};

} // namespace Xora2D