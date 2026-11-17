#include "Xora2D/Core/Logger.h"
#include "Xora2D/Events/KeyEvent.h"
#include "Xora2D/Events/MouseEvent.h"
#include "Xora2D/Events/WindowEvent.h"
#include "Xora2D/Input/InputManager.h"
#include "Xora2D/Input/InputMapping.h"
#include "Xora2D/Input/InputMappingSerializer.h"

#include "Platform/SdlEventTranslation.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

int g_inputFailures = 0;

void Check(bool condition, const std::string& description, const char* file, int line) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {} ({}:{})", description, file, line);
    ++g_inputFailures;
}

#define CHECK(condition, description) Check(static_cast<bool>(condition), description, __FILE__, __LINE__)

void Feed(Xora2D::InputManager& input, Xora2D::Event& event) {
    input.OnEvent(event);
}

std::filesystem::path TempMappingPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("xora2d_input_" + name + ".yaml");
}

} // namespace

int RunInputTests() {
    const int before = g_inputFailures;
    XORA_LOG_INFO("TEST input system started");

    Xora2D::InputManager input;

    CHECK(input.IsKeyUp(Xora2D::Key::A), "key starts up");
    CHECK(!input.IsKeyDown(Xora2D::Key::A), "key starts not down");

    input.BeginFrame();
    Xora2D::KeyPressedEvent pressA(Xora2D::Key::A, 4, false);
    Feed(input, pressA);
    CHECK(input.IsKeyPressed(Xora2D::Key::A), "press sets pressed");
    CHECK(input.IsKeyDown(Xora2D::Key::A), "press sets down");

    input.BeginFrame();
    CHECK(!input.IsKeyPressed(Xora2D::Key::A), "pressed clears next frame");
    CHECK(input.IsKeyDown(Xora2D::Key::A), "held remains down");

    input.BeginFrame();
    Xora2D::KeyPressedEvent repeatA(Xora2D::Key::A, 4, true);
    Feed(input, repeatA);
    CHECK(!input.IsKeyPressed(Xora2D::Key::A), "repeat does not retrigger pressed");

    input.BeginFrame();
    Xora2D::KeyReleasedEvent releaseA(Xora2D::Key::A, 4);
    Feed(input, releaseA);
    CHECK(input.IsKeyReleased(Xora2D::Key::A), "release sets released");
    CHECK(input.IsKeyUp(Xora2D::Key::A), "release clears down");

    input.BeginFrame();
    CHECK(!input.IsKeyReleased(Xora2D::Key::A), "released clears next frame");

    input.BeginFrame();
    Xora2D::KeyPressedEvent pressB(Xora2D::Key::B, 5, false);
    Xora2D::KeyPressedEvent pressC(Xora2D::Key::C, 6, false);
    Feed(input, pressB);
    Feed(input, pressC);
    CHECK(input.IsKeyDown(Xora2D::Key::B) && input.IsKeyDown(Xora2D::Key::C), "multiple keys independent");

    input.BeginFrame();
    Xora2D::KeyPressedEvent tap(Xora2D::Key::D, 7, false);
    Xora2D::KeyReleasedEvent tapUp(Xora2D::Key::D, 7);
    Feed(input, tap);
    Feed(input, tapUp);
    CHECK(input.IsKeyPressed(Xora2D::Key::D) && input.IsKeyReleased(Xora2D::Key::D),
          "press and release same frame");
    CHECK(!input.IsKeyDown(Xora2D::Key::D), "not down after same-frame tap");

    CHECK(!input.IsKeyPressed(static_cast<Xora2D::Key>(9999)), "invalid key handled safely");

    input.ClearState();
    CHECK(input.IsKeyUp(Xora2D::Key::B), "clear state releases keys");

    input.BeginFrame();
    Xora2D::MouseButtonPressedEvent mouseDown(Xora2D::MouseButton::Left, 1, 10.0f, 20.0f);
    Feed(input, mouseDown);
    CHECK(input.IsMouseButtonPressed(Xora2D::MouseButton::Left), "mouse press");
    CHECK(input.IsMouseButtonDown(Xora2D::MouseButton::Left), "mouse down");
    CHECK(input.GetMouseX() == 10.0f && input.GetMouseY() == 20.0f, "mouse position");

    input.BeginFrame();
    Xora2D::MouseMovedEvent move(15.0f, 25.0f);
    Feed(input, move);
    CHECK(input.GetMouseDelta().x == 5.0f && input.GetMouseDelta().y == 5.0f, "mouse delta");

    input.BeginFrame();
    CHECK(input.GetMouseDelta().x == 0.0f, "mouse delta resets each frame");

    input.BeginFrame();
    Xora2D::MouseScrolledEvent scroll(0.0f, 2.0f);
    Feed(input, scroll);
    CHECK(input.GetScrollDelta().y == 2.0f, "scroll delta");

    input.BeginFrame();
    CHECK(input.GetScrollDelta().y == 0.0f, "scroll delta resets");

    input.BeginFrame();
    Xora2D::KeyPressedEvent holdKey(Xora2D::Key::W, 10, false);
    Feed(input, holdKey);
    Xora2D::WindowFocusLostEvent focusLost;
    Feed(input, focusLost);
    CHECK(input.IsKeyUp(Xora2D::Key::W), "focus loss clears key down");
    CHECK(input.IsKeyReleased(Xora2D::Key::W), "focus loss reports release");
    CHECK(input.IsMouseButtonUp(Xora2D::MouseButton::Left), "focus loss clears mouse");

    Xora2D::InputMapping mapping;
    CHECK(mapping.BindKey("Jump", Xora2D::Key::Space), "bind key action");
    CHECK(mapping.BindKey("Jump", Xora2D::Key::Up), "second bind same action");
    CHECK(mapping.BindMouseButton("Fire", Xora2D::MouseButton::Left), "bind mouse action");

    input.BeginFrame();
    Xora2D::KeyPressedEvent jumpKey(Xora2D::Key::Up, 11, false);
    Feed(input, jumpKey);
    CHECK(mapping.IsActionPressed(input, "Jump"), "action pressed via alternate binding");
    CHECK(mapping.IsActionDown(input, "Jump"), "action down");
    CHECK(!mapping.IsActionPressed(input, "Unknown"), "unknown action safe");

    Xora2D::InputBinding remove{};
    remove.BindingType = Xora2D::InputBinding::Type::Key;
    remove.KeyCode = Xora2D::Key::Up;
    CHECK(mapping.RemoveBinding("Jump", remove), "remove binding");
    mapping.ClearAction("Fire");
    CHECK(!mapping.HasAction("Fire"), "clear action");

    const std::filesystem::path path = TempMappingPath("roundtrip");
    Xora2D::InputMapping toSave;
    toSave.BindKey("MoveLeft", Xora2D::Key::A);
    toSave.BindKey("Jump", Xora2D::Key::Space);
    toSave.BindKey("Jump", Xora2D::Key::Up);
    toSave.BindMouseButton("Fire", Xora2D::MouseButton::Left);
    const auto saveResult = Xora2D::InputMappingSerializer::Save(toSave, path);
    CHECK(saveResult.Success, "save mapping");

    Xora2D::InputMapping loaded;
    loaded.BindKey("Placeholder", Xora2D::Key::Z);
    const auto goodLoad = Xora2D::InputMappingSerializer::Load(loaded, path);
    CHECK(goodLoad.Success, "load mapping");
    CHECK(loaded.HasAction("Jump") && loaded.GetBindings("Jump")->size() == 2, "multiple bindings preserved");
    const std::filesystem::path badPath = TempMappingPath("bad");
    {
        std::ofstream file(badPath);
        file << "Version: 1\nActions:\n  Bad:\n    - Type: Key\n      Code: NotARealKey\n";
    }
    Xora2D::InputMapping unchanged;
    unchanged.BindKey("Keep", Xora2D::Key::K);
    const auto badLoad = Xora2D::InputMappingSerializer::Load(unchanged, badPath);
    CHECK(!badLoad.Success, "reject invalid key");
    CHECK(unchanged.HasAction("Keep"), "failed load leaves mapping unchanged");

    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
    if (std::filesystem::exists(badPath)) {
        std::filesystem::remove(badPath);
    }

    SDL_Event sdlKeyDown{};
    sdlKeyDown.type = SDL_EVENT_KEY_DOWN;
    sdlKeyDown.key.scancode = SDL_SCANCODE_ESCAPE;
    sdlKeyDown.key.repeat = 0;
    const Xora2D::TranslatedEvent translated = Xora2D::TranslateSdlEvent(sdlKeyDown);
    CHECK(translated.event != nullptr, "SDL key down translates");
    if (translated.event) {
        input.BeginFrame();
        Feed(input, *translated.event);
        CHECK(input.IsKeyPressed(Xora2D::Key::Escape), "SDL escape updates input");
    }

    input.PrepareShutdown();
    CHECK(input.IsKeyUp(Xora2D::Key::A), "shutdown clears input");

    const int added = g_inputFailures - before;
    XORA_LOG_INFO("TEST input system finished ({} failures)", added);
    return added;
}