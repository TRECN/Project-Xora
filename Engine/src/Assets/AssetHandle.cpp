#include "Xora2D/Assets/AssetHandle.h"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace Xora2D {
namespace {

std::uint64_t Fnv1a64(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char character : text) {
        hash ^= static_cast<std::uint64_t>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

std::string AssetHandle::ToString() const {
    std::ostringstream stream;
    stream << std::hex << std::nouppercase << m_value;
    return stream.str();
}

std::optional<AssetHandle> AssetHandle::FromString(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    for (char character : text) {
        if (!std::isxdigit(static_cast<unsigned char>(character))) {
            return std::nullopt;
        }
    }
    try {
        const std::uint64_t value = std::stoull(text, nullptr, 16);
        if (value == kInvalidValue) {
            return std::nullopt;
        }
        return AssetHandle(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

AssetHandle AssetHandle::StableFromRelativePath(const std::string& normalizedRelativePath) {
    std::uint64_t hash = Fnv1a64(normalizedRelativePath);
    if (hash == kInvalidValue) {
        hash = 1;
    }
    return AssetHandle(hash);
}

} // namespace Xora2D