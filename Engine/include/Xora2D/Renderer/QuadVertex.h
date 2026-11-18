#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace Xora2D {

// A pre-built textured quad corner (local space). Callers that cache geometry (e.g. tilemap chunks) submit these in
// bulk through Renderer2D::DrawTexturedQuads; 4 consecutive vertices form one quad (BL, BR, TR, TL).
struct TexturedQuadVertex {
    glm::vec2 Position{0.0f};
    glm::vec2 TexCoord{0.0f};
    glm::vec4 Color{1.0f};
};

} // namespace Xora2D
