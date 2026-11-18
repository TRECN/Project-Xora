#include "Xora2DEditor/Content/ContentBrowserQuery.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace Xora2DEditor {
namespace {

std::string ToLower(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered;
}

int CompareNames(const ContentBrowserEntry& left, const ContentBrowserEntry& right) {
    const std::string a = ToLower(left.Name);
    const std::string b = ToLower(right.Name);
    if (a != b) {
        return a < b ? -1 : 1;
    }
    return left.Name < right.Name ? -1 : (left.Name == right.Name ? 0 : 1);
}

} // namespace

const char* ContentTypeFilterLabel(ContentTypeFilter filter) {
    switch (filter) {
    case ContentTypeFilter::All:
        return "All";
    case ContentTypeFilter::Folders:
        return "Folders";
    case ContentTypeFilter::Textures:
        return "Textures";
    case ContentTypeFilter::Audio:
        return "Audio";
    case ContentTypeFilter::Scripts:
        return "Scripts";
    case ContentTypeFilter::Scenes:
        return "Scenes";
    case ContentTypeFilter::Animations:
        return "Animations";
    case ContentTypeFilter::Missing:
        return "Missing";
    default:
        return "?";
    }
}

const char* ContentSortKeyLabel(ContentSortKey key) {
    switch (key) {
    case ContentSortKey::Name:
        return "Name";
    case ContentSortKey::Type:
        return "Type";
    case ContentSortKey::Modified:
        return "Date Modified";
    case ContentSortKey::Size:
        return "Size";
    }
    return "?";
}

bool MatchesTypeFilter(const ContentBrowserEntry& entry, ContentTypeFilter filter) {
    switch (filter) {
    case ContentTypeFilter::All:
        return true;
    case ContentTypeFilter::Folders:
        return entry.Kind == ContentItemKind::Folder;
    case ContentTypeFilter::Textures:
        return entry.Kind == ContentItemKind::Texture;
    case ContentTypeFilter::Audio:
        return entry.Kind == ContentItemKind::Audio;
    case ContentTypeFilter::Scripts:
        return entry.Kind == ContentItemKind::Script;
    case ContentTypeFilter::Scenes:
        return entry.Kind == ContentItemKind::Scene;
    case ContentTypeFilter::Animations:
        return entry.Kind == ContentItemKind::Animation;
    case ContentTypeFilter::Missing:
        return entry.IsMissing();
    default:
        return true;
    }
}

bool MatchesSearch(const ContentBrowserEntry& entry, const std::string& search) {
    const std::string name = ToLower(entry.Name);
    std::istringstream terms(ToLower(search));
    std::string term;
    while (terms >> term) {
        if (name.find(term) == std::string::npos) {
            return false;
        }
    }
    return true;
}

std::vector<ContentBrowserEntry> ApplyContentQuery(const std::vector<ContentBrowserEntry>& entries,
                                                   const ContentQuery& query) {
    std::vector<ContentBrowserEntry> result;
    result.reserve(entries.size());
    for (const ContentBrowserEntry& entry : entries) {
        if (MatchesTypeFilter(entry, query.Filter) && MatchesSearch(entry, query.Search)) {
            result.push_back(entry);
        }
    }

    std::stable_sort(result.begin(), result.end(), [&](const ContentBrowserEntry& left, const ContentBrowserEntry& right) {
        if (left.IsFolder() != right.IsFolder()) {
            return left.IsFolder();
        }
        int order = 0;
        switch (query.SortKey) {
        case ContentSortKey::Name:
            break;
        case ContentSortKey::Type:
            if (left.Kind != right.Kind) {
                order = std::string_view(ContentItemKindLabel(left.Kind)) < ContentItemKindLabel(right.Kind) ? -1 : 1;
            }
            break;
        case ContentSortKey::Modified:
            if (left.LastModified != right.LastModified) {
                order = left.LastModified < right.LastModified ? -1 : 1;
            }
            break;
        case ContentSortKey::Size:
            if (left.FileSizeBytes != right.FileSizeBytes) {
                order = left.FileSizeBytes < right.FileSizeBytes ? -1 : 1;
            }
            break;
        }
        if (order == 0) {
            order = CompareNames(left, right);
        }
        return query.Ascending ? order < 0 : order > 0;
    });
    return result;
}

} // namespace Xora2DEditor
