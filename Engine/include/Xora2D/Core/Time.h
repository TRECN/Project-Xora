#pragma once

#include <chrono>

namespace Xora2D {

// Frame clock owned by the application. Time is reported in seconds.
// Pass an explicit time point to Tick when a test needs a deterministic clock.
class Time {
public:
    Time();
    explicit Time(std::chrono::steady_clock::time_point start);

    double Tick();
    double Tick(std::chrono::steady_clock::time_point now);

    double DeltaSeconds() const { return m_deltaSeconds; }
    double ElapsedSeconds() const { return m_elapsedSeconds; }

private:
    std::chrono::steady_clock::time_point m_start{};
    std::chrono::steady_clock::time_point m_last{};
    double m_deltaSeconds = 0.0;
    double m_elapsedSeconds = 0.0;
};

} // namespace Xora2D
