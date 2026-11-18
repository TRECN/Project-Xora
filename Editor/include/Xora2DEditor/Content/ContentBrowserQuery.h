#pragma once

#include "Xora2DEditor/Content/ContentBrowserPaths.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Xora2DEditor {

enum class ContentTypeFilter : std::uint8_t {
    All = 0,
    Folders,
    Textures,
    Audio,
    Scripts,
    Scenes,
    Animations,
    Prefabs,
    Tilemaps,
    Missing,
    Count,
};

enum class ContentSortKey : std::uint8_t {
    Name = 0,
    Type,
    Modified,
    Size,
};

struct ContentQuery {
    ContentTypeFilter Filter = ContentTypeFilter::All;
    std::string Search;
    ContentSortKey SortKey = ContentSortKey::Name;
    bool Ascending = true;
};

[[nodiscard]] const char* ContentTypeFilterLabel(ContentTypeFilter filter);
[[nodiscard]] const char* ContentSortKeyLabel(ContentSortKey key);

[[nodiscard]] bool MatchesTypeFilter(const ContentBrowserEntry& entry, ContentTypeFilter filter);
// Case-insensitive substring match on the entry name; whitespace-separated terms must all match.
[[nodiscard]] bool MatchesSearch(const ContentBrowserEntry& entry, const std::string& search);

// Filters and sorts a directory listing. Folders always come first; ties are broken by name so the order is
// stable across refreshes.
[[nodiscard]] std::vector<ContentBrowserEntry> ApplyContentQuery(const std::vector<ContentBrowserEntry>& entries,
                                                                 const ContentQuery& query);

} // namespace Xora2DEditor
