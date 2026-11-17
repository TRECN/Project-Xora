#include "Xora2D/Renderer/VertexArray.h"

#include "Xora2D/Renderer/Buffer.h"
#include "Xora2D/Renderer/BufferLayout.h"

#include <glad/gl.h>

#include <stdexcept>

namespace Xora2D {
namespace {

GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type) {
    switch (type) {
    case ShaderDataType::Float:
    case ShaderDataType::Float2:
    case ShaderDataType::Float3:
    case ShaderDataType::Float4:
        return GL_FLOAT;
    case ShaderDataType::Int:
    case ShaderDataType::Int2:
    case ShaderDataType::Int3:
    case ShaderDataType::Int4:
        return GL_INT;
    case ShaderDataType::Bool:
        return GL_BOOL;
    }
    throw std::runtime_error("Unknown shader data type");
}

class OpenGLVertexArray final : public VertexArray {
public:
    OpenGLVertexArray() { glGenVertexArrays(1, &m_rendererId); }

    ~OpenGLVertexArray() override { glDeleteVertexArrays(1, &m_rendererId); }

    OpenGLVertexArray(const OpenGLVertexArray&) = delete;
    OpenGLVertexArray& operator=(const OpenGLVertexArray&) = delete;

    void Bind() const override { glBindVertexArray(m_rendererId); }

    void Unbind() const override { glBindVertexArray(0); }

    void AddVertexBuffer(const VertexBuffer& vertexBuffer) override {
        vertexBuffer.Bind();
        m_vertexBuffer = &vertexBuffer;
    }

    void SetIndexBuffer(const IndexBuffer& indexBuffer) override {
        indexBuffer.Bind();
        m_indexBuffer = &indexBuffer;
    }

    void SetLayout(const BufferLayout& layout) override {
        if (m_vertexBuffer == nullptr) {
            throw std::runtime_error("Vertex buffer must be set before layout");
        }

        Bind();
        m_vertexBuffer->Bind();

        std::uint32_t index = 0;
        for (const BufferElement& element : layout.GetElements()) {
            glEnableVertexAttribArray(index);
            glVertexAttribPointer(index, static_cast<GLint>(element.GetComponentCount()),
                                  ShaderDataTypeToOpenGLBaseType(element.GetType()),
                                  element.IsNormalized() ? GL_TRUE : GL_FALSE, layout.GetStride(),
                                  reinterpret_cast<const void*>(static_cast<std::uintptr_t>(element.GetOffset())));
            ++index;
        }
        m_layout = layout;
    }

private:
    GLuint m_rendererId = 0;
    const VertexBuffer* m_vertexBuffer = nullptr;
    const IndexBuffer* m_indexBuffer = nullptr;
    BufferLayout m_layout;
};

} // namespace

std::unique_ptr<VertexArray> VertexArray::Create() {
    return std::make_unique<OpenGLVertexArray>();
}

} // namespace Xora2D