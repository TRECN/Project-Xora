#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Xora2D {

enum class TextureFilter {
    Nearest,
    Linear,
};

enum class TextureWrap {
    Repeat,
    ClampToEdge,
};

class Texture2D {
public:
    virtual ~Texture2D() = default;

    virtual void Bind(std::uint32_t slot = 0) const = 0;
    virtual void Unbind() const = 0;

    virtual std::uint32_t GetWidth() const = 0;
    virtual std::uint32_t GetHeight() const = 0;
    virtual std::uint32_t GetRendererId() const = 0;

    virtual void SetFilter(TextureFilter minFilter, TextureFilter magFilter) = 0;
    virtual void SetWrap(TextureWrap wrapS, TextureWrap wrapT) = 0;

    static std::unique_ptr<Texture2D> CreateFromFile(const std::string& path);
    static std::unique_ptr<Texture2D> CreateSolid(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                                                  std::uint8_t alpha = 255);
};

} // namespace Xora2D