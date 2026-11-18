#include "Xora2DEditor/Content/ContentBrowserDirectoryCache.h"

namespace Xora2DEditor {

const ContentBrowserDirectoryCache::Listing& ContentBrowserDirectoryCache::Get(
    const std::string& resourceRelativeDirectory, const Xora2D::AssetRegistry& registry, Clock::time_point now) {
    auto found = m_cache.find(resourceRelativeDirectory);
    const bool stale = found != m_cache.end() && m_autoRefreshInterval.count() > 0 &&
                       now - found->second.ScannedAt >= m_autoRefreshInterval;
    if (found != m_cache.end() && !stale) {
        return found->second;
    }
    Listing listing{};
    listing.Entries = ListDirectory(resourceRelativeDirectory, registry, listing.ErrorMessage);
    listing.ScannedAt = now;
    ++m_scanCount;
    auto [iterator, inserted] = m_cache.insert_or_assign(resourceRelativeDirectory, std::move(listing));
    (void)inserted;
    return iterator->second;
}

void ContentBrowserDirectoryCache::InvalidateDirectory(const std::string& resourceRelativeDirectory) {
    m_cache.erase(resourceRelativeDirectory);
}

void ContentBrowserDirectoryCache::InvalidateTree(const std::string& resourceRelativeDirectory) {
    for (auto iterator = m_cache.begin(); iterator != m_cache.end();) {
        if (IsResourcePathWithin(iterator->first, resourceRelativeDirectory)) {
            iterator = m_cache.erase(iterator);
        } else {
            ++iterator;
        }
    }
    m_cache.erase(ParentResourceDirectory(resourceRelativeDirectory));
}

void ContentBrowserDirectoryCache::InvalidateAll() {
    m_cache.clear();
}

} // namespace Xora2DEditor
