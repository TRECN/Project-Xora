#pragma once

namespace Xora2D {

// Platform graphics context. OpenGL and SDL types stay in the implementation.
class GraphicsContext {
public:
    virtual ~GraphicsContext() = default;

    virtual void MakeCurrent() = 0;
    virtual void SwapBuffers() = 0;

    // Returns false when the driver rejects the requested interval.
    virtual bool SetVSync(bool enabled) = 0;
    virtual bool IsVSyncEnabled() const = 0;
};

} // namespace Xora2D