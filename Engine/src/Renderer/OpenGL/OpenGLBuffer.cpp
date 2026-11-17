#include "Xora2D/Renderer/Buffer.h"

#include <glad/gl.h>

namespace Xora2D {
namespace {

class OpenGLVertexBuffer final : public VertexBuffer {
public:
    explicit OpenGLVertexBuffer(std::uint32_t size, GLenum usage) {
        glGenBuffers(1, &m_rendererId);
        glBindBuffer(GL_ARRAY_BUFFER, m_rendererId);
        glBufferData(GL_ARRAY_BUFFER, size, nullptr, usage);
        m_size = size;
    }

    ~OpenGLVertexBuffer() override { glDeleteBuffers(1, &m_rendererId); }

    OpenGLVertexBuffer(const OpenGLVertexBuffer&) = delete;
    OpenGLVertexBuffer& operator=(const OpenGLVertexBuffer&) = delete;

    void Bind() const override { glBindBuffer(GL_ARRAY_BUFFER, m_rendererId); }
    void Unbind() const override { glBindBuffer(GL_ARRAY_BUFFER, 0); }

    void SetData(const void* data, std::uint32_t size, std::uint32_t offset) override {
        glBindBuffer(GL_ARRAY_BUFFER, m_rendererId);
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset), size, data);
    }

    std::uint32_t GetSize() const override { return m_size; }

private:
    GLuint m_rendererId = 0;
    std::uint32_t m_size = 0;
};

class OpenGLIndexBuffer final : public IndexBuffer {
public:
    OpenGLIndexBuffer(const std::uint32_t* indices, std::uint32_t count) : m_count(count) {
        glGenBuffers(1, &m_rendererId);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_rendererId);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(count * sizeof(std::uint32_t)), indices,
                     GL_STATIC_DRAW);
    }

    ~OpenGLIndexBuffer() override { glDeleteBuffers(1, &m_rendererId); }

    OpenGLIndexBuffer(const OpenGLIndexBuffer&) = delete;
    OpenGLIndexBuffer& operator=(const OpenGLIndexBuffer&) = delete;

    void Bind() const override { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_rendererId); }
    void Unbind() const override { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }

    std::uint32_t GetCount() const override { return m_count; }

private:
    GLuint m_rendererId = 0;
    std::uint32_t m_count = 0;
};

} // namespace

std::unique_ptr<VertexBuffer> VertexBuffer::Create(std::uint32_t size) {
    return std::make_unique<OpenGLVertexBuffer>(size, GL_STATIC_DRAW);
}

std::unique_ptr<VertexBuffer> VertexBuffer::CreateDynamic(std::uint32_t capacity) {
    return std::make_unique<OpenGLVertexBuffer>(capacity, GL_DYNAMIC_DRAW);
}

std::unique_ptr<IndexBuffer> IndexBuffer::Create(const std::uint32_t* indices, std::uint32_t count) {
    return std::make_unique<OpenGLIndexBuffer>(indices, count);
}

} // namespace Xora2D