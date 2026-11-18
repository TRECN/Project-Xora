#pragma once

#include "Xora2DEditor/Content/ContentBrowserQuery.h"

#include "Xora2D/Assets/AssetHandle.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Xora2DEditor {

enum class ContentBrowserViewMode : std::uint8_t { Grid = 0, List };

// Editor-only navigation, query and selection state for the content browser (never part of the runtime).
class ContentBrowserState {
public:
    ContentBrowserState();

    ContentQuery Query;
    ContentBrowserViewMode ViewMode = ContentBrowserViewMode::Grid;
    float ThumbnailSize = 88.0f;

    [[nodiscard]] const std::string& GetCurrentDirectory() const { return m_history[m_historyIndex]; }

    // Navigation truncates forward history like a web browser. Returns false for unsafe paths.
    bool NavigateTo(const std::string& resourceRelativeDirectory);
    bool NavigateUp();
    bool NavigateBack();
    bool NavigateForward();
    [[nodiscard]] bool CanNavigateBack() const { return m_historyIndex > 0; }
    [[nodiscard]] bool CanNavigateForward() const { return m_historyIndex + 1 < m_history.size(); }

    // Selection is keyed by the stable asset handle for registered assets so it survives renames/moves; plain
    // files (scenes, unregistered files) fall back to their resource-relative path.
    void SelectAsset(Xora2D::AssetHandle handle, const std::string& resourceRelativePath);
    void SelectPath(const std::string& resourceRelativePath);
    void ClearSelection();
    [[nodiscard]] bool HasSelection() const { return m_selectedHandle.has_value() || m_selectedPath.has_value(); }
    [[nodiscard]] const std::optional<Xora2D::AssetHandle>& GetSelectedHandle() const { return m_selectedHandle; }
    [[nodiscard]] const std::optional<std::string>& GetSelectedPath() const { return m_selectedPath; }
    [[nodiscard]] bool IsSelected(const ContentBrowserEntry& entry) const;

    // Called after a rename/move so path-based state follows the item.
    void OnPathRelocated(const std::string& oldResourcePath, const std::string& newResourcePath);
    // Called after a delete; drops selection/history that pointed into the deleted subtree.
    void OnPathDeleted(const std::string& resourceRelativePath);

private:
    std::vector<std::string> m_history;
    std::size_t m_historyIndex = 0;
    std::optional<Xora2D::AssetHandle> m_selectedHandle;
    std::optional<std::string> m_selectedPath;
};

} // namespace Xora2DEditor
