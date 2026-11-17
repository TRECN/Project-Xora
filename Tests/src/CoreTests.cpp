#include "Platform/SdlEventTranslation.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Time.h"
#include "Xora2D/Events/ApplicationEvent.h"
#include "Xora2D/Events/KeyEvent.h"
#include "Xora2D/Events/MouseEvent.h"
#include "Xora2D/Events/WindowEvent.h"
#include "Xora2D/Platform/Window.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;
int g_checksInTest = 0;

void Check(bool condition, const std::string& description, const char* file, int line) {
    ++g_checksInTest;
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }

    XORA_LOG_ERROR("  fail: {} ({}:{})", description, file, line);
    ++g_failures;
}

#define CHECK(condition, description) Check(static_cast<bool>(condition), description, __FILE__, __LINE__)

bool Near(double value, double expected) {
    return std::abs(value - expected) < 0.000001;
}

void BeginTest(const char* name) {
    g_checksInTest = 0;
    XORA_LOG_INFO("TEST {} started", name);
}

void EndTest(const char* name, int failuresBefore) {
    const int failedHere = g_failures - failuresBefore;
    if (failedHere == 0) {
        XORA_LOG_INFO("TEST {} passed ({} checks)", name, g_checksInTest);
        return;
    }
    XORA_LOG_ERROR("TEST {} failed ({} of {} checks)", name, failedHere, g_checksInTest);
}

void TestLogger() {
    const int failuresBefore = g_failures;
    BeginTest("logger");

    std::tm local{};
    local.tm_year = 126;
    local.tm_mon = 10;
    local.tm_mday = 17;
    local.tm_hour = 9;
    local.tm_min = 8;
    local.tm_sec = 7;
    local.tm_isdst = -1;
    const std::time_t seconds = std::mktime(&local);
    const auto time = std::chrono::system_clock::from_time_t(seconds) + std::chrono::milliseconds(42);

    const std::string line = Xora2D::Logger::FormatLine(Xora2D::LogLevel::Warn, "disk full", time);
    XORA_LOG_INFO("  formatted line: {}", line);
    CHECK(line.find("09:08:07.042") != std::string::npos, "timestamp is 09:08:07.042");
    CHECK(line.find("[WARN] disk full") != std::string::npos, "level and message are present");

    const Xora2D::LogLevel previous = Xora2D::Logger::GetLevel();
    Xora2D::Logger::SetLevel(Xora2D::LogLevel::Error);
    const bool warnHidden = !Xora2D::Logger::IsEnabled(Xora2D::LogLevel::Warn);
    const bool errorVisible = Xora2D::Logger::IsEnabled(Xora2D::LogLevel::Error);
    Xora2D::Logger::SetLevel(previous);
    CHECK(warnHidden, "WARN is hidden when the minimum level is ERROR");
    CHECK(errorVisible, "ERROR stays visible when the minimum level is ERROR");

    EndTest("logger", failuresBefore);
}

