#include "Xora2D/Scene/Components.h"

namespace Xora2D {

glm::mat4 TransformComponent::GetLocalMatrix() const {
    return m_matrix;
}

void TransformComponent::RecalculateLocalMatrix() {
    glm::mat4 matrix(1.0f);
    matrix = glm::translate(matrix, glm::vec3(Translation, 0.0f));
    matrix = glm::rotate(matrix, RotationRadians, glm::vec3(0.0f, 0.0f, 1.0f));
    matrix = glm::scale(matrix, glm::vec3(Scale, 1.0f));
    m_matrix = matrix;
}

} // namespace Xora2D