#pragma once

#include <glm/vec2.hpp>

namespace Xora2D {

class AudioEngine;

class AudioListener {
public:
    AudioListener() = default;

    void SetPosition(const glm::vec2& position) { m_position = position; }
    void SetForward(const glm::vec2& forward) { m_forward = forward; }
    void SetActive(bool active) { m_active = active; }

    [[nodiscard]] const glm::vec2& GetPosition() const { return m_position; }
    [[nodiscard]] const glm::vec2& GetForward() const { return m_forward; }
    [[nodiscard]] bool IsActive() const { return m_active; }

private:
    friend class AudioEngine;

    glm::vec2 m_position{0.0f};
    glm::vec2 m_forward{0.0f, 1.0f};
    bool m_active = true;
};

} // namespace Xora2D