void TestEvents() {
    const int failuresBefore = g_failures;
    BeginTest("events");

    Xora2D::AppQuitEvent quit;
    CHECK(quit.GetType() == Xora2D::EventType::AppQuit, "quit event type");
    CHECK(quit.GetName() == "AppQuit", "quit event name");
    CHECK(quit.IsInCategory(Xora2D::EventCategory::Application), "quit is an application event");
    CHECK(!quit.IsInCategory(Xora2D::EventCategory::Window), "quit is not a window event");

    Xora2D::WindowCloseEvent close;
    CHECK(close.IsInCategory(Xora2D::EventCategory::Application), "close is an application event");
    CHECK(close.IsInCategory(Xora2D::EventCategory::Window), "close is a window event");
    CHECK(!close.IsInCategory(Xora2D::EventCategory::Input), "close is not an input event");

    Xora2D::WindowResizeEvent resize(800, 600);
    CHECK(resize.GetWidth() == 800 && resize.GetHeight() == 600, "resize stores 800x600");
    CHECK(resize.ToString() == "WindowResize 800x600", "resize text is WindowResize 800x600");

    Xora2D::KeyPressedEvent key(Xora2D::Key::A, 4, true);
    XORA_LOG_INFO("  key event: {}", key.ToString());
    CHECK(key.IsInCategory(Xora2D::EventCategory::Keyboard), "key press is a keyboard event");
    CHECK(key.IsInCategory(Xora2D::EventCategory::Input), "key press is an input event");
    CHECK(!key.IsInCategory(Xora2D::EventCategory::Mouse), "key press is not a mouse event");
    CHECK(key.IsRepeat(), "key press records repeat");

    Xora2D::MouseButtonPressedEvent click(Xora2D::MouseButton::Left, 1, 20.0f, 30.0f);
    XORA_LOG_INFO("  mouse event: {}", click.ToString());
    CHECK(click.IsInCategory(Xora2D::EventCategory::MouseButton), "click is a mouse-button event");
    CHECK(click.GetButton() == Xora2D::MouseButton::Left, "click button is left");

    bool sawResize = false;
    bool sawClose = false;
    Xora2D::EventDispatcher<Xora2D::WindowCloseEvent> wrong(resize);
    wrong.Dispatch([&sawClose](Xora2D::WindowCloseEvent&) {
        sawClose = true;
        return true;
    });

    Xora2D::EventDispatcher<Xora2D::WindowResizeEvent> matching(resize);
    const bool dispatched = matching.Dispatch([&sawResize](Xora2D::WindowResizeEvent& event) {
        sawResize = event.GetWidth() == 800;
        return true;
    });

    CHECK(!sawClose, "dispatcher ignores a window-close handler for a resize");
    CHECK(sawResize && dispatched, "dispatcher delivers the resize to its handler");
    CHECK(resize.IsHandled(), "matched event is marked handled");
    CHECK(!close.IsHandled(), "unmatched event stays unhandled");

    EndTest("events", failuresBefore);
}

void TestTime() {
    const int failuresBefore = g_failures;
    BeginTest("time");

    using clock = std::chrono::steady_clock;
    const auto start = clock::time_point(std::chrono::seconds(10));
    Xora2D::Time time(start);

    CHECK(Near(time.Tick(start), 0.0) && Near(time.ElapsedSeconds(), 0.0), "first tick at startup is 0 seconds");

    CHECK(Near(time.Tick(start + std::chrono::milliseconds(100)), 0.1), "delta after 100ms is 0.1 seconds");
    CHECK(Near(time.ElapsedSeconds(), 0.1), "elapsed after 100ms is 0.1 seconds");

    CHECK(Near(time.Tick(start + std::chrono::milliseconds(250)), 0.15), "delta after another 150ms is 0.15 seconds");
    CHECK(Near(time.ElapsedSeconds(), 0.25), "elapsed after 250ms is 0.25 seconds");

    CHECK(Near(time.Tick(start + std::chrono::milliseconds(200)), 0.0), "a backwards clock does not produce a negative delta");
    CHECK(Near(time.ElapsedSeconds(), 0.25), "elapsed stays 0.25 seconds when the clock moves backwards");

    EndTest("time", failuresBefore);
}

void TestTranslation() {
    const int failuresBefore = g_failures;
    BeginTest("sdl event translation");

    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    const Xora2D::TranslatedEvent quitEvent = Xora2D::TranslateSdlEvent(quit);
    CHECK(quitEvent.event != nullptr && quitEvent.event->GetType() == Xora2D::EventType::AppQuit,
          "SDL quit becomes AppQuit");

    SDL_Event close{};
    close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    const Xora2D::TranslatedEvent closeEvent = Xora2D::TranslateSdlEvent(close);
    CHECK(closeEvent.event != nullptr && closeEvent.event->GetType() == Xora2D::EventType::WindowClose,
          "SDL window close becomes WindowClose");

    SDL_Event resize{};
    resize.type = SDL_EVENT_WINDOW_RESIZED;
    resize.window.data1 = 640;
    resize.window.data2 = 480;
    const Xora2D::TranslatedEvent resizeEvent = Xora2D::TranslateSdlEvent(resize);
    CHECK(resizeEvent.resized && resizeEvent.width == 640 && resizeEvent.height == 480,
          "SDL resize reports 640x480");
    CHECK(resizeEvent.event != nullptr && resizeEvent.event->GetType() == Xora2D::EventType::WindowResize,
          "SDL resize becomes WindowResize");

    SDL_Event keyDown{};
    keyDown.type = SDL_EVENT_KEY_DOWN;
    keyDown.key.scancode = SDL_SCANCODE_ESCAPE;
    keyDown.key.repeat = false;
    const Xora2D::TranslatedEvent keyDownEvent = Xora2D::TranslateSdlEvent(keyDown);
    const auto* pressed = dynamic_cast<Xora2D::KeyPressedEvent*>(keyDownEvent.event.get());
    if (pressed != nullptr) {
        XORA_LOG_INFO("  translated key down: {}", pressed->ToString());
    }
    CHECK(pressed != nullptr && pressed->GetKey() == Xora2D::Key::Escape && !pressed->IsRepeat(),
          "Escape key down becomes KeyPressed");

    SDL_Event keyUp{};
    keyUp.type = SDL_EVENT_KEY_UP;
    keyUp.key.scancode = SDL_SCANCODE_A;
    const Xora2D::TranslatedEvent keyUpEvent = Xora2D::TranslateSdlEvent(keyUp);
    const auto* released = dynamic_cast<Xora2D::KeyReleasedEvent*>(keyUpEvent.event.get());
    if (released != nullptr) {
        XORA_LOG_INFO("  translated key up: {}", released->ToString());
    }
    CHECK(released != nullptr && released->GetKey() == Xora2D::Key::A, "A key up becomes KeyReleased");

    SDL_Event click{};
    click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    click.button.button = SDL_BUTTON_LEFT;
    click.button.x = 15.0f;
    click.button.y = 25.0f;
    const Xora2D::TranslatedEvent clickEvent = Xora2D::TranslateSdlEvent(click);
    const auto* buttonDown = dynamic_cast<Xora2D::MouseButtonPressedEvent*>(clickEvent.event.get());
    if (buttonDown != nullptr) {
        XORA_LOG_INFO("  translated click: {}", buttonDown->ToString());
    }
    CHECK(buttonDown != nullptr && buttonDown->GetButton() == Xora2D::MouseButton::Left,
          "left mouse down becomes MouseButtonPressed");

    SDL_Event release{};
    release.type = SDL_EVENT_MOUSE_BUTTON_UP;
    release.button.button = SDL_BUTTON_RIGHT;
    release.button.x = 15.0f;
    release.button.y = 25.0f;
    const Xora2D::TranslatedEvent releaseEvent = Xora2D::TranslateSdlEvent(release);
    const auto* buttonUp = dynamic_cast<Xora2D::MouseButtonReleasedEvent*>(releaseEvent.event.get());
    if (buttonUp != nullptr) {
        XORA_LOG_INFO("  translated release: {}", buttonUp->ToString());
    }
    CHECK(buttonUp != nullptr && buttonUp->GetButton() == Xora2D::MouseButton::Right,
          "right mouse up becomes MouseButtonReleased");

    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.x = 1.0f;
    wheel.wheel.y = 2.0f;
    wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    const Xora2D::TranslatedEvent wheelEvent = Xora2D::TranslateSdlEvent(wheel);
    const auto* scrolled = dynamic_cast<Xora2D::MouseScrolledEvent*>(wheelEvent.event.get());
    if (scrolled != nullptr) {
        XORA_LOG_INFO("  translated scroll: {}", scrolled->ToString());
    }
    CHECK(scrolled != nullptr && scrolled->GetOffsetX() == -1.0f && scrolled->GetOffsetY() == -2.0f,
          "flipped scroll is normalized to -1, -2");

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 12.5f;
    motion.motion.y = 8.0f;
    const Xora2D::TranslatedEvent motionEvent = Xora2D::TranslateSdlEvent(motion);
    const auto* moved = dynamic_cast<Xora2D::MouseMovedEvent*>(motionEvent.event.get());
    if (moved != nullptr) {
        XORA_LOG_INFO("  translated motion: {}", moved->ToString());
    }
    CHECK(moved != nullptr && moved->GetX() == 12.5f && moved->GetY() == 8.0f, "mouse motion keeps 12.5, 8");

    EndTest("sdl event translation", failuresBefore);
}

