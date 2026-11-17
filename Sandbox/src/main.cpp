#include "Xora2D/Core/Application.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Events/Event.h"
#include "Xora2D/Events/KeyEvent.h"
#include "Xora2D/Events/WindowEvent.h"
#include "Xora2D/Input/KeyCodes.h"
#include "Xora2D/Renderer/OrthographicCamera.h"
#include "Xora2D/Renderer/Renderer2D.h"
#include "Xora2D/Renderer/Texture2D.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstdlib>
#include <exception>
#include <memory>

namespace {

constexpr float kMoveSpeed = 420.0f;

class SandboxApp : public Xora2D::Application {
public:
    SandboxApp() {
        const float width = static_cast<float>(GetWindow().GetWidth());
        const float height = static_cast<float>(GetWindow().GetHeight());
        m_camera.SetViewportSize(width, height);
        m_camera.SetPosition({width * 0.5f, height * 0.5f});

        m_logoTexture = Xora2D::Texture2D::CreateFromFile(
            Xora2D::ResolveResourcePath("Textures/Xora2D_Logo.png").string());
        XORA_LOG_INFO("Loaded sandbox texture {}x{}", m_logoTexture->GetWidth(), m_logoTexture->GetHeight());

        const Xora2D::GraphicsInfo& info = GetGraphicsInfo();
        XORA_LOG_INFO("Sandbox graphics vendor: {}", info.vendor);
        XORA_LOG_INFO("Sandbox graphics renderer: {}", info.renderer);
        XORA_LOG_INFO("Sandbox graphics version: {}", info.version);
    }

protected:
    void OnUpdate(double deltaSeconds) override {
        const float delta = static_cast<float>(deltaSeconds);
        const float move = kMoveSpeed * delta;
        glm::vec2 position = m_camera.GetPosition();
        if (m_moveLeft) {
            position.x -= move;
        }
        if (m_moveRight) {
            position.x += move;
        }
        if (m_moveDown) {
            position.y -= move;
        }
        if (m_moveUp) {
            position.y += move;
        }
        m_camera.SetPosition(position);
        m_rotationTime += delta;
        m_logCooldown -= delta;
    }

    void OnRender() override {
        m_camera.SetViewportSize(static_cast<float>(GetWindow().GetFramebufferWidth()),
                                 static_cast<float>(GetWindow().GetFramebufferHeight()));

        Xora2D::Renderer2D& renderer = GetRenderer2D();
        renderer.BeginScene(m_camera);

        renderer.DrawQuad({120.0f, 120.0f}, {220.0f, 160.0f}, glm::vec4(0.86f, 0.24f, 0.28f, 1.0f));
        renderer.DrawQuad({400.0f, 140.0f}, {140.0f, 140.0f}, glm::vec4(0.20f, 0.72f, 0.45f, 1.0f));
        renderer.DrawQuad({700.0f, 220.0f}, {180.0f, 90.0f}, glm::vec4(0.95f, 0.78f, 0.12f, 1.0f));
        for (int index = 0; index < 8; ++index) {
            const float x = 250.0f + static_cast<float>(index) * 90.0f;
            const float y = 420.0f;
            renderer.DrawQuad({x, y}, {64.0f, 64.0f}, glm::vec4(0.20f + index * 0.08f, 0.28f, 0.80f, 1.0f));
        }

        Xora2D::Transform2D rotated{};
        rotated.Position = {340.0f, 360.0f};
        rotated.Size = {160.0f, 90.0f};
        rotated.RotationRadians = std::sin(m_rotationTime) * 0.8f;
        renderer.DrawQuad(rotated, glm::vec4(0.55f, 0.36f, 0.97f, 0.92f));

        Xora2D::Transform2D logoTransform{};
        logoTransform.Position = {860.0f, 360.0f};
        logoTransform.Size = {220.0f, 220.0f};
        renderer.DrawQuad(logoTransform, *m_logoTexture);

        for (int index = 0; index < 24; ++index) {
            const float angle = static_cast<float>(index) * 0.55f;
            renderer.DrawQuad({520.0f + std::cos(angle) * 180.0f, 360.0f + std::sin(angle) * 120.0f}, {28.0f, 28.0f},
                              *m_logoTexture, glm::vec4(1.0f, 1.0f, 1.0f, 0.88f));
        }

        renderer.EndScene();

        if (m_logCooldown <= 0.0f) {
            const Xora2D::Renderer2DStats& stats = renderer.GetStats();
            XORA_LOG_INFO("Renderer2D stats — drawCalls: {}, quads: {}, vertices: {}, indices: {}", stats.drawCalls,
                          stats.quadCount, stats.vertexCount, stats.indexCount);
            renderer.ResetStats();
            m_logCooldown = 1.0f;
        }
    }

    void OnEvent(Xora2D::Event& event) override {
        Xora2D::EventDispatcher<Xora2D::WindowResizeEvent> resizeDispatcher(event);
        resizeDispatcher.Dispatch([this](Xora2D::WindowResizeEvent&) {
            m_camera.SetViewportSize(static_cast<float>(GetWindow().GetFramebufferWidth()),
                                     static_cast<float>(GetWindow().GetFramebufferHeight()));
            return false;
        });

        Xora2D::EventDispatcher<Xora2D::KeyPressedEvent> keyPressed(event);
        keyPressed.Dispatch([this](Xora2D::KeyPressedEvent& pressed) {
            SetMovementKey(pressed.GetKey(), true);
            return false;
        });

        Xora2D::EventDispatcher<Xora2D::KeyReleasedEvent> keyReleased(event);
        keyReleased.Dispatch([this](Xora2D::KeyReleasedEvent& released) {
            SetMovementKey(released.GetKey(), false);
            return false;
        });
    }

private:
    void SetMovementKey(Xora2D::Key key, bool down) {
        switch (key) {
        case Xora2D::Key::A:
        case Xora2D::Key::Left:
            m_moveLeft = down;
            break;
        case Xora2D::Key::D:
        case Xora2D::Key::Right:
            m_moveRight = down;
            break;
        case Xora2D::Key::S:
        case Xora2D::Key::Down:
            m_moveDown = down;
            break;
        case Xora2D::Key::W:
        case Xora2D::Key::Up:
            m_moveUp = down;
            break;
        default:
            break;
        }
    }

    Xora2D::OrthographicCamera m_camera;
    std::unique_ptr<Xora2D::Texture2D> m_logoTexture;
    float m_rotationTime = 0.0f;
    float m_logCooldown = 0.0f;
    bool m_moveLeft = false;
    bool m_moveRight = false;
    bool m_moveUp = false;
    bool m_moveDown = false;
};

} // namespace

int main() {
    try {
        XORA_LOG_INFO("Sandbox starting");
        int exitCode = EXIT_FAILURE;
        {
            SandboxApp application;
            exitCode = application.Run();
        }
        XORA_LOG_INFO("Sandbox stopped");
        return exitCode;
    } catch (const std::exception& exception) {
        XORA_LOG_ERROR("{}", exception.what());
        return EXIT_FAILURE;
    }
}