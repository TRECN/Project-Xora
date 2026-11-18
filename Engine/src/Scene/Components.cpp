#include "Xora2D/Scene/Components.h"

namespace Xora2D {

glm::mat4 TransformComponent::GetLocalMatrix() const {
    return m_matrix;
}

glm::mat4 TransformComponent::ComputeMatrix() const {
    glm::mat4 matrix(1.0f);
    matrix = glm::translate(matrix, glm::vec3(Translation, 0.0f));
    matrix = glm::rotate(matrix, RotationRadians, glm::vec3(0.0f, 0.0f, 1.0f));
    matrix = glm::scale(matrix, glm::vec3(Scale, 1.0f));
    return matrix;
}

void TransformComponent::RecalculateLocalMatrix() {
    m_matrix = ComputeMatrix();
}

} // namespace Xora2D