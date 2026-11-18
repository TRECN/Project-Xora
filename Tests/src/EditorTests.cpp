#include "Xora2D/Core/Logger.h"
#include "Xora2D/Platform/Window.h"
#include "Xora2D/Renderer/ColorRenderTarget.h"
#include "Xora2D/Renderer/GraphicsContext.h"

#include "Xora2D/Renderer/Texture2D.h"
#include "Xora2DEditor/EditorConfig.h"
#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/EditorPanel.h"
#include "Xora2DEditor/EditorSceneCommands.h"
#include "Xora2DEditor/EditorSelection.h"

#include "Xora2D/Scene/Components.h"
#include "Xora2D/Physics/PhysicsComponents.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& description) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {}", description);
    ++g_failures;
}

class TestPanel final : public Xora2DEditor::EditorPanel {
public:
    [[nodiscard]] const char* GetTitle() const override { return "TestPanel"; }
    [[nodiscard]] std::string GetId() const override { return "TestPanel"; }
    void OnImGui(Xora2DEditor::EditorContext&) override {}
};

} // namespace

int RunEditorTests() {
    XORA_LOG_INFO("TEST editor started");
    const int failuresBefore = g_failures;

    {
        Xora2DEditor::EditorConfig config;
        config.GetData().Window.Width = 1440;
        config.GetData().Window.Height = 810;
        config.GetData().RecentProjects = {"SampleProject"};

        const std::filesystem::path tempPath =
            std::filesystem::temp_directory_path() / "xora2d_editor_config_test.yaml";
        const auto save = config.Save(tempPath);
        Check(save.Success, "editor config save");

        Xora2DEditor::EditorConfig loaded;
        const auto load = loaded.Load(tempPath);
        Check(load.Success, "editor config load");
        Check(loaded.GetData().Window.Width == 1440, "editor config width round-trip");
        Check(loaded.GetData().RecentProjects.size() == 1, "editor config recent projects");

        Xora2DEditor::EditorConfig invalid;
        const auto badLoad = invalid.Load(tempPath.parent_path() / "missing_editor_config.yaml");
        Check(!badLoad.Success, "editor config missing file handled");

        std::filesystem::remove(tempPath);
    }

    {
        TestPanel panel;
        Check(std::string(panel.GetId()) == "TestPanel", "editor panel id");
        panel.SetVisible(false);
        Check(!panel.IsVisible(), "editor panel visibility");
    }

    {
        Xora2D::WindowProperties props{.title = "RenderTargetTest", .width = 64, .height = 64, .enableOpenGL = true};
        Xora2D::Window window(props);
        std::unique_ptr<Xora2D::GraphicsContext> context = window.CreateGraphicsContext();
        context->MakeCurrent();

        auto target = Xora2D::ColorRenderTarget::Create();
        Check(!target->Resize(0, 0), "render target rejects zero size");
        Check(target->Resize(128, 96), "render target resize valid");
        Check(target->IsValid(), "render target valid after resize");
        Check(target->GetWidth() == 128 && target->GetHeight() == 96, "render target dimensions");
        target->Bind();
        Check(target->GetColorTexture().GetRendererId() != 0, "render target texture id");
        target->Unbind();
        Check(target->Resize(128, 96), "render target idempotent resize");
    }

    {
        Xora2DEditor::EditorContext context;
        auto scene = context.GetScene();
        Xora2D::Entity a = Xora2DEditor::CreateEmptyEntity(*scene, "Alpha");
        Xora2D::Entity b = Xora2DEditor::CreateEmptyEntity(*scene, "Bravo");
        context.SelectEntity(a);
        Check(context.GetSelection().IsSelectionValid(*scene), "editor selection valid after select");
        Check(context.GetSelectedEntity().GetUUID() == a.GetUUID(), "editor selection resolves entity");

        context.ClearSelection();
        Check(!context.GetSelection().HasSelection(), "editor selection cleared");

        const Xora2D::UUID bUuid = b.GetUUID();
        Xora2DEditor::DestroyEntity(*scene, context.GetSelection(), bUuid);
        Check(!scene->FindEntityByUUID(bUuid).IsValid(), "editor destroy entity");

        const Xora2D::UUID stale = bUuid;
        context.SelectEntityByUUID(stale);
        context.GetSelection().Sanitize(*scene);
        Check(!context.GetSelection().IsSelectionValid(*scene), "stale uuid selection sanitized");

        auto replacement = Xora2D::Scene::Create();
        context.SetScene(replacement);
        Check(!context.GetSelection().HasSelection(), "selection cleared on scene replace");

        Xora2D::Entity target = replacement->CreateEntity("Target");
        context.SelectEntity(target);
        Check(Xora2DEditor::RenameEntity(target, "Renamed") == Xora2DEditor::EditorCommandStatus::Success,
              "editor rename entity");
        Check(target.GetName() == "Renamed", "entity name updated after rename");

        Check(Xora2DEditor::AddComponentToEntity(*replacement, target, Xora2DEditor::SceneComponentKind::SpriteRenderer) ==
                  Xora2DEditor::EditorCommandStatus::Success,
              "add sprite renderer");
        Check(target.HasComponent<Xora2D::SpriteRendererComponent>(), "sprite renderer present");
        Check(Xora2DEditor::AddComponentToEntity(*replacement, target, Xora2DEditor::SceneComponentKind::SpriteRenderer) ==
                  Xora2DEditor::EditorCommandStatus::DuplicateComponent,
              "duplicate component prevented");

        auto& sprite = target.GetComponent<Xora2D::SpriteRendererComponent>();
        sprite.Color = {0.2f, 0.4f, 0.6f, 1.0f};
        Check(sprite.Color.r == 0.2f, "inspector-style component mutation");

        Check(Xora2DEditor::RemoveComponentFromEntity(*replacement, target,
                                                      Xora2DEditor::SceneComponentKind::SpriteRenderer) ==
                  Xora2DEditor::EditorCommandStatus::Success,
              "remove sprite renderer");
        Check(!target.HasComponent<Xora2D::SpriteRendererComponent>(), "sprite renderer removed");

        Check(Xora2DEditor::AddComponentToEntity(*replacement, target, Xora2DEditor::SceneComponentKind::RigidBody2D) ==
                  Xora2DEditor::EditorCommandStatus::Success,
              "add rigid body");
        Check(Xora2DEditor::RemoveComponentFromEntity(*replacement, target,
                                                      Xora2DEditor::SceneComponentKind::RigidBody2D) ==
                  Xora2DEditor::EditorCommandStatus::Success,
              "remove rigid body");
    }

    if (g_failures == failuresBefore) {
        XORA_LOG_INFO("TEST editor passed");
    } else {
        XORA_LOG_ERROR("TEST editor failed");
    }
    return g_failures - failuresBefore;
}