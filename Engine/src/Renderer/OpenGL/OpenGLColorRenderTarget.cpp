#include "Xora2D/Renderer/ColorRenderTarget.h"

#include "Xora2D/Renderer/Texture2D.h"

#include <glad/gl.h>

#include <algorithm>
#include <stdexcept>

namespace Xora2D {
namespace {

class OpenGLRenderTargetTexture final : public Texture2D {
public:
    OpenGLRenderTargetTexture() = default;

    void Allocate(std::uint32_t width, std::uint32_t height) {
        if (m_rendererId == 0) {
            glGenTextures(1, &m_rendererId);
        }
        m_width = width;
        m_height = height;
        glBindTexture(GL_TEXTURE_2D, m_rendererId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void Destroy() {
        if (m_rendererId != 0) {
            glDeleteTextures(1, &m_rendererId);
            m_rendererId = 0;
        }
        m_width = 0;
        m_height = 0;
    }

    ~OpenGLRenderTargetTexture() override { Destroy(); }

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
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        minFilter == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        magFilter == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR);
    }

    void SetWrap(TextureWrap wrapS, TextureWrap wrapT) override {
        glBindTexture(GL_TEXTURE_2D, m_rendererId);
        const GLenum wrap = wrapS == TextureWrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        (void)wrapT;
    }

private:
    std::uint32_t m_rendererId = 0;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
};

class OpenGLColorRenderTarget final : public ColorRenderTarget {
public:
    ~OpenGLColorRenderTarget() override { ReleaseFramebuffer(); }

    bool Resize(int width, int height) override {
        const int clampedWidth = std::max(width, 0);
        const int clampedHeight = std::max(height, 0);
        if (clampedWidth <= 0 || clampedHeight <= 0) {
            ReleaseFramebuffer();
            m_width = 0;
            m_height = 0;
            return false;
        }
        if (clampedWidth == m_width && clampedHeight == m_height && IsValid()) {
            return true;
        }

        ReleaseFramebuffer();
        m_width = clampedWidth;
        m_height = clampedHeight;

        m_color.Allocate(static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height));

        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color.GetRendererId(), 0);

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            ReleaseFramebuffer();
            throw std::runtime_error("Failed to create color render target framebuffer");
        }
        return true;
    }

    int GetWidth() const override { return m_width; }
    int GetHeight() const override { return m_height; }

    bool IsValid() const override { return m_fbo != 0 && m_width > 0 && m_height > 0; }

    void Bind() override {
        if (!IsValid()) {
            return;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glViewport(0, 0, m_width, m_height);
    }

    void Unbind() override { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

    Texture2D& GetColorTexture() override { return m_color; }
    const Texture2D& GetColorTexture() const override { return m_color; }

private:
    void ReleaseFramebuffer() {
        if (m_fbo != 0) {
            glDeleteFramebuffers(1, &m_fbo);
            m_fbo = 0;
        }
        m_color.Destroy();
    }

    OpenGLRenderTargetTexture m_color;
    GLuint m_fbo = 0;
    int m_width = 0;
    int m_height = 0;
};

} // namespace

std::unique_ptr<ColorRenderTarget> ColorRenderTarget::Create() {
    return std::make_unique<OpenGLColorRenderTarget>();
}

} // namespace Xora2D