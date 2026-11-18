#include "Xora2DEditor/Content/ContentBrowserState.h"

namespace Xora2DEditor {
namespace {

std::string Rebase(const std::string& path, const std::string& oldPrefix, const std::string& newPrefix) {
    if (!IsResourcePathWithin(path, oldPrefix)) {
        return path;
    }
    return newPrefix + path.substr(oldPrefix.size());
}

} // namespace

ContentBrowserState::ContentBrowserState()
    : m_history{std::string(kAssetsFolder)} {}

bool ContentBrowserState::NavigateTo(const std::string& resourceRelativeDirectory) {
    const std::optional<std::string> normalized = NormalizeResourceRelativePath(resourceRelativeDirectory);
    if (!normalized.has_value()) {
        return false;
    }
    if (*normalized == GetCurrentDirectory()) {
        return true;
    }
    m_history.resize(m_historyIndex + 1);
    m_history.push_back(*normalized);
    m_historyIndex = m_history.size() - 1;
    constexpr std::size_t kMaxHistory = 64;
    if (m_history.size() > kMaxHistory) {
        m_history.erase(m_history.begin());
        --m_historyIndex;
    }
    return true;
}

bool ContentBrowserState::NavigateUp() {
    const std::string& current = GetCurrentDirectory();
    if (current.empty()) {
        return false;
    }
    return NavigateTo(ParentResourceDirectory(current));
}

bool ContentBrowserState::NavigateBack() {
    if (!CanNavigateBack()) {
        return false;
    }
    --m_historyIndex;
    return true;
}

bool ContentBrowserState::NavigateForward() {
    if (!CanNavigateForward()) {
        return false;
    }
    ++m_historyIndex;
    return true;
}

void ContentBrowserState::SelectAsset(Xora2D::AssetHandle handle, const std::string& resourceRelativePath) {
    m_selectedHandle = handle;
    m_selectedPath = resourceRelativePath;
}

void ContentBrowserState::SelectPath(const std::string& resourceRelativePath) {
    m_selectedHandle.reset();
    m_selectedPath = resourceRelativePath;
}

void ContentBrowserState::ClearSelection() {
    m_selectedHandle.reset();
    m_selectedPath.reset();
}

bool ContentBrowserState::IsSelected(const ContentBrowserEntry& entry) const {
    if (m_selectedHandle.has_value() && entry.RegisteredHandle.has_value()) {
        return *m_selectedHandle == *entry.RegisteredHandle;
    }
    return !m_selectedHandle.has_value() && m_selectedPath.has_value() && *m_selectedPath == entry.ResourceRelativePath;
}

void ContentBrowserState::OnPathRelocated(const std::string& oldResourcePath, const std::string& newResourcePath) {
    for (std::string& entry : m_history) {
        entry = Rebase(entry, oldResourcePath, newResourcePath);
    }
    if (m_selectedPath.has_value()) {
        m_selectedPath = Rebase(*m_selectedPath, oldResourcePath, newResourcePath);
    }
}

void ContentBrowserState::OnPathDeleted(const std::string& resourceRelativePath) {
    if (m_selectedPath.has_value() && IsResourcePathWithin(*m_selectedPath, resourceRelativePath)) {
        ClearSelection();
    }
    const std::string fallback = ParentResourceDirectory(resourceRelativePath);
    for (std::string& entry : m_history) {
        if (IsResourcePathWithin(entry, resourceRelativePath)) {
            entry = fallback;
        }
    }
}

} // namespace Xora2DEditor
