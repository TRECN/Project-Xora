#pragma once

#include <glm/vec4.hpp>

#include <memory>

namespace Xora2D {

class GraphicsContext;

// Renderer operations used by the engine. The concrete graphics API stays
// behind RendererAPI::Create.
class RendererAPI {
public:
    virtual ~RendererAPI() = default;

    virtual void SetClearColor(const glm::vec4& color) = 0;
    virtual void Clear() = 0;
    virtual void SetViewport(int x, int y, int width, int height) = 0;
    virtual void SwapBuffers() = 0;

    static std::unique_ptr<RendererAPI> Create(GraphicsContext& context);
};

} // namespace Xora2D
