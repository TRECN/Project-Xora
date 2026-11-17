#pragma once

#include <string>

namespace Xora2D {

class GraphicsContext;

struct GraphicsInfo {
    std::string vendor;
    std::string renderer;
    std::string version;
    int majorVersion = 0;
    int minorVersion = 0;
};

// Loads OpenGL entry points after the context is current and records driver info.
// The context must outlive this object.
class GraphicsDevice {
public:
    explicit GraphicsDevice(GraphicsContext& context);

    const GraphicsInfo& GetInfo() const { return m_info; }

private:
    GraphicsInfo m_info;
};

} // namespace Xora2D