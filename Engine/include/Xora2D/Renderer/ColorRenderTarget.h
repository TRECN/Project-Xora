#pragma once

#include <memory>

namespace Xora2D {

class Texture2D;

// Off-screen color render target for editor viewports and tests. OpenGL stays in the implementation.
class ColorRenderTarget {
public:
    virtual ~ColorRenderTarget() = default;

    // Returns false for non-positive dimensions (no GL resources created).
    virtual bool Resize(int width, int height) = 0;

    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
    virtual bool IsValid() const = 0;

    virtual void Bind() = 0;
    virtual void Unbind() = 0;

    virtual Texture2D& GetColorTexture() = 0;
    virtual const Texture2D& GetColorTexture() const = 0;

    static std::unique_ptr<ColorRenderTarget> Create();
};

} // namespace Xora2D