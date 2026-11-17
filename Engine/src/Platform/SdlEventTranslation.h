#pragma once

// Internal translation header. It is intentionally outside include/ so the
// public engine API does not depend on SDL.

#include "Xora2D/Events/Event.h"

#include <SDL3/SDL.h>

#include <memory>

namespace Xora2D {

struct TranslatedEvent {
    std::unique_ptr<Event> event;
    bool resized = false;
    int width = 0;
    int height = 0;
};

TranslatedEvent TranslateSdlEvent(const SDL_Event& sdlEvent);

} // namespace Xora2D