void TestWindowDeliversEvents() {
    const int failuresBefore = g_failures;
    BeginTest("window event delivery");

    try {
        Xora2D::Window window(Xora2D::WindowProperties{"Xora2D Tests", 320, 240});
        window.PollEvents();

        bool sawQuit = false;
        bool sawResize = false;
        bool sawKey = false;
        bool sawClick = false;
        window.SetEventCallback([&](Xora2D::Event& event) {
            XORA_LOG_INFO("  window delivered: {}", event.ToString());
            if (event.GetType() == Xora2D::EventType::AppQuit) {
                sawQuit = true;
            }
            if (event.GetType() == Xora2D::EventType::WindowResize) {
                sawResize = true;
            }
            if (event.GetType() == Xora2D::EventType::KeyPressed) {
                sawKey = true;
            }
            if (event.GetType() == Xora2D::EventType::MouseButtonPressed) {
                sawClick = true;
            }
        });

        SDL_Event resize{};
        resize.type = SDL_EVENT_WINDOW_RESIZED;
        resize.window.data1 = 400;
        resize.window.data2 = 300;
        CHECK(SDL_PushEvent(&resize), "resize event was queued");

        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.scancode = SDL_SCANCODE_SPACE;
        CHECK(SDL_PushEvent(&key), "space key event was queued");

        SDL_Event click{};
        click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        click.button.button = SDL_BUTTON_LEFT;
        click.button.x = 4.0f;
        click.button.y = 5.0f;
        CHECK(SDL_PushEvent(&click), "mouse click event was queued");

        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        CHECK(SDL_PushEvent(&quit), "quit event was queued");

        window.PollEvents();
        CHECK(sawResize, "window callback received resize");
        CHECK(sawKey, "window callback received the space key");
        CHECK(sawClick, "window callback received the left click");
        CHECK(sawQuit, "window callback received quit");
        CHECK(window.GetWidth() == 400 && window.GetHeight() == 300, "window size updated to 400x300");

        window.SetTitle("Xora2D Tests Renamed");
        CHECK(window.GetTitle() == "Xora2D Tests Renamed", "window title updated");
    } catch (const std::exception& exception) {
        XORA_LOG_ERROR("  fail: window test threw {}", exception.what());
        ++g_failures;
    }

    EndTest("window event delivery", failuresBefore);
}

} // namespace

int RunGraphicsTests();
int RunRendererTests();
int RunSceneTests();
int RunSerializationTests();
int RunAssetTests();
int RunPhysicsTests();

int main() {
    XORA_LOG_INFO("Xora2D tests starting");
    TestLogger();
    TestEvents();
    TestTime();
    TestTranslation();
    TestWindowDeliversEvents();
    g_failures += RunGraphicsTests();
    g_failures += RunRendererTests();
    g_failures += RunSceneTests();
    g_failures += RunSerializationTests();
    g_failures += RunAssetTests();
    g_failures += RunPhysicsTests();

    if (g_failures != 0) {
        XORA_LOG_ERROR("{} test check(s) failed", g_failures);
        return EXIT_FAILURE;
    }

    XORA_LOG_INFO("All Xora2D tests passed");
    return EXIT_SUCCESS;
}
