#include "Xora2D/Core/Time.h"

namespace Xora2D {

Time::Time()
    : Time(std::chrono::steady_clock::now()) {}

Time::Time(std::chrono::steady_clock::time_point start)
    : m_start(start), m_last(start) {}

double Time::Tick() {
    return Tick(std::chrono::steady_clock::now());
}

double Time::Tick(std::chrono::steady_clock::time_point now) {
    if (now < m_last) {
        m_deltaSeconds = 0.0;
        return m_deltaSeconds;
    }

    m_deltaSeconds = std::chrono::duration<double>(now - m_last).count();
    m_elapsedSeconds = std::chrono::duration<double>(now - m_start).count();
    m_last = now;
    return m_deltaSeconds;
}

} // namespace Xora2D
