#include "Xora2D/Renderer/OrthographicCamera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace Xora2D {

void OrthographicCamera::SetViewportSize(float width, float height) {
    m_viewportWidth = std::max(width, 1.0f);
    m_viewportHeight = std::max(height, 1.0f);
    Recalculate();
}

void OrthographicCamera::SetZoom(float zoom) {
    m_zoom = std::max(zoom, 0.01f);
    Recalculate();
}

void OrthographicCamera::Recalculate() {
    const float viewWidth = m_viewportWidth / m_zoom;
    const float viewHeight = m_viewportHeight / m_zoom;
    m_projectionMatrix = glm::ortho(0.0f, viewWidth, 0.0f, viewHeight, -1.0f, 1.0f);

    glm::mat4 view(1.0f);
    view = glm::translate(view, glm::vec3(-m_position, 0.0f));
    view = glm::rotate(view, -m_rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    m_viewMatrix = view;
    m_viewProjectionMatrix = m_projectionMatrix * m_viewMatrix;
}

} // namespace Xora2D