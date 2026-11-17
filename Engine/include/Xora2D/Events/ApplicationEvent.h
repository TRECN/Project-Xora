#pragma once

#include "Xora2D/Events/Event.h"

namespace Xora2D {

class AppQuitEvent : public Event {
public:
    XORA_EVENT_CLASS_TYPE(AppQuit)
    XORA_EVENT_CLASS_CATEGORY(EventCategory::Application)
};

} // namespace Xora2D
