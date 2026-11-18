#pragma once

#include "Xora2D/Renderer/OrthographicCamera.h"
#include "Xora2D/Renderer/Renderer2DStats.h"
#include "Xora2D/Renderer/Texture2D.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Xora2D {

struct Transform2D {
    glm::vec2 Position{0.0f};
    glm::vec2 Size{100.0f, 100.0f};
    float RotationRadians = 0.0f;
};

class Renderer2D {
public:
    Renderer2D();
    ~Renderer2D();

    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    void Init();
    void Shutdown();

    void BeginScene(const OrthographicCamera& camera);
    void EndScene();

    void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
    void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Texture2D& texture,
                  const glm::vec4& tint = glm::vec4(1.0f));
    void DrawQuad(const Transform2D& transform, const glm::vec4& color);
    void DrawQuad(const Transform2D& transform, const Texture2D& texture,
                  const glm::vec4& tint = glm::vec4(1.0f));
    void DrawQuad(const Transform2D& transform, const Texture2D& texture, const glm::vec4& tint,
                  const glm::vec2 uvCorners[4]);

    const Renderer2DStats& GetStats() const { return m_stats; }
    Renderer2DStats& GetStats() { return m_stats; }
    void ResetStats();

private:
    struct ColoredVertex {
        glm::vec3 Position{0.0f};
        glm::vec4 Color{1.0f};
    };

    struct TexturedVertex {
        glm::vec3 Position{0.0f};
        glm::vec4 Color{1.0f};
        glm::vec2 TexCoord{0.0f};
        float TexIndex = 0.0f;
    };

    void FlushColoredBatch();
    void FlushTexturedBatch();
    void EnsureColoredCapacity();
    void EnsureTexturedCapacity(std::uint32_t textureId);

    std::uint32_t AcquireTextureSlot(const Texture2D& texture);

    static glm::vec2 RotateOffset(const glm::vec2& offset, float rotationRadians);

    bool m_initialized = false;
    glm::mat4 m_viewProjection{1.0f};

    std::unique_ptr<class Shader> m_colorShader;
    std::unique_ptr<class Shader> m_textureShader;
    std::unique_ptr<class VertexArray> m_colorVertexArray;
    std::unique_ptr<class VertexArray> m_textureVertexArray;
    std::unique_ptr<class VertexBuffer> m_colorVertexBuffer;
    std::unique_ptr<class VertexBuffer> m_textureVertexBuffer;
    std::unique_ptr<class IndexBuffer> m_quadIndexBuffer;

    static constexpr std::uint32_t kMaxQuads = 10'000;
    static constexpr std::uint32_t kMaxTextureSlots = 16;
    static constexpr std::uint32_t kVerticesPerQuad = 4;
    static constexpr std::uint32_t kIndicesPerQuad = 6;

    std::vector<struct ColoredVertex> m_coloredVertices;
    std::vector<struct TexturedVertex> m_texturedVertices;
    std::array<std::uint32_t, kMaxTextureSlots> m_textureSlots{};
    std::uint32_t m_textureSlotIndex = 0;

    Renderer2DStats m_stats;
    Renderer2DStats m_frameStats;
};

} // namespace Xora2D