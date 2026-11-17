#pragma once

#include <cstdint>

namespace Xora2D {

struct Renderer2DStats {
    std::uint32_t drawCalls = 0;
    std::uint32_t quadCount = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
};

} // namespace Xora2D