#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Renderer/BufferLayout.h"
#include "Xora2D/Renderer/OrthographicCamera.h"
#include "Xora2D/Renderer/Renderer2D.h"
#include "Xora2D/Renderer/Shader.h"
#include "Xora2D/Renderer/Texture2D.h"
#include "Xora2D/Platform/Window.h"
#include "Xora2D/Renderer/GraphicsContext.h"
#include "Xora2D/Renderer/GraphicsDevice.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdlib>
#include <exception>
#include <string>

namespace {

int g_rendererFailures = 0;

void Check(bool condition, const std::string& description, const char* file, int line) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {} ({}:{})", description, file, line);
    ++g_rendererFailures;
}

#define CHECK(condition, description) Check(static_cast<bool>(condition), description, __FILE__, __LINE__)

bool Near(float value, float expected) {
    return std::abs(value - expected) < 0.001f;
}

void TestBufferLayout() {
    XORA_LOG_INFO("TEST buffer layout started");

    const Xora2D::BufferLayout layout{
        {Xora2D::ShaderDataType::Float3, "a_Position"},
        {Xora2D::ShaderDataType::Float4, "a_Color"},
        {Xora2D::ShaderDataType::Float2, "a_TexCoord"},
        {Xora2D::ShaderDataType::Float, "a_TexIndex"},
    };

    CHECK(layout.GetStride() == 40, "layout stride is 40 bytes");
    CHECK(layout.GetElements()[0].GetOffset() == 0, "position offset is 0");
    CHECK(layout.GetElements()[1].GetOffset() == 12, "color offset is 12");
    CHECK(layout.GetElements()[2].GetOffset() == 28, "texcoord offset is 28");
    CHECK(layout.GetElements()[3].GetOffset() == 36, "texindex offset is 36");

    XORA_LOG_INFO("TEST buffer layout finished");
}

void TestOrthographicCamera() {
    XORA_LOG_INFO("TEST orthographic camera started");

    Xora2D::OrthographicCamera camera;
    camera.SetViewportSize(800.0f, 600.0f);
    camera.SetPosition({100.0f, 50.0f});

    const glm::vec4 beforeZoom = camera.GetViewProjectionMatrix() * glm::vec4(100.0f, 50.0f, 0.0f, 1.0f);
    CHECK(Near(beforeZoom.w, 1.0f), "view-projection keeps w at 1 for affine transforms");

    camera.SetZoom(2.0f);
    CHECK(Near(camera.GetZoom(), 2.0f), "camera zoom stored");
    CHECK(camera.GetProjectionMatrix()[0][0] > 0.0f, "projection matrix stays valid after zoom");

    XORA_LOG_INFO("TEST orthographic camera finished");
}

void TestShaderAndTexture() {
    XORA_LOG_INFO("TEST shader and texture started");

    bool invalidShaderThrown = false;
    try {
        (void)Xora2D::Shader::CreateFromSource("Broken", "not valid glsl", "also invalid");
    } catch (const std::exception&) {
        invalidShaderThrown = true;
    }
    CHECK(invalidShaderThrown, "invalid shader source throws");

    bool invalidTextureThrown = false;
    try {
        (void)Xora2D::Texture2D::CreateFromFile("/path/that/does/not/exist.png");
    } catch (const std::exception&) {
        invalidTextureThrown = true;
    }
    CHECK(invalidTextureThrown, "invalid texture path throws");

    Xora2D::WindowProperties properties;
    properties.title = "Renderer Tests";
    properties.width = 320;
    properties.height = 240;
    properties.enableOpenGL = true;
    Xora2D::Window window(properties);
    std::unique_ptr<Xora2D::GraphicsContext> context = window.CreateGraphicsContext();
    context->MakeCurrent();
    Xora2D::GraphicsDevice device(*context);

    const auto texturePath = Xora2D::ResolveResourcePath("Textures/Xora2D_Logo.png");
    auto texture = Xora2D::Texture2D::CreateFromFile(texturePath.string());
    CHECK(texture->GetWidth() > 0 && texture->GetHeight() > 0, "texture loads with non-zero dimensions");

    auto shader = Xora2D::Shader::CreateFromSource(
        "TestColor",
        "#version 330 core\nvoid main(){gl_Position=vec4(0.0);}",
        "#version 330 core\nout vec4 FragColor;void main(){FragColor=vec4(1.0);}");
    CHECK(shader->GetRendererId() != 0, "valid shader program id is non-zero");

    XORA_LOG_INFO("TEST shader and texture finished");
}

void TestRendererBatching() {
    XORA_LOG_INFO("TEST renderer2d batching started");

    Xora2D::WindowProperties properties;
    properties.title = "Renderer2D Tests";
    properties.width = 640;
    properties.height = 480;
    properties.enableOpenGL = true;
    Xora2D::Window window(properties);
    std::unique_ptr<Xora2D::GraphicsContext> context = window.CreateGraphicsContext();
    context->MakeCurrent();
    Xora2D::GraphicsDevice device(*context);

    Xora2D::Renderer2D renderer;
    renderer.Init();

    Xora2D::OrthographicCamera camera;
    camera.SetViewportSize(static_cast<float>(window.GetFramebufferWidth()),
                           static_cast<float>(window.GetFramebufferHeight()));

    auto texture = Xora2D::Texture2D::CreateFromFile(
        Xora2D::ResolveResourcePath("Textures/Xora2D_Logo.png").string());

    renderer.BeginScene(camera);
    for (int index = 0; index < 32; ++index) {
        renderer.DrawQuad({40.0f + index * 10.0f, 80.0f}, {20.0f, 20.0f},
                          glm::vec4(0.2f, 0.4f, 0.9f, 1.0f));
    }
    for (int index = 0; index < 48; ++index) {
        renderer.DrawQuad({40.0f + (index % 12) * 40.0f, 200.0f + (index / 12) * 40.0f}, {24.0f, 24.0f}, *texture);
    }
    renderer.EndScene();

    const Xora2D::Renderer2DStats& stats = renderer.GetStats();
    XORA_LOG_INFO("  batch stats: drawCalls={} quads={} vertices={} indices={}", stats.drawCalls, stats.quadCount,
                  stats.vertexCount, stats.indexCount);
    CHECK(stats.drawCalls <= 3, "batching uses at most three draw calls for this scene");
    CHECK(stats.quadCount == 80, "quad count matches submitted sprites");
    CHECK(stats.vertexCount == 80 * 4, "vertex count matches submitted sprites");
    CHECK(stats.indexCount == 80 * 6, "index count matches submitted sprites");

    renderer.Shutdown();
    XORA_LOG_INFO("TEST renderer2d batching finished");
}

} // namespace

int RunRendererTests() {
    const int failuresBefore = g_rendererFailures;
    TestBufferLayout();
    TestOrthographicCamera();
    TestShaderAndTexture();
    TestRendererBatching();

    const int failed = g_rendererFailures - failuresBefore;
    if (failed == 0) {
        XORA_LOG_INFO("Renderer unit tests passed");
    } else {
        XORA_LOG_ERROR("Renderer unit tests failed ({} checks)", failed);
    }
    return failed;
}