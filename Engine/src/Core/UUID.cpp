#include "Xora2D/Core/UUID.h"

#include <random>
#include <sstream>

namespace Xora2D {

UUID::UUID() = default;

UUID::UUID(std::uint64_t high, std::uint64_t low)
    : m_high(high), m_low(low) {}

UUID UUID::Generate() {
    static thread_local std::mt19937_64 randomEngine{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> distribution;
    return UUID(distribution(randomEngine), distribution(randomEngine));
}

bool UUID::IsValid() const {
    return m_high != 0 || m_low != 0;
}

std::string UUID::ToString() const {
    std::ostringstream stream;
    stream << std::hex << m_high << '-' << m_low;
    return stream.str();
}

} // namespace Xora2D