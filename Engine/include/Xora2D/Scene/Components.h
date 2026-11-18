#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Core/UUID.h"
#include "Xora2D/Renderer/Texture2D.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <string>

namespace Xora2D {

struct IDComponent {
    UUID ID;
};

struct TagComponent {
    std::string Tag;

    explicit TagComponent(std::string tag)
        : Tag(std::move(tag)) {}
};

// Root-level 2D transform (no parent-child hierarchy in this milestone).
//
// Convention:
// - Translation in world pixels (same space as Renderer2D / OrthographicCamera).
// - Rotation in radians, positive = counter-clockwise (standard math, Y-up).
// - Scale multiplies the sprite quad size.
// - Local matrix: M = T * R * S (column vectors, applied as M * v).
struct TransformComponent {
    glm::vec2 Translation{0.0f};
    float RotationRadians = 0.0f;
    glm::vec2 Scale{1.0f};

    glm::mat4 GetLocalMatrix() const;
    // Matrix built from the current Translation / RotationRadians / Scale (does not rely on the cached matrix being
    // recalculated after direct field writes).
    [[nodiscard]] glm::mat4 ComputeMatrix() const;
    void RecalculateLocalMatrix();
    const glm::mat4& GetMatrix() const { return m_matrix; }

private:
    glm::mat4 m_matrix{1.0f};
};

struct SpriteRendererComponent {
    glm::vec4 Color{1.0f};
    glm::vec2 Size{100.0f, 100.0f};
    // Stable asset reference (persisted in scene files v2+).
    AssetHandle TextureAsset{};
    // Legacy scene v1 field (runtime migration only).
    std::string TexturePath;
    std::shared_ptr<Texture2D> Texture{};
    float TilingFactor = 1.0f;
    bool Visible = true;
};

struct CameraComponent {
    bool Enabled = true;
    bool Primary = false;
    float Zoom = 1.0f;
};

} // namespace Xora2D