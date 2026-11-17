#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Xora2D {

class UUID {
public:
    UUID();

    static UUID Generate();
    static std::optional<UUID> FromString(const std::string& text);
    static void RegisterUsed(const UUID& uuid);
    static bool IsRegistered(const UUID& uuid);
    static void ClearRegistry();

    std::uint64_t GetHigh() const { return m_high; }
    std::uint64_t GetLow() const { return m_low; }

    bool IsValid() const;

    std::string ToString() const;

    friend bool operator==(const UUID& left, const UUID& right) {
        return left.m_high == right.m_high && left.m_low == right.m_low;
    }

    friend bool operator!=(const UUID& left, const UUID& right) { return !(left == right); }

private:
    UUID(std::uint64_t high, std::uint64_t low);

    std::uint64_t m_high = 0;
    std::uint64_t m_low = 0;
};

} // namespace Xora2D

namespace std {

template <>
struct hash<Xora2D::UUID> {
    std::size_t operator()(const Xora2D::UUID& uuid) const noexcept {
        return std::hash<std::uint64_t>{}(uuid.GetHigh()) ^ (std::hash<std::uint64_t>{}(uuid.GetLow()) << 1U);
    }
};

} // namespace std