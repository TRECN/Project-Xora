#pragma once

#include "Xora2DEditor/Content/ContentBrowserPaths.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace Xora2D {

class AssetRegistry;

} // namespace Xora2D

namespace Xora2DEditor {

// Caches per-directory listings so the browser never rescans the filesystem every frame. A cached listing is
// rescanned when explicitly invalidated (after editor operations) or once it is older than the auto-refresh
// interval, which is how changes made outside the editor are picked up for the folders being displayed.
class ContentBrowserDirectoryCache {
public:
    using Clock = std::chrono::steady_clock;

    struct Listing {
        std::vector<ContentBrowserEntry> Entries;
        std::string ErrorMessage;
        Clock::time_point ScannedAt{};
    };

    const Listing& Get(const std::string& resourceRelativeDirectory, const Xora2D::AssetRegistry& registry,
                       Clock::time_point now = Clock::now());

    void InvalidateDirectory(const std::string& resourceRelativeDirectory);
    // Invalidates the directory, all of its descendants and its parent (rename/move/delete of a folder).
    void InvalidateTree(const std::string& resourceRelativeDirectory);
    void InvalidateAll();

    // Zero disables automatic refresh (tests use explicit invalidation).
    void SetAutoRefreshInterval(Clock::duration interval) { m_autoRefreshInterval = interval; }
    [[nodiscard]] std::size_t GetScanCount() const { return m_scanCount; }

private:
    std::unordered_map<std::string, Listing> m_cache;
    Clock::duration m_autoRefreshInterval = std::chrono::seconds(2);
    std::size_t m_scanCount = 0;
};

} // namespace Xora2DEditor
