#include "Xora2D/Core/UUID.h"

#include <mutex>
#include <random>
#include <stdexcept>
#include <sstream>
#include <unordered_set>

namespace Xora2D {
namespace {

std::mutex g_uuidRegistryMutex;
std::unordered_set<UUID> g_registeredUuids;

} // namespace

UUID::UUID() = default;

UUID::UUID(std::uint64_t high, std::uint64_t low)
    : m_high(high), m_low(low) {}

void UUID::RegisterUsed(const UUID& uuid) {
    if (!uuid.IsValid()) {
        return;
    }
    const std::lock_guard<std::mutex> lock(g_uuidRegistryMutex);
    g_registeredUuids.insert(uuid);
}

bool UUID::IsRegistered(const UUID& uuid) {
    const std::lock_guard<std::mutex> lock(g_uuidRegistryMutex);
    return g_registeredUuids.contains(uuid);
}

void UUID::ClearRegistry() {
    const std::lock_guard<std::mutex> lock(g_uuidRegistryMutex);
    g_registeredUuids.clear();
}

UUID UUID::Generate() {
    static thread_local std::mt19937_64 randomEngine{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> distribution;
    const std::lock_guard<std::mutex> lock(g_uuidRegistryMutex);
    for (int attempt = 0; attempt < 64; ++attempt) {
        const UUID candidate(distribution(randomEngine), distribution(randomEngine));
        if (!candidate.IsValid()) {
            continue;
        }
        if (!g_registeredUuids.contains(candidate)) {
            g_registeredUuids.insert(candidate);
            return candidate;
        }
    }
    throw std::runtime_error("Failed to generate a unique UUID");
}

std::optional<UUID> UUID::FromString(const std::string& text) {
    const std::size_t dash = text.find('-');
    if (dash == std::string::npos || dash == 0 || dash + 1 >= text.size()) {
        return std::nullopt;
    }
    try {
        const std::uint64_t high = std::stoull(text.substr(0, dash), nullptr, 16);
        const std::uint64_t low = std::stoull(text.substr(dash + 1), nullptr, 16);
        const UUID uuid(high, low);
        if (!uuid.IsValid()) {
            return std::nullopt;
        }
        return uuid;
    } catch (const std::exception&) {
        return std::nullopt;
    }
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