#include "Xora2D/Renderer/Texture2D.h"

#include <glad/gl.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <stdexcept>
#include <string>

namespace Xora2D {
namespace {

GLenum ToGlFilter(TextureFilter filter) {
    return filter == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR;
}

GLenum ToGlWrap(TextureWrap wrap) {
    return wrap == TextureWrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
}

class OpenGLTexture2D final : public Texture2D {
public:
    OpenGLTexture2D(std::uint32_t width, std::uint32_t height, GLenum internalFormat, GLenum dataFormat,
                      const void* pixels) {
        m_width = width;
        m_height = height;

        glGenTextures(1, &m_rendererId);
        glBindTexture(GL_TEXTURE_2D, m_rendererId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), static_cast<GLsizei>(width),
                     static_cast<GLsizei>(height), 0, dataFormat, GL_UNSIGNED_BYTE, pixels);
    }

    ~OpenGLTexture2D() override {
        if (m_rendererId != 0) {
            glDeleteTextures(1, &m_rendererId);
        }
    }

    OpenGLTexture2D(const OpenGLTexture2D&) = delete;
    OpenGLTexture2D& operator=(const OpenGLTexture2D&) = delete;

    void Bind(std::uint32_t slot) const override {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_rendererId);
    }

    void Unbind() const override { glBindTexture(GL_TEXTURE_2D, 0); }

    std::uint32_t GetWidth() const override { return m_width; }
    std::uint32_t GetHeight() const override { return m_height; }
    std::uint32_t GetRendererId() const override { return m_rendererId; }

    void SetFilter(TextureFilter minFilter, TextureFilter magFilter) override {
        glBindTexture(GL_TEXTURE_2D, m_rendererId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, ToGlFilter(minFilter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, ToGlFilter(magFilter));
    }

    void SetWrap(TextureWrap wrapS, TextureWrap wrapT) override {
        glBindTexture(GL_TEXTURE_2D, m_rendererId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ToGlWrap(wrapS));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ToGlWrap(wrapT));
    }

private:
    GLuint m_rendererId = 0;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
};

} // namespace

std::unique_ptr<Texture2D> Texture2D::CreateFromFile(const std::string& path) {
    stbi_set_flip_vertically_on_load(1);
    // Also set this thread's override: other decoders (e.g. editor thumbnails) set a per-thread "no flip", which
    // would otherwise win over the global flag and load textures upside down on that thread.
    stbi_set_flip_vertically_on_load_thread(1);

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 0);
    if (pixels == nullptr) {
        throw std::runtime_error("Failed to load texture '" + path + "': " + stbi_failure_reason());
    }

    GLenum internalFormat = GL_RGBA8;
    GLenum dataFormat = GL_RGBA;
    if (channels == 3) {
        internalFormat = GL_RGB8;
        dataFormat = GL_RGB;
    } else if (channels != 4) {
        stbi_image_free(pixels);
        throw std::runtime_error("Unsupported channel count in texture: " + path);
    }

    auto texture = std::make_unique<OpenGLTexture2D>(static_cast<std::uint32_t>(width),
                                                     static_cast<std::uint32_t>(height), internalFormat, dataFormat,
                                                     pixels);
    stbi_image_free(pixels);
    return texture;
}

std::unique_ptr<Texture2D> Texture2D::CreateFromRGBA8(std::uint32_t width, std::uint32_t height,
                                                      const std::uint8_t* pixels) {
    if (width == 0 || height == 0 || pixels == nullptr) {
        return nullptr;
    }
    return std::make_unique<OpenGLTexture2D>(width, height, GL_RGBA8, GL_RGBA, pixels);
}

std::unique_ptr<Texture2D> Texture2D::CreateSolid(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                                                  std::uint8_t alpha) {
    const std::uint8_t pixel[4] = {red, green, blue, alpha};
    return std::make_unique<OpenGLTexture2D>(1, 1, GL_RGBA8, GL_RGBA, pixel);
}

} // namespace Xora2D