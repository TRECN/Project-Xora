#include "Xora2D/Renderer/Renderer2D.h"

#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Renderer/Buffer.h"
#include "Xora2D/Renderer/BufferLayout.h"
#include "Xora2D/Renderer/Shader.h"
#include "Xora2D/Renderer/VertexArray.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace Xora2D {
namespace {

const glm::vec2 kUnitQuadCorners[4] = {
    {-0.5f, -0.5f},
    {0.5f, -0.5f},
    {0.5f, 0.5f},
    {-0.5f, 0.5f},
};

const glm::vec2 kUnitQuadUvs[4] = {
    {0.0f, 0.0f},
    {1.0f, 0.0f},
    {1.0f, 1.0f},
    {0.0f, 1.0f},
};

constexpr std::uint32_t kVerticesPerQuad = 4;
constexpr std::uint32_t kIndicesPerQuad = 6;

std::vector<std::uint32_t> BuildQuadIndices(std::uint32_t maxQuads) {
    std::vector<std::uint32_t> indices;
    indices.reserve(maxQuads * kIndicesPerQuad);
    for (std::uint32_t quad = 0; quad < maxQuads; ++quad) {
        const std::uint32_t offset = quad * kVerticesPerQuad;
        indices.push_back(offset + 0);
        indices.push_back(offset + 1);
        indices.push_back(offset + 2);
        indices.push_back(offset + 2);
        indices.push_back(offset + 3);
        indices.push_back(offset + 0);
    }
    return indices;
}

glm::mat4 BuildModelMatrix(const Transform2D& transform) {
    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(transform.Position, 0.0f));
    model = glm::rotate(model, transform.RotationRadians, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, glm::vec3(transform.Size, 1.0f));
    return model;
}

} // namespace

Renderer2D::Renderer2D() {
    m_coloredVertices.reserve(kMaxQuads * kVerticesPerQuad);
    m_texturedVertices.reserve(kMaxQuads * kVerticesPerQuad);
}

Renderer2D::~Renderer2D() {
    Shutdown();
}

void Renderer2D::Init() {
    if (m_initialized) {
        return;
    }

    const auto colorVertexPath = ResolveResourcePath("Shaders/ColorQuad.vert");
    const auto colorFragmentPath = ResolveResourcePath("Shaders/ColorQuad.frag");
    const auto textureVertexPath = ResolveResourcePath("Shaders/TexturedQuad.vert");
    const auto textureFragmentPath = ResolveResourcePath("Shaders/TexturedQuad.frag");

    m_colorShader = Shader::CreateFromFiles(colorVertexPath.string(), colorFragmentPath.string());
    m_textureShader = Shader::CreateFromFiles(textureVertexPath.string(), textureFragmentPath.string());

    m_colorVertexArray = VertexArray::Create();
    m_textureVertexArray = VertexArray::Create();

    m_colorVertexBuffer = VertexBuffer::CreateDynamic(kMaxQuads * kVerticesPerQuad * sizeof(ColoredVertex));
    m_textureVertexBuffer = VertexBuffer::CreateDynamic(kMaxQuads * kVerticesPerQuad * sizeof(TexturedVertex));

    const std::vector<std::uint32_t> indices = BuildQuadIndices(kMaxQuads);
    m_quadIndexBuffer = IndexBuffer::Create(indices.data(), static_cast<std::uint32_t>(indices.size()));

    m_colorVertexArray->AddVertexBuffer(*m_colorVertexBuffer);
    m_colorVertexArray->SetLayout(BufferLayout{
        {ShaderDataType::Float3, "a_Position"},
        {ShaderDataType::Float4, "a_Color"},
    });

    m_textureVertexArray->AddVertexBuffer(*m_textureVertexBuffer);
    m_textureVertexArray->SetLayout(BufferLayout{
        {ShaderDataType::Float3, "a_Position"},
        {ShaderDataType::Float4, "a_Color"},
        {ShaderDataType::Float2, "a_TexCoord"},
        {ShaderDataType::Float, "a_TexIndex"},
    });

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_initialized = true;
    XORA_LOG_INFO("Renderer2D initialized");
}

void Renderer2D::Shutdown() {
    if (!m_initialized) {
        return;
    }
    m_textureShader.reset();
    m_colorShader.reset();
    m_colorVertexArray.reset();
    m_textureVertexArray.reset();
    m_colorVertexBuffer.reset();
    m_textureVertexBuffer.reset();
    m_quadIndexBuffer.reset();
    m_initialized = false;
}

void Renderer2D::BeginScene(const OrthographicCamera& camera) {
    if (!m_initialized) {
        throw std::runtime_error("Renderer2D must be initialized before BeginScene");
    }
    m_viewProjection = camera.GetViewProjectionMatrix();
    m_coloredVertices.clear();
    m_texturedVertices.clear();
    m_textureSlotIndex = 0;
    m_frameStats = {};
}

void Renderer2D::EndScene() {
    FlushColoredBatch();
    FlushTexturedBatch();
    m_stats.drawCalls += m_frameStats.drawCalls;
    m_stats.quadCount += m_frameStats.quadCount;
    m_stats.vertexCount += m_frameStats.vertexCount;
    m_stats.indexCount += m_frameStats.indexCount;
}

void Renderer2D::ResetStats() {
    m_stats = {};
}

glm::vec2 Renderer2D::RotateOffset(const glm::vec2& offset, float rotationRadians) {
    const float cosine = std::cos(rotationRadians);
    const float sine = std::sin(rotationRadians);
    return {offset.x * cosine - offset.y * sine, offset.x * sine + offset.y * cosine};
}

void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color) {
    Transform2D transform{.Position = position, .Size = size};
    DrawQuad(transform, color);
}

void Renderer2D::DrawQuad(const Transform2D& transform, const glm::vec4& color) {
    EnsureColoredCapacity();
    const glm::mat4 model = BuildModelMatrix(transform);

    for (std::uint32_t corner = 0; corner < kVerticesPerQuad; ++corner) {
        const glm::vec4 world = model * glm::vec4(kUnitQuadCorners[corner], 0.0f, 1.0f);
        m_coloredVertices.push_back(ColoredVertex{glm::vec3(world), color});
    }
}

void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const Texture2D& texture,
                            const glm::vec4& tint) {
    Transform2D transform{.Position = position, .Size = size};
    DrawQuad(transform, texture, tint);
}

void Renderer2D::DrawQuad(const Transform2D& transform, const Texture2D& texture, const glm::vec4& tint) {
    const std::uint32_t slot = AcquireTextureSlot(texture);
    const glm::mat4 model = BuildModelMatrix(transform);

    for (std::uint32_t corner = 0; corner < kVerticesPerQuad; ++corner) {
        const glm::vec4 world = model * glm::vec4(kUnitQuadCorners[corner], 0.0f, 1.0f);
        m_texturedVertices.push_back(
            TexturedVertex{glm::vec3(world), tint, kUnitQuadUvs[corner], static_cast<float>(slot)});
    }
}

void Renderer2D::EnsureColoredCapacity() {
    if (m_coloredVertices.size() + kVerticesPerQuad > kMaxQuads * kVerticesPerQuad) {
        FlushColoredBatch();
    }
    if (!m_texturedVertices.empty()) {
        FlushTexturedBatch();
    }
}

void Renderer2D::EnsureTexturedCapacity(std::uint32_t /*textureId*/) {
    if (m_texturedVertices.size() + kVerticesPerQuad > kMaxQuads * kVerticesPerQuad) {
        FlushTexturedBatch();
    }
    if (!m_coloredVertices.empty()) {
        FlushColoredBatch();
    }
    if (m_textureSlotIndex >= kMaxTextureSlots) {
        FlushTexturedBatch();
    }
}

std::uint32_t Renderer2D::AcquireTextureSlot(const Texture2D& texture) {
    EnsureTexturedCapacity(texture.GetRendererId());

    const std::uint32_t textureId = texture.GetRendererId();
    for (std::uint32_t slot = 0; slot < m_textureSlotIndex; ++slot) {
        if (m_textureSlots[slot] == textureId) {
            return slot;
        }
    }

    if (m_textureSlotIndex >= kMaxTextureSlots) {
        FlushTexturedBatch();
    }

    const std::uint32_t slot = m_textureSlotIndex++;
    m_textureSlots[slot] = textureId;
    return slot;
}

void Renderer2D::FlushColoredBatch() {
    if (m_coloredVertices.empty()) {
        return;
    }

    const std::uint32_t quadCount = static_cast<std::uint32_t>(m_coloredVertices.size() / kVerticesPerQuad);
    const std::uint32_t indexCount = quadCount * kIndicesPerQuad;

    m_colorVertexBuffer->SetData(m_coloredVertices.data(),
                                  static_cast<std::uint32_t>(m_coloredVertices.size() * sizeof(ColoredVertex)), 0);

    m_colorShader->Bind();
    m_colorShader->SetMat4("u_ViewProjection", m_viewProjection);

    m_colorVertexArray->Bind();
    m_quadIndexBuffer->Bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, nullptr);

    m_frameStats.drawCalls += 1;
    m_frameStats.quadCount += quadCount;
    m_frameStats.vertexCount += static_cast<std::uint32_t>(m_coloredVertices.size());
    m_frameStats.indexCount += indexCount;
    m_coloredVertices.clear();
}

void Renderer2D::FlushTexturedBatch() {
    if (m_texturedVertices.empty()) {
        return;
    }

    const std::uint32_t quadCount = static_cast<std::uint32_t>(m_texturedVertices.size() / kVerticesPerQuad);
    const std::uint32_t indexCount = quadCount * kIndicesPerQuad;

    m_textureVertexBuffer->SetData(
        m_texturedVertices.data(), static_cast<std::uint32_t>(m_texturedVertices.size() * sizeof(TexturedVertex)), 0);

    for (std::uint32_t slot = 0; slot < m_textureSlotIndex; ++slot) {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_textureSlots[slot]);
    }

    m_textureShader->Bind();
    m_textureShader->SetMat4("u_ViewProjection", m_viewProjection);
    for (std::uint32_t slot = 0; slot < m_textureSlotIndex; ++slot) {
        m_textureShader->SetInt("u_Textures[" + std::to_string(slot) + "]", static_cast<int>(slot));
    }

    m_textureVertexArray->Bind();
    m_quadIndexBuffer->Bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, nullptr);

    m_frameStats.drawCalls += 1;
    m_frameStats.quadCount += quadCount;
    m_frameStats.vertexCount += static_cast<std::uint32_t>(m_texturedVertices.size());
    m_frameStats.indexCount += indexCount;

    m_texturedVertices.clear();
    m_textureSlotIndex = 0;
}

} // namespace Xora2D