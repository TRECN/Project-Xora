#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Xora2D {

// Stable 64-bit asset identifier (persisted in the asset registry and scenes).
class AssetHandle {
public:
    static constexpr std::uint64_t kInvalidValue = 0;

    constexpr AssetHandle() = default;
    explicit constexpr AssetHandle(std::uint64_t value)
        : m_value(value) {}

    [[nodiscard]] bool IsValid() const { return m_value != kInvalidValue; }
    [[nodiscard]] std::uint64_t Value() const { return m_value; }

    [[nodiscard]] std::string ToString() const;

    static std::optional<AssetHandle> FromString(const std::string& text);
    static AssetHandle StableFromRelativePath(const std::string& normalizedRelativePath);

    friend constexpr bool operator==(const AssetHandle& left, const AssetHandle& right) {
        return left.m_value == right.m_value;
    }
    friend constexpr bool operator!=(const AssetHandle& left, const AssetHandle& right) {
        return left.m_value != right.m_value;
    }

private:
    std::uint64_t m_value = kInvalidValue;
};

} // namespace Xora2D

namespace std {

template <>
struct hash<Xora2D::AssetHandle> {
    std::size_t operator()(const Xora2D::AssetHandle& handle) const noexcept {
        return std::hash<std::uint64_t>{}(handle.Value());
    }
};

} // namespace std