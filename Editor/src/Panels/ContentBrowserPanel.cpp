#include "Xora2DEditor/Panels/ContentBrowserPanel.h"

#include "Xora2DEditor/Content/ContentBrowserImport.h"
#include "Xora2DEditor/Content/ContentBrowserOperations.h"
#include "Xora2DEditor/Content/ContentBrowserPaths.h"
#include "Xora2DEditor/Content/ContentBrowserQuery.h"
#include "Xora2DEditor/Content/EditorAssetAssignments.h"
#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/EditorPanel.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Scene/Components.h"

#include "Xora2D/Core/Paths.h"
#include "Xora2D/Prefab/PrefabUtility.h"
#include "Xora2D/Tilemap/Tileset.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace Xora2DEditor {
namespace {

using Clock = std::chrono::steady_clock;

constexpr ImVec4 kErrorColor{1.0f, 0.45f, 0.40f, 1.0f};
constexpr ImVec4 kWarningColor{1.0f, 0.75f, 0.35f, 1.0f};
constexpr ImVec4 kSuccessColor{0.50f, 0.85f, 0.55f, 1.0f};
constexpr const char* kResourcesLabel = "Resources";

enum class Severity : std::uint8_t { Info, Success, Warning, Error };

ImU32 KindColor(ContentItemKind kind) {
    switch (kind) {
    case ContentItemKind::Folder:
        return IM_COL32(222, 178, 82, 255);
    case ContentItemKind::Texture:
        return IM_COL32(86, 156, 214, 255);
    case ContentItemKind::Audio:
        return IM_COL32(197, 134, 192, 255);
    case ContentItemKind::Script:
        return IM_COL32(78, 201, 176, 255);
    case ContentItemKind::Scene:
        return IM_COL32(220, 120, 90, 255);
    case ContentItemKind::Animation:
        return IM_COL32(181, 206, 108, 255);
    case ContentItemKind::Prefab:
        return IM_COL32(64, 196, 255, 255);
    case ContentItemKind::Tileset:
        return IM_COL32(236, 140, 64, 255);
    case ContentItemKind::Tilemap:
        return IM_COL32(120, 200, 120, 255);
    default:
        return IM_COL32(130, 136, 150, 255);
    }
}

const char* KindBadge(const ContentBrowserEntry& entry) {
    switch (entry.Kind) {
    case ContentItemKind::Texture:
        return "IMG";
    case ContentItemKind::Audio:
        return "AUDIO";
    case ContentItemKind::Script:
        return "PY";
    case ContentItemKind::Scene:
        return "SCENE";
    case ContentItemKind::Animation:
        return "ANIM";
    case ContentItemKind::Prefab:
        return "PREFAB";
    case ContentItemKind::Tileset:
        return "TILESET";
    case ContentItemKind::Tilemap:
        return "TILEMAP";
    default:
        return "FILE";
    }
}

ImTextureID ToTextureId(std::uint32_t rendererId) {
    return static_cast<ImTextureID>(static_cast<std::uintptr_t>(rendererId));
}

// Shortens `text` with "..." so it fits in `maxWidth` pixels.
std::string Ellipsize(const std::string& text, float maxWidth) {
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
    }
    std::string result = text;
    while (!result.empty() && ImGui::CalcTextSize((result + "...").c_str()).x > maxWidth) {
        result.pop_back();
    }
    return result + "...";
}

// Root label: the project folder name while a project is open, "Resources" for the engine samples.
std::string RootLabel() {
    if (Xora2D::HasContentRootOverride()) {
        const std::string name = Xora2D::GetContentRoot().filename().string();
        if (!name.empty()) {
            return name;
        }
    }
    return kResourcesLabel;
}

std::string DisplayNameForDirectory(const std::string& directory) {
    return directory.empty() ? RootLabel() : FileNameOf(directory);
}

void DrawFolderIcon(ImDrawList* drawList, ImVec2 min, float size, ImU32 color) {
    const float pad = size * 0.14f;
    const ImVec2 bodyMin(min.x + pad, min.y + size * 0.30f);
    const ImVec2 bodyMax(min.x + size - pad, min.y + size - pad);
    drawList->AddRectFilled(ImVec2(bodyMin.x, bodyMin.y - size * 0.10f),
                            ImVec2(bodyMin.x + size * 0.32f, bodyMin.y + 2.0f), color, 3.0f);
    drawList->AddRectFilled(bodyMin, bodyMax, color, 4.0f);
    drawList->AddRectFilled(ImVec2(bodyMin.x, bodyMin.y + size * 0.08f), bodyMax,
                            IM_COL32(255, 255, 255, 28), 4.0f);
}

void DrawFileIcon(ImDrawList* drawList, ImVec2 min, float size, const ContentBrowserEntry& entry) {
    const float pad = size * 0.16f;
    const ImVec2 pageMin(min.x + pad, min.y + pad * 0.6f);
    const ImVec2 pageMax(min.x + size - pad, min.y + size - pad * 0.6f);
    drawList->AddRectFilled(pageMin, pageMax, IM_COL32(52, 56, 66, 255), 4.0f);
    drawList->AddRect(pageMin, pageMax, KindColor(entry.Kind), 4.0f, 0, 1.5f);
    if (entry.Kind == ContentItemKind::Tilemap) {
        // Distinct tilemap glyph: a 3x3 tile grid.
        const float cell = (pageMax.x - pageMin.x - 16.0f) / 3.0f;
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                const ImVec2 min(pageMin.x + 8.0f + static_cast<float>(x) * cell, pageMin.y + 8.0f + static_cast<float>(y) * cell);
                const ImU32 color = (x + y) % 2 == 0 ? IM_COL32(120, 200, 120, 255) : IM_COL32(80, 150, 90, 255);
                drawList->AddRectFilled(min, ImVec2(min.x + cell - 2.0f, min.y + cell - 2.0f), color, 1.0f);
            }
        }
    }
    if (entry.Kind == ContentItemKind::Prefab) {
        // Distinct prefab glyph: an isometric cube.
        const float cx = (pageMin.x + pageMax.x) * 0.5f;
        const float cy = pageMin.y + (pageMax.y - pageMin.y) * 0.36f;
        const float r = (pageMax.x - pageMin.x) * 0.24f;
        const ImU32 light = IM_COL32(120, 215, 255, 255);
        const ImU32 mid = IM_COL32(64, 160, 225, 255);
        const ImU32 dark = IM_COL32(36, 110, 170, 255);
        const ImVec2 top(cx, cy - r), right(cx + r, cy - r * 0.5f), left(cx - r, cy - r * 0.5f), center(cx, cy);
        const ImVec2 bottom(cx, cy + r), rightBottom(cx + r, cy + r * 0.5f), leftBottom(cx - r, cy + r * 0.5f);
        drawList->AddQuadFilled(top, right, center, left, light);
        drawList->AddQuadFilled(left, center, bottom, leftBottom, mid);
        drawList->AddQuadFilled(center, right, rightBottom, bottom, dark);
    }
    const char* badge = KindBadge(entry);
    const ImVec2 textSize = ImGui::CalcTextSize(badge);
    const ImVec2 badgeMin(pageMin.x + 4.0f, pageMax.y - textSize.y - 8.0f);
    const ImVec2 badgeMax(pageMax.x - 4.0f, pageMax.y - 4.0f);
    drawList->AddRectFilled(badgeMin, badgeMax, KindColor(entry.Kind), 3.0f);
    drawList->AddText(ImVec2((badgeMin.x + badgeMax.x - textSize.x) * 0.5f, badgeMin.y + 2.0f), IM_COL32(20, 22, 28, 255),
                      badge);
}

struct PendingMove {
    std::string Source;
    std::string DestinationDirectory;
};

class ContentBrowserPanel final : public EditorPanel {
public:
    [[nodiscard]] const char* GetTitle() const override { return "Content"; }
    [[nodiscard]] std::string GetId() const override { return "ContentBrowser"; }

    void OnAttach(EditorContext& context) override {
        m_context = &context;
        m_missingCount = SyncRegistryWithDisk(context.GetAssetRegistry()).Missing.size();
        m_lastRegistrySync = Clock::now();
    }

    void OnDetach() override {
        if (m_context != nullptr) {
            m_context->GetAudioPreview().Stop();
        }
    }

    void OnImGui(EditorContext& context) override {
        m_context = &context;
        PumpBackgroundWork(context);

        ImGui::SetNextWindowSize(ImVec2(900.0f, 320.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(GetTitle())) {
            DrawToolbar(context);
            DrawBody(context);
            DrawStatusBar(context);
            HandleKeyboard(context);
        }
        ImGui::End();

        DrawModals(context);
        ApplyPendingActions(context);
    }

private:
    // ---------------------------------------------------------------- background work
    void PumpBackgroundWork(EditorContext& context) {
        std::vector<std::filesystem::path> picked = context.GetFileDialog().ConsumeCompletedSelections();
        if (!picked.empty()) {
            context.GetImportQueue().Submit(std::move(picked), context.GetContentBrowser().GetCurrentDirectory());
        }
        if (std::string error = context.GetFileDialog().ConsumeError(); !error.empty()) {
            SetStatus(error, Severity::Error);
        }

        if (std::optional<ContentImportBatchResult> batch = context.GetImportQueue().Poll(context.GetAssetManager())) {
            OnImportBatchFinished(context, *batch);
        }
        context.GetThumbnailCache().ProcessCompleted(4);

        // Cheap periodic stat of registered files: detects deletions/restorations made outside the editor.
        const Clock::time_point now = Clock::now();
        if (now - m_lastRegistrySync > std::chrono::seconds(3)) {
            m_lastRegistrySync = now;
            const RegistrySyncReport report = SyncRegistryWithDisk(context.GetAssetRegistry());
            m_missingCount = report.Missing.size();
            if (report.NewlyMissing > 0 || report.Restored > 0) {
                context.GetDirectoryCache().InvalidateAll();
                context.GetAssetDetailsLoader().Invalidate();
                if (report.NewlyMissing > 0) {
                    SetStatus(std::to_string(report.NewlyMissing) + " asset file(s) went missing on disk", Severity::Warning);
                }
            }
        }
    }

    void OnImportBatchFinished(EditorContext& context, const ContentImportBatchResult& batch) {
        for (const std::string& directory : batch.AffectedDirectories()) {
            context.GetDirectoryCache().InvalidateDirectory(directory);
        }
        if (batch.SuccessCount() > 0 && !context.PersistAssetRegistry()) {
            SetStatus("Imported, but the asset registry could not be saved", Severity::Error);
        }
        std::size_t duplicates = 0;
        std::size_t warnings = 0;
        for (const ContentImportItemResult& item : batch.Items) {
            duplicates += item.Duplicate ? 1 : 0;
            warnings += item.Warning.empty() ? 0 : 1;
            const std::string name = item.Source.filename().string();
            if (item.Success) {
                XORA_LOG_INFO("Import '{}': {}{}", name, item.Message, item.Warning.empty() ? "" : " (" + item.Warning + ")");
            } else {
                XORA_LOG_WARN("Import '{}' failed: {}", name, item.Message);
            }
        }
        const std::size_t failed = batch.Items.size() - batch.SuccessCount();
        std::string summary = "Imported " + std::to_string(batch.SuccessCount()) + "/" + std::to_string(batch.Items.size());
        if (duplicates > 0) {
            summary += ", " + std::to_string(duplicates) + " already in project";
        }
        if (failed > 0) {
            summary += ", " + std::to_string(failed) + " failed";
        }
        SetStatus(summary, failed > 0 ? Severity::Error : (warnings > 0 ? Severity::Warning : Severity::Success));

        // Select the last imported item if it landed in the folder being shown.
        for (auto iterator = batch.Items.rbegin(); iterator != batch.Items.rend(); ++iterator) {
            if (iterator->Success &&
                ParentResourceDirectory(iterator->DestinationResourcePath) == context.GetContentBrowser().GetCurrentDirectory()) {
                ContentBrowserEntry entry{};
                entry.ResourceRelativePath = iterator->DestinationResourcePath;
                entry.Kind = ClassifyContentFile(FileNameOf(entry.ResourceRelativePath));
                if (iterator->Handle.IsValid()) {
                    entry.RegisteredHandle = iterator->Handle;
                }
                context.SelectContentItem(entry);
                break;
            }
        }
        if (failed > 0 || warnings > 0) {
            m_importReport = batch;
            m_openImportReport = true;
        }
    }

    // ---------------------------------------------------------------- toolbar
    void DrawToolbar(EditorContext& context) {
        ContentBrowserState& browser = context.GetContentBrowser();

        ImGui::BeginDisabled(!browser.CanNavigateBack());
        if (ImGui::ArrowButton("##Back", ImGuiDir_Left)) {
            browser.NavigateBack();
            m_revealCurrent = true;
        }
        ImGui::SetItemTooltip("Back (Alt+Left)");
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::BeginDisabled(!browser.CanNavigateForward());
        if (ImGui::ArrowButton("##Forward", ImGuiDir_Right)) {
            browser.NavigateForward();
            m_revealCurrent = true;
        }
        ImGui::SetItemTooltip("Forward (Alt+Right)");
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::BeginDisabled(browser.GetCurrentDirectory().empty());
        if (ImGui::ArrowButton("##Up", ImGuiDir_Up)) {
            browser.NavigateUp();
            m_revealCurrent = true;
        }
        ImGui::SetItemTooltip("Parent folder (Backspace)");
        ImGui::EndDisabled();
        ImGui::SameLine();
        DrawBreadcrumbs(context);

        // Right-aligned actions.
        const float actionsWidth = 250.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowContentRegionMax().x - actionsWidth));
        const bool importBusy = context.GetImportQueue().IsBusy() || context.GetFileDialog().HasPendingRequest();
        ImGui::BeginDisabled(importBusy || context.GetHostServices().MainWindow == nullptr);
        if (ImGui::Button("Import...")) {
            context.GetFileDialog().RequestOpenFiles(*context.GetHostServices().MainWindow, true);
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Import PNG/JPEG, WAV/MP3, Python, scene and animation files.\n"
                              "You can also drop files onto the editor window.");
        ImGui::SameLine();
        if (ImGui::Button("New Folder")) {
            OpenNewFolderModal(browser.GetCurrentDirectory());
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            Refresh(context);
        }
        ImGui::SetItemTooltip("Rescan folders and re-check registered files (F5)");

        // Second row: search, filter, sort, view.
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x * 0.30f));
        char search[256];
        std::snprintf(search, sizeof(search), "%s", browser.Query.Search.c_str());
        if (m_focusSearch) {
            ImGui::SetKeyboardFocusHere();
            m_focusSearch = false;
        }
        if (ImGui::InputTextWithHint("##Search", "Search this folder (Ctrl+F)", search, sizeof(search))) {
            browser.Query.Search = search;
        }
        if (!browser.Query.Search.empty()) {
            ImGui::SameLine(0.0f, 2.0f);
            if (ImGui::SmallButton("x##ClearSearch")) {
                browser.Query.Search.clear();
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::BeginCombo("##Filter", ContentTypeFilterLabel(browser.Query.Filter))) {
            for (int index = 0; index < static_cast<int>(ContentTypeFilter::Count); ++index) {
                const auto filter = static_cast<ContentTypeFilter>(index);
                if (ImGui::Selectable(ContentTypeFilterLabel(filter), filter == browser.Query.Filter)) {
                    browser.Query.Filter = filter;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Filter by type");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::BeginCombo("##Sort", ContentSortKeyLabel(browser.Query.SortKey))) {
            for (const ContentSortKey key :
                 {ContentSortKey::Name, ContentSortKey::Type, ContentSortKey::Modified, ContentSortKey::Size}) {
                if (ImGui::Selectable(ContentSortKeyLabel(key), key == browser.Query.SortKey)) {
                    browser.Query.SortKey = key;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Sort by");
        ImGui::SameLine(0.0f, 2.0f);
        if (ImGui::ArrowButton("##SortDirection", browser.Query.Ascending ? ImGuiDir_Up : ImGuiDir_Down)) {
            browser.Query.Ascending = !browser.Query.Ascending;
        }
        ImGui::SetItemTooltip(browser.Query.Ascending ? "Ascending" : "Descending");
        ImGui::SameLine();
        const bool grid = browser.ViewMode == ContentBrowserViewMode::Grid;
        if (ImGui::RadioButton("Grid", grid)) {
            browser.ViewMode = ContentBrowserViewMode::Grid;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("List", !grid)) {
            browser.ViewMode = ContentBrowserViewMode::List;
        }
        if (grid && ImGui::GetContentRegionAvail().x > 160.0f) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::min(120.0f, ImGui::GetContentRegionAvail().x));
            ImGui::SliderFloat("##TileSize", &browser.ThumbnailSize, 56.0f, 160.0f, "%.0f px");
            ImGui::SetItemTooltip("Thumbnail size");
        }
    }

    void DrawBreadcrumbs(EditorContext& context) {
        ContentBrowserState& browser = context.GetContentBrowser();
        const std::string current = browser.GetCurrentDirectory();
        std::vector<std::string> crumbs{std::string{}};
        for (std::size_t start = 0; start < current.size();) {
            const std::size_t slash = current.find('/', start);
            const std::size_t end = slash == std::string::npos ? current.size() : slash;
            crumbs.push_back(current.substr(0, end));
            start = end + 1;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, ImGui::GetStyle().ItemSpacing.y));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        for (std::size_t index = 0; index < crumbs.size(); ++index) {
            if (index > 0) {
                ImGui::SameLine();
                ImGui::TextDisabled(">");
                ImGui::SameLine();
            }
            ImGui::PushID(static_cast<int>(index));
            const bool last = index + 1 == crumbs.size();
            if (last) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }
            if (ImGui::Button(DisplayNameForDirectory(crumbs[index]).c_str()) && !last) {
                m_pendingNavigate = crumbs[index];
            }
            ImGui::PopStyleColor();
            AcceptMoveDrop(crumbs[index]);
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::NewLine();
    }

    // ---------------------------------------------------------------- body
    void DrawBody(EditorContext& context) {
        const float statusHeight = ImGui::GetFrameHeightWithSpacing();
        const float bodyHeight = std::max(60.0f, ImGui::GetContentRegionAvail().y - statusHeight);
        const float available = ImGui::GetContentRegionAvail().x;
        const bool showTree = available > 360.0f;
        // Clamp only what is drawn: the stored width survives transient tiny sizes (first frames, docking).
        const float treeWidth = std::clamp(m_treeWidth, 120.0f, std::max(120.0f, available * 0.5f));

        if (showTree) {
            ImGui::BeginChild("##FolderTree", ImVec2(treeWidth, bodyHeight), ImGuiChildFlags_Borders);
            DrawFolderTree(context);
            ImGui::EndChild();
            ImGui::SameLine(0.0f, 0.0f);
            // Splitter.
            ImGui::InvisibleButton("##TreeSplitter", ImVec2(6.0f, bodyHeight));
            if (ImGui::IsItemActive()) {
                m_treeWidth = std::clamp(treeWidth + ImGui::GetIO().MouseDelta.x, 120.0f, std::max(120.0f, available * 0.5f));
            }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
            ImGui::SameLine(0.0f, 0.0f);
        }

        ImGui::BeginChild("##Items", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Borders);
        DrawItems(context);
        ImGui::EndChild();
    }

    void DrawFolderTree(EditorContext& context) {
        DrawFolderNode(context, std::string{}, 0);
        m_revealCurrent = false;
    }

    void DrawFolderNode(EditorContext& context, const std::string& directory, int depth) {
        if (depth > 32) {
            return;
        }
        ContentBrowserState& browser = context.GetContentBrowser();
        const ContentBrowserDirectoryCache::Listing& listing =
            context.GetDirectoryCache().Get(directory, context.GetAssetRegistry());
        std::vector<std::string> children;
        for (const ContentBrowserEntry& entry : listing.Entries) {
            if (entry.IsFolder()) {
                children.push_back(entry.ResourceRelativePath);
            }
        }
        std::sort(children.begin(), children.end());

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;
        if (children.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }
        if (directory == browser.GetCurrentDirectory()) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        if (directory.empty() || directory == kAssetsFolder) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        if (m_revealCurrent && directory != browser.GetCurrentDirectory() &&
            IsResourcePathWithin(browser.GetCurrentDirectory(), directory)) {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }

        ImGui::PushID(directory.c_str());
        const bool open = ImGui::TreeNodeEx("##Folder", flags, "%s", DisplayNameForDirectory(directory).c_str());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
            m_pendingNavigate = directory;
        }
        AcceptMoveDrop(directory);
        if (ImGui::BeginPopupContextItem("##FolderContext")) {
            ContentBrowserEntry entry{};
            entry.Kind = ContentItemKind::Folder;
            entry.Name = DisplayNameForDirectory(directory);
            entry.ResourceRelativePath = directory;
            DrawFolderContextItems(context, entry);
            ImGui::EndPopup();
        }
        if (open) {
            for (const std::string& child : children) {
                DrawFolderNode(context, child, depth + 1);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void DrawItems(EditorContext& context) {
        ContentBrowserState& browser = context.GetContentBrowser();
        const ContentBrowserDirectoryCache::Listing& listing =
            context.GetDirectoryCache().Get(browser.GetCurrentDirectory(), context.GetAssetRegistry());

        if (!listing.ErrorMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(kErrorColor, "Cannot show '%s'", DisplayNameForDirectory(browser.GetCurrentDirectory()).c_str());
            ImGui::TextWrapped("%s", listing.ErrorMessage.c_str());
            if (ImGui::Button("Go to Assets")) {
                m_pendingNavigate = std::string(kAssetsFolder);
            }
            ImGui::SameLine();
            if (ImGui::Button("Retry")) {
                context.GetDirectoryCache().InvalidateDirectory(browser.GetCurrentDirectory());
            }
            return;
        }

        m_visibleEntries = ApplyContentQuery(listing.Entries, browser.Query);
        DrawBackgroundContextMenu(context);
        if (listing.Entries.empty()) {
            DrawCenteredHint("This folder is empty.", "Use Import... or drop files onto the editor window.");
            return;
        }
        if (m_visibleEntries.empty()) {
            DrawCenteredHint("No items match the current search or filter.", nullptr);
            if (ImGui::Button("Clear search and filter")) {
                browser.Query.Search.clear();
                browser.Query.Filter = ContentTypeFilter::All;
            }
            return;
        }

        if (browser.ViewMode == ContentBrowserViewMode::Grid) {
            DrawGrid(context);
        } else {
            DrawList(context);
        }

        // Clicking empty space clears the selection.
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
            browser.ClearSelection();
        }
    }

    static void DrawCenteredHint(const char* primary, const char* secondary) {
        ImGui::Spacing();
        ImGui::Spacing();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (width - ImGui::CalcTextSize(primary).x) * 0.5f));
        ImGui::TextDisabled("%s", primary);
        if (secondary != nullptr) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 std::max(0.0f, (width - ImGui::CalcTextSize(secondary).x) * 0.5f));
            ImGui::TextDisabled("%s", secondary);
        }
    }

    void DrawGrid(EditorContext& context) {
        ContentBrowserState& browser = context.GetContentBrowser();
        const float thumb = browser.ThumbnailSize;
        const float cellWidth = thumb + 12.0f;
        const float labelHeight = ImGui::GetTextLineHeight() + 6.0f;
        const float cellHeight = thumb + labelHeight + 8.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const int columns =
            std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (cellWidth + spacing)));

        // Only rows on screen are drawn (thumbnails are requested lazily for visible tiles).
        const int rows = static_cast<int>((m_visibleEntries.size() + static_cast<std::size_t>(columns) - 1) /
                                          static_cast<std::size_t>(columns));
        ImGuiListClipper clipper;
        clipper.Begin(rows, cellHeight + ImGui::GetStyle().ItemSpacing.y);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                for (int column = 0; column < columns; ++column) {
                    const std::size_t index = static_cast<std::size_t>(row * columns + column);
                    if (index >= m_visibleEntries.size()) {
                        break;
                    }
                    if (column > 0) {
                        ImGui::SameLine();
                    }
                    DrawTile(context, m_visibleEntries[index], thumb, cellWidth, cellHeight);
                }
            }
        }
    }

    void DrawTile(EditorContext& context, const ContentBrowserEntry& entry, float thumb, float cellWidth,
                  float cellHeight) {
        ContentBrowserState& browser = context.GetContentBrowser();
        ImGui::PushID(entry.ResourceRelativePath.c_str());
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const bool selected = browser.IsSelected(entry);
        if (ImGui::Selectable("##Tile", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(cellWidth, cellHeight))) {
            OnItemClicked(context, entry, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
        }
        DrawItemInteractions(context, entry);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 iconMin(origin.x + (cellWidth - thumb) * 0.5f, origin.y + 4.0f);
        bool drewThumbnail = false;
        if (entry.Kind == ContentItemKind::Folder) {
            DrawFolderIcon(drawList, iconMin, thumb, KindColor(entry.Kind));
            drewThumbnail = true;
        } else if ((entry.Kind == ContentItemKind::Texture || entry.Kind == ContentItemKind::Tileset) &&
                   !entry.IsMissing()) {
            // Tilesets preview their source texture (with a tile grid drawn on top below).
            std::string thumbnailKey = entry.ResourceRelativePath;
            std::filesystem::path thumbnailFile = ResolveResourceRelativePath(entry.ResourceRelativePath);
            std::filesystem::file_time_type thumbnailTime = entry.LastModified;
            const Xora2D::Tileset* tileset = nullptr;
            if (entry.Kind == ContentItemKind::Tileset) {
                thumbnailFile.clear();
                if (entry.RegisteredHandle.has_value() &&
                    context.GetAssetManager().LoadTileset(*entry.RegisteredHandle).Success) {
                    tileset = context.GetAssetManager().GetLoadedTileset(*entry.RegisteredHandle).get();
                    if (const auto* textureMeta = context.GetAssetRegistry().FindByHandle(tileset->GetSettings().Texture)) {
                        thumbnailKey = ResourcePathFromAssetRelativePath(textureMeta->RelativePath);
                        thumbnailFile = ResolveResourceRelativePath(thumbnailKey);
                        std::error_code timeError;
                        thumbnailTime = std::filesystem::last_write_time(thumbnailFile, timeError);
                    }
                }
            }
            AssetThumbnailCache::View view{};
            view.Status = AssetThumbnailCache::State::Failed;
            if (!thumbnailFile.empty()) {
                view = context.GetThumbnailCache().Request(thumbnailKey, thumbnailFile, thumbnailTime);
            }
            if (view.Status == AssetThumbnailCache::State::Ready && view.Width > 0 && view.Height > 0) {
                // Preserve aspect ratio inside the square tile.
                const float scale = std::min(thumb / static_cast<float>(view.Width), thumb / static_cast<float>(view.Height));
                const ImVec2 size(static_cast<float>(view.Width) * scale, static_cast<float>(view.Height) * scale);
                const ImVec2 min(iconMin.x + (thumb - size.x) * 0.5f, iconMin.y + (thumb - size.y) * 0.5f);
                drawList->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), IM_COL32(40, 42, 50, 255));
                drawList->AddImage(ToTextureId(view.RendererId), min, ImVec2(min.x + size.x, min.y + size.y));
                if (tileset != nullptr && view.SourceWidth > 0) {
                    // Tile grid over the preview + badge, so tilesets are distinct from plain textures.
                    const float pixel = size.x / static_cast<float>(view.SourceWidth);
                    if (pixel * static_cast<float>(tileset->GetSettings().TileWidth) >= 4.0f) {
                        for (const Xora2D::TileDefinition& tile : tileset->GetTiles()) {
                            const ImVec2 a(min.x + static_cast<float>(tile.Region.X) * pixel, min.y + static_cast<float>(tile.Region.Y) * pixel);
                            drawList->AddRect(a, ImVec2(a.x + static_cast<float>(tile.Region.Width) * pixel,
                                                        a.y + static_cast<float>(tile.Region.Height) * pixel),
                                              IM_COL32(255, 255, 255, 60));
                        }
                    }
                    const char* badge = "TILESET";
                    const ImVec2 badgeSize = ImGui::CalcTextSize(badge);
                    const ImVec2 badgeMin(iconMin.x + 2.0f, iconMin.y + thumb - badgeSize.y - 4.0f);
                    drawList->AddRectFilled(badgeMin, ImVec2(badgeMin.x + badgeSize.x + 6.0f, badgeMin.y + badgeSize.y + 2.0f),
                                            KindColor(ContentItemKind::Tileset), 3.0f);
                    drawList->AddText(ImVec2(badgeMin.x + 3.0f, badgeMin.y + 1.0f), IM_COL32(20, 22, 28, 255), badge);
                }
                drewThumbnail = true;
            } else if (view.Status == AssetThumbnailCache::State::Pending) {
                DrawFileIcon(drawList, iconMin, thumb, entry);
                drawList->AddText(ImVec2(iconMin.x + 6.0f, iconMin.y + 6.0f), IM_COL32(200, 200, 210, 160), "...");
                drewThumbnail = true;
            }
        }
        if (!drewThumbnail) {
            DrawFileIcon(drawList, iconMin, thumb, entry);
        }

        if (entry.IsMissing()) {
            drawList->AddRect(iconMin, ImVec2(iconMin.x + thumb, iconMin.y + thumb), IM_COL32(230, 90, 80, 255), 4.0f, 0,
                              2.0f);
            drawList->AddText(ImVec2(iconMin.x + 6.0f, iconMin.y + 6.0f), IM_COL32(240, 110, 100, 255), "MISSING");
        } else if (entry.Status == ContentImportStatus::NotRegistered) {
            drawList->AddCircleFilled(ImVec2(iconMin.x + thumb - 8.0f, iconMin.y + 8.0f), 5.0f, IM_COL32(240, 180, 80, 255));
        }

        const std::string label = Ellipsize(entry.Name, cellWidth - 4.0f);
        const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
        drawList->AddText(ImVec2(origin.x + (cellWidth - labelSize.x) * 0.5f, origin.y + thumb + 8.0f),
                          entry.IsMissing() ? IM_COL32(240, 110, 100, 255) : ImGui::GetColorU32(ImGuiCol_Text),
                          label.c_str());
        ImGui::PopID();
    }

    void DrawList(EditorContext& context) {
        ContentBrowserState& browser = context.GetContentBrowser();
        constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                           ImGuiTableFlags_SizingStretchProp;
        if (!ImGui::BeginTable("##ContentList", 5, kFlags)) {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch, 3.0f,
                                static_cast<ImGuiID>(ContentSortKey::Name));
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.2f, static_cast<ImGuiID>(ContentSortKey::Type));
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.8f, static_cast<ImGuiID>(ContentSortKey::Size));
        ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthStretch, 1.3f,
                                static_cast<ImGuiID>(ContentSortKey::Modified));
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsDirty) {
            if (specs->SpecsCount > 0) {
                browser.Query.SortKey = static_cast<ContentSortKey>(specs->Specs[0].ColumnUserID);
                browser.Query.Ascending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
            }
            specs->SpecsDirty = false;
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_visibleEntries.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const ContentBrowserEntry& entry = m_visibleEntries[static_cast<std::size_t>(row)];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(entry.ResourceRelativePath.c_str());
                const ImVec2 cursor = ImGui::GetCursorScreenPos();
                const float square = ImGui::GetTextLineHeight() * 0.7f;
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(cursor.x, cursor.y + square * 0.25f), ImVec2(cursor.x + square, cursor.y + square * 1.25f),
                    KindColor(entry.Kind), 2.0f);
                ImGui::SetCursorScreenPos(ImVec2(cursor.x + square + 6.0f, cursor.y));
                if (entry.IsMissing()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
                }
                if (ImGui::Selectable(entry.Name.c_str(), context.GetContentBrowser().IsSelected(entry),
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    OnItemClicked(context, entry, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
                }
                if (entry.IsMissing()) {
                    ImGui::PopStyleColor();
                }
                DrawItemInteractions(context, entry);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(ContentItemKindLabel(entry.Kind));
                ImGui::TableSetColumnIndex(2);
                if (!entry.IsFolder() && !entry.IsMissing()) {
                    ImGui::TextUnformatted(FormatFileSize(entry.FileSizeBytes).c_str());
                }
                ImGui::TableSetColumnIndex(3);
                if (!entry.IsMissing()) {
                    ImGui::TextUnformatted(FormatModified(entry.LastModified).c_str());
                }
                ImGui::TableSetColumnIndex(4);
                const char* status = ContentImportStatusLabel(entry.Status);
                if (entry.Status == ContentImportStatus::Missing) {
                    ImGui::TextColored(kErrorColor, "%s", status);
                } else if (entry.Status == ContentImportStatus::NotRegistered) {
                    ImGui::TextColored(kWarningColor, "%s", status);
                } else {
                    ImGui::TextDisabled("%s", status);
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    static std::string FormatModified(std::filesystem::file_time_type time) {
        if (time == std::filesystem::file_time_type{}) {
            return {};
        }
        const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        const std::time_t value = std::chrono::system_clock::to_time_t(system);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &value);
#else
        localtime_r(&value, &local);
#endif
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
        return buffer;
    }

    // ---------------------------------------------------------------- item interaction
    void OnItemClicked(EditorContext& context, const ContentBrowserEntry& entry, bool doubleClicked) {
        if (entry.IsFolder()) {
            if (doubleClicked) {
                m_pendingNavigate = entry.ResourceRelativePath;
            } else {
                context.GetContentBrowser().SelectPath(entry.ResourceRelativePath);
            }
            return;
        }
        context.SelectContentItem(entry);
        if (doubleClicked && entry.Kind == ContentItemKind::Scene && !entry.IsMissing()) {
            m_openScenePath = entry.ResourceRelativePath;
            m_openSceneConfirm = true;
        }
        if (doubleClicked && entry.Kind == ContentItemKind::Tileset && !entry.IsMissing() && entry.RegisteredHandle.has_value()) {
            context.RequestTilesetEditor({*entry.RegisteredHandle, {}});
        }
        if (doubleClicked && entry.Kind == ContentItemKind::Tilemap && !entry.IsMissing() &&
            entry.RegisteredHandle.has_value()) {
            const EditorSceneFileResult open = context.OpenTilemapAsset(*entry.RegisteredHandle);
            SetStatus(open.Message, open.Success ? Severity::Success : Severity::Error);
            if (open.Success) {
                context.RequestPanel("Tilemap");
            }
        }
        if (doubleClicked && entry.Kind == ContentItemKind::Prefab && !entry.IsMissing() &&
            entry.RegisteredHandle.has_value() && !context.IsPlayModeActive()) {
            const EditorSceneFileResult open = context.OpenPrefabEditor(*entry.RegisteredHandle);
            SetStatus(open.Message, open.Success ? Severity::Success : Severity::Error);
        }
    }

    void DrawItemInteractions(EditorContext& context, const ContentBrowserEntry& entry) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(entry.Name.c_str());
            ImGui::TextDisabled("%s", ContentItemKindLabel(entry.Kind));
            if (!entry.IsFolder() && !entry.IsMissing()) {
                ImGui::TextDisabled("%s", FormatFileSize(entry.FileSizeBytes).c_str());
            }
            if (entry.Status != ContentImportStatus::NotApplicable) {
                ImGui::TextDisabled("Status: %s", ContentImportStatusLabel(entry.Status));
            }
            ImGui::EndTooltip();
        }

        // Drag source: stable handle for Inspector fields + path for moving inside the browser.
        if (!IsProtectedResourcePath(entry.ResourceRelativePath) && !entry.IsMissing() &&
            entry.ResourceRelativePath.size() < sizeof(AssetDragPayload::ResourcePath) &&
            ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            AssetDragPayload payload{};
            if (entry.RegisteredHandle.has_value()) {
                payload.Handle = entry.RegisteredHandle->Value();
                payload.Type = static_cast<std::uint8_t>(entry.AssetType);
            }
            std::memcpy(payload.ResourcePath, entry.ResourceRelativePath.c_str(), entry.ResourceRelativePath.size() + 1);
            ImGui::SetDragDropPayload(kAssetDragDropPayload, &payload, sizeof(payload));
            ImGui::Text("%s", entry.Name.c_str());
            if (entry.RegisteredHandle.has_value()) {
                ImGui::TextDisabled("Drop on a folder to move, or on a matching Inspector field");
            }
            ImGui::EndDragDropSource();
        }
        if (entry.IsFolder()) {
            AcceptMoveDrop(entry.ResourceRelativePath);
        }

        if (ImGui::BeginPopupContextItem("##ItemContext")) {
            if (!context.GetContentBrowser().IsSelected(entry) && !entry.IsFolder()) {
                context.SelectContentItem(entry);
            }
            if (entry.IsFolder()) {
                DrawFolderContextItems(context, entry);
            } else {
                DrawFileContextItems(context, entry);
            }
            ImGui::EndPopup();
        }
    }

    void AcceptMoveDrop(const std::string& destinationDirectory) {
        if (!ImGui::BeginDragDropTarget()) {
            return;
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetDragDropPayload)) {
            if (payload->DataSize == static_cast<int>(sizeof(AssetDragPayload))) {
                AssetDragPayload data{};
                std::memcpy(&data, payload->Data, sizeof(data));
                data.ResourcePath[sizeof(data.ResourcePath) - 1] = '\0';
                const std::string source = data.ResourcePath;
                if (!source.empty() && source != destinationDirectory) {
                    m_pendingMove = PendingMove{source, destinationDirectory};
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    void DrawFolderContextItems(EditorContext& context, const ContentBrowserEntry& entry) {
        const bool isProtected = IsProtectedResourcePath(entry.ResourceRelativePath);
        if (ImGui::MenuItem("Open")) {
            m_pendingNavigate = entry.ResourceRelativePath;
        }
        if (ImGui::MenuItem("New Folder Inside...")) {
            OpenNewFolderModal(entry.ResourceRelativePath);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename...", "F2", false, !isProtected)) {
            OpenRenameModal(entry);
        }
        if (ImGui::MenuItem("Move To...", nullptr, false, !isProtected)) {
            OpenMoveModal(entry);
        }
        if (ImGui::MenuItem("Delete...", "Del", false, !isProtected)) {
            OpenDeleteModal(context, entry);
        }
        if (isProtected) {
            ImGui::TextDisabled("Protected project folder");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Copy Path")) {
            ImGui::SetClipboardText(entry.ResourceRelativePath.c_str());
        }
    }

    void DrawFileContextItems(EditorContext& context, const ContentBrowserEntry& entry) {
        if (entry.Kind == ContentItemKind::Scene && !entry.IsMissing() && ImGui::MenuItem("Open Scene")) {
            m_openScenePath = entry.ResourceRelativePath;
            m_openSceneConfirm = true;
        }
        if (ImGui::MenuItem("Show in Inspector")) {
            context.SelectContentItem(entry);
        }
        if (entry.Kind == ContentItemKind::Texture && entry.RegisteredHandle.has_value() && !entry.IsMissing()) {
            if (ImGui::MenuItem("Create Tileset...")) {
                context.RequestTilesetEditor({{}, *entry.RegisteredHandle});
            }
        }
        if (entry.Kind == ContentItemKind::Tileset && entry.RegisteredHandle.has_value() && !entry.IsMissing()) {
            if (ImGui::MenuItem("Edit Tileset")) {
                context.RequestTilesetEditor({*entry.RegisteredHandle, {}});
            }
        }
        if (entry.Kind == ContentItemKind::Tilemap && entry.RegisteredHandle.has_value() && !entry.IsMissing()) {
            if (ImGui::MenuItem("Open in Tilemap Editor", nullptr, false, !context.IsPlayModeActive())) {
                const EditorSceneFileResult open = context.OpenTilemapAsset(*entry.RegisteredHandle);
                SetStatus(open.Message, open.Success ? Severity::Success : Severity::Error);
                if (open.Success) {
                    context.RequestPanel("Tilemap");
                }
            }
        }
        if (entry.Kind == ContentItemKind::Prefab && entry.RegisteredHandle.has_value() && !entry.IsMissing()) {
            if (ImGui::MenuItem("Instantiate in Scene", nullptr, false, !context.IsEditingPrefab())) {
                const auto result = context.InstantiatePrefab(*entry.RegisteredHandle);
                SetStatus(result.Message, result.Success ? Severity::Success : Severity::Error);
                if (result.Success) {
                    context.SetInspectorFocus(InspectorFocus::Entity);
                }
            }
            if (ImGui::MenuItem("Open Prefab", nullptr, false, !context.IsPlayModeActive())) {
                const EditorSceneFileResult open = context.OpenPrefabEditor(*entry.RegisteredHandle);
                SetStatus(open.Message, open.Success ? Severity::Success : Severity::Error);
            }
            if (ImGui::MenuItem("Refresh Instances in Scene", nullptr, false, !context.IsEditingPrefab())) {
                std::size_t refreshed = 0;
                std::string failure;
                if (const auto scene = context.GetScene()) {
                    for (const Xora2D::UUID& instance : Xora2D::FindPrefabInstances(*scene, *entry.RegisteredHandle)) {
                        const auto result = Xora2D::RefreshPrefabInstance(*scene, instance, context.GetAssetManager());
                        refreshed += result.Success ? 1 : 0;
                        if (!result.Success) {
                            failure = result.Message;
                        }
                    }
                }
                SetStatus(failure.empty() ? "Refreshed " + std::to_string(refreshed) + " instance(s) (overrides kept)"
                                          : failure,
                          failure.empty() ? Severity::Success : Severity::Error);
            }
            ImGui::Separator();
        }
        if (entry.RegisteredHandle.has_value() && !entry.IsMissing() && entry.Kind != ContentItemKind::Prefab) {
            const Xora2D::Entity selectedEntity = context.GetSelectedEntity();
            const char* target = AssignmentTargetLabel(entry.AssetType);
            if (target != nullptr) {
                std::string label = "Assign to Selected Entity";
                if (selectedEntity.IsValid() && selectedEntity.HasComponent<Xora2D::TagComponent>()) {
                    label = "Assign to '" + selectedEntity.GetComponent<Xora2D::TagComponent>().Tag + "' (" + target + ")";
                }
                if (ImGui::MenuItem(label.c_str(), nullptr, false, selectedEntity.IsValid() && context.GetScene())) {
                    const AssetAssignmentResult result = AssignAssetToEntity(
                        *context.GetScene(), selectedEntity, *entry.RegisteredHandle, context.GetAssetRegistry());
                    SetStatus(result.Message, result.Success ? Severity::Success : Severity::Error);
                    if (result.Success) {
                        context.SetInspectorFocus(InspectorFocus::Entity);
                    }
                }
            }
        }
        ImGui::Separator();
        if (entry.Status == ContentImportStatus::NotRegistered && ImGui::MenuItem("Import (register in place)")) {
            const FilesystemOperationResult result = RegisterExistingFile(entry.ResourceRelativePath, context.GetAssetManager());
            FinishOperation(context, result, {entry.ResourceRelativePath}, "Registered '" + entry.Name + "'");
        }
        if (entry.IsMissing() && entry.RegisteredHandle.has_value() && ImGui::MenuItem("Remove from Registry")) {
            const FilesystemOperationResult result = ForgetMissingAsset(*entry.RegisteredHandle, context.GetAssetManager());
            FinishOperation(context, result, {entry.ResourceRelativePath}, "Removed missing asset '" + entry.Name + "'");
        }
        if (!entry.IsMissing()) {
            if (ImGui::MenuItem("Rename...", "F2")) {
                OpenRenameModal(entry);
            }
            if (ImGui::MenuItem("Move To...")) {
                OpenMoveModal(entry);
            }
            if (ImGui::MenuItem("Delete...", "Del")) {
                OpenDeleteModal(context, entry);
            }
        }
        ImGui::Separator();
        if (entry.RegisteredHandle.has_value() && ImGui::MenuItem("Copy Asset Handle")) {
            ImGui::SetClipboardText(entry.RegisteredHandle->ToString().c_str());
        }
        if (ImGui::MenuItem("Copy Path")) {
            ImGui::SetClipboardText(entry.ResourceRelativePath.c_str());
        }
    }

    void DrawBackgroundContextMenu(EditorContext& context) {
        if (ImGui::BeginPopupContextWindow("##ContentBackground",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            const std::string& directory = context.GetContentBrowser().GetCurrentDirectory();
            if (ImGui::MenuItem("New Folder...")) {
                OpenNewFolderModal(directory);
            }
            if (ImGui::MenuItem("Import Files...", nullptr, false,
                                context.GetHostServices().MainWindow != nullptr && !context.GetImportQueue().IsBusy())) {
                context.GetFileDialog().RequestOpenFiles(*context.GetHostServices().MainWindow, true);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Refresh", "F5")) {
                Refresh(context);
            }
            ImGui::EndPopup();
        }
    }

    void HandleKeyboard(EditorContext& context) {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput ||
            ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
            return;
        }
        ContentBrowserState& browser = context.GetContentBrowser();
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
            Refresh(context);
        }
        if ((io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_F)) {
            m_focusSearch = true;
        }
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            browser.NavigateBack();
            m_revealCurrent = true;
        }
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            browser.NavigateForward();
            m_revealCurrent = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            browser.NavigateUp();
            m_revealCurrent = true;
        }
        const ContentBrowserEntry* selected = FindSelectedVisibleEntry(context);
        if (selected == nullptr || IsProtectedResourcePath(selected->ResourceRelativePath) || selected->IsMissing()) {
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
            OpenRenameModal(*selected);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            OpenDeleteModal(context, *selected);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) && selected->IsFolder()) {
            m_pendingNavigate = selected->ResourceRelativePath;
        }
    }

    const ContentBrowserEntry* FindSelectedVisibleEntry(EditorContext& context) const {
        for (const ContentBrowserEntry& entry : m_visibleEntries) {
            if (context.GetContentBrowser().IsSelected(entry)) {
                return &entry;
            }
        }
        return nullptr;
    }

    // ---------------------------------------------------------------- status bar
    void DrawStatusBar(EditorContext& context) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%zu item%s", m_visibleEntries.size(), m_visibleEntries.size() == 1 ? "" : "s");
        if (context.GetImportQueue().IsBusy()) {
            ImGui::SameLine();
            const char spinner[] = {'|', '/', '-', '\\'};
            ImGui::TextColored(kWarningColor, "%c Importing %zu file(s)...",
                               spinner[static_cast<int>(ImGui::GetTime() * 8.0) % 4],
                               context.GetImportQueue().GetPendingFileCount());
        }
        if (m_missingCount > 0) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
            const std::string label = std::to_string(m_missingCount) + " missing asset(s)";
            if (ImGui::SmallButton(label.c_str())) {
                ImGui::OpenPopup("##MissingAssets");
            }
            ImGui::PopStyleColor();
            DrawMissingAssetsPopup(context);
        }
        if (!m_statusMessage.empty()) {
            ImGui::SameLine();
            ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            if (m_statusSeverity == Severity::Error) {
                color = kErrorColor;
            } else if (m_statusSeverity == Severity::Warning) {
                color = kWarningColor;
            } else if (m_statusSeverity == Severity::Success) {
                color = kSuccessColor;
            }
            ImGui::TextColored(color, "%s", Ellipsize(m_statusMessage, std::max(40.0f, ImGui::GetContentRegionAvail().x)).c_str());
        }
    }

    void DrawMissingAssetsPopup(EditorContext& context) {
        if (!ImGui::BeginPopup("##MissingAssets")) {
            return;
        }
        ImGui::TextUnformatted("Registered assets whose files are missing:");
        ImGui::Separator();
        for (const Xora2D::AssetMetadata& meta : context.GetAssetRegistry().ListAssets()) {
            if (meta.Available) {
                continue;
            }
            ImGui::PushID(meta.Handle.ToString().c_str());
            const std::string resourcePath = ResourcePathFromAssetRelativePath(meta.RelativePath);
            ImGui::TextColored(kErrorColor, "%s", resourcePath.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Show")) {
                m_pendingNavigate = ParentResourceDirectory(resourcePath);
                ContentBrowserEntry entry{};
                entry.ResourceRelativePath = resourcePath;
                entry.RegisteredHandle = meta.Handle;
                entry.Kind = ClassifyContentFile(FileNameOf(resourcePath));
                context.SelectContentItem(entry);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove from registry")) {
                const FilesystemOperationResult result = ForgetMissingAsset(meta.Handle, context.GetAssetManager());
                FinishOperation(context, result, {resourcePath}, "Removed missing asset from registry");
                m_missingCount = SyncRegistryWithDisk(context.GetAssetRegistry()).Missing.size();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    void SetStatus(std::string message, Severity severity) {
        m_statusMessage = std::move(message);
        m_statusSeverity = severity;
    }

    void Refresh(EditorContext& context) {
        context.GetDirectoryCache().InvalidateAll();
        context.GetThumbnailCache().Clear();
        context.GetAssetDetailsLoader().Invalidate();
        const RegistrySyncReport report = SyncRegistryWithDisk(context.GetAssetRegistry());
        m_missingCount = report.Missing.size();
        m_lastRegistrySync = Clock::now();
        SetStatus(report.Missing.empty() ? "Refreshed" : "Refreshed: " + std::to_string(report.Missing.size()) +
                                                              " registered asset(s) missing on disk",
                  report.Missing.empty() ? Severity::Info : Severity::Warning);
    }

    // Common post-processing for filesystem operations.
    void FinishOperation(EditorContext& context, const FilesystemOperationResult& result,
                         const std::vector<std::string>& affectedPaths, const std::string& successMessage) {
        for (const std::string& path : affectedPaths) {
            context.NotifyContentChanged(path);
        }
        if (!result.Success) {
            SetStatus(result.Message, Severity::Error);
            return;
        }
        if (!context.PersistAssetRegistry()) {
            SetStatus(successMessage + ", but the asset registry could not be saved", Severity::Error);
            return;
        }
        std::string message = successMessage;
        if (result.UpdatedReferenceFiles > 0) {
            message += " (updated " + std::to_string(result.UpdatedReferenceFiles) + " animation clip reference(s))";
        }
        if (!result.Warnings.empty()) {
            SetStatus(message + " - " + result.Warnings.front(), Severity::Warning);
            for (const std::string& warning : result.Warnings) {
                XORA_LOG_WARN("{}", warning);
            }
            return;
        }
        SetStatus(message, Severity::Success);
    }

    // ---------------------------------------------------------------- modals
    void OpenNewFolderModal(const std::string& parent) {
        m_newFolderParent = parent;
        std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "New Folder");
        m_openNewFolder = true;
    }

    void OpenRenameModal(const ContentBrowserEntry& entry) {
        m_targetPath = entry.ResourceRelativePath;
        m_targetName = entry.Name;
        std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "%s", entry.Name.c_str());
        m_openRename = true;
    }

    void OpenMoveModal(const ContentBrowserEntry& entry) {
        m_targetPath = entry.ResourceRelativePath;
        m_targetName = entry.Name;
        m_moveDestinations.clear();
        m_moveDestinations.push_back(std::string{});
        for (std::string& folder : ListFoldersRecursive("")) {
            if (!IsResourcePathWithin(folder, m_targetPath) && !IsResourcePathWithin(folder, kRegistryFolder)) {
                m_moveDestinations.push_back(std::move(folder));
            }
        }
        m_moveSelection = ParentResourceDirectory(entry.ResourceRelativePath);
        m_moveFilter[0] = '\0';
        m_openMove = true;
    }

    void OpenDeleteModal(EditorContext& context, const ContentBrowserEntry& entry) {
        m_targetPath = entry.ResourceRelativePath;
        m_targetName = entry.Name;
        m_deleteIsFolder = entry.IsFolder();
        const std::vector<Xora2D::AssetHandle> handles = CollectAssetsUnder(m_targetPath, context.GetAssetRegistry());
        m_deleteAssetCount = handles.size();
        m_deleteReferences = FindAssetReferences(handles, context.GetAssetRegistry(), context.GetEditScene().get());
        std::erase_if(m_deleteReferences, [&](const AssetReference& reference) {
            return IsResourcePathWithin(reference.ReferencedBy, m_targetPath);
        });
        m_deleteConfirmReferenced = false;
        m_openDelete = true;
    }

    void DrawModals(EditorContext& context) {
        DrawNewFolderModal(context);
        DrawRenameModal(context);
        DrawMoveModal(context);
        DrawDeleteModal(context);
        DrawOpenSceneModal(context);
        DrawImportReportModal();
    }

    static void CenterNextModal() {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }

    bool NameInput(const char* label, std::string& validationMessage, const std::string& directory,
                   const std::string& ignoreExisting) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(320.0f);
        const bool submitted =
            ImGui::InputText(label, m_nameBuffer, sizeof(m_nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string name = m_nameBuffer;
        validationMessage.clear();
        if (!IsValidEntryName(name)) {
            validationMessage = "Names cannot be empty, start with '.', or contain / \\ : * ? \" < > |";
        } else if (name != ignoreExisting) {
            std::error_code errorCode;
            const std::filesystem::path absolute = ResolveResourceRelativePath(directory);
            if (!absolute.empty() && std::filesystem::exists(absolute / name, errorCode)) {
                validationMessage = "An item with this name already exists";
            }
        }
        if (!validationMessage.empty()) {
            ImGui::TextColored(kErrorColor, "%s", validationMessage.c_str());
        }
        return submitted && validationMessage.empty();
    }

    void DrawNewFolderModal(EditorContext& context) {
        if (m_openNewFolder) {
            ImGui::OpenPopup("New Folder");
            m_openNewFolder = false;
        }
        CenterNextModal();
        if (!ImGui::BeginPopupModal("New Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::Text("Create a folder in '%s'", DisplayNameForDirectory(m_newFolderParent).c_str());
        std::string validation;
        const bool submitted = NameInput("##FolderName", validation, m_newFolderParent, {});
        ImGui::BeginDisabled(!validation.empty());
        if (ImGui::Button("Create", ImVec2(100.0f, 0.0f)) || submitted) {
            const FilesystemOperationResult result = CreateProjectFolder(m_newFolderParent, m_nameBuffer);
            context.NotifyContentChanged(m_newFolderParent);
            SetStatus(result.Success ? "Created folder '" + std::string(m_nameBuffer) + "'" : result.Message,
                      result.Success ? Severity::Success : Severity::Error);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void DrawRenameModal(EditorContext& context) {
        if (m_openRename) {
            ImGui::OpenPopup("Rename");
            m_openRename = false;
        }
        CenterNextModal();
        if (!ImGui::BeginPopupModal("Rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::Text("Rename '%s'", m_targetName.c_str());
        std::string validation;
        const bool submitted = NameInput("##NewName", validation, ParentResourceDirectory(m_targetPath), m_targetName);
        ImGui::TextDisabled("Asset handles are preserved, so scene references stay valid.");
        ImGui::BeginDisabled(!validation.empty());
        if (ImGui::Button("Rename", ImVec2(100.0f, 0.0f)) || submitted) {
            const std::string oldPath = m_targetPath;
            const FilesystemOperationResult result =
                RenameResourceEntry(oldPath, m_nameBuffer, context.GetAssetRegistry());
            if (result.Success) {
                context.GetContentBrowser().OnPathRelocated(oldPath, result.ResultPath);
            }
            FinishOperation(context, result, {oldPath, result.ResultPath},
                            "Renamed to '" + FileNameOf(result.ResultPath) + "'");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void DrawMoveModal(EditorContext& context) {
        if (m_openMove) {
            ImGui::OpenPopup("Move To");
            m_openMove = false;
        }
        CenterNextModal();
        ImGui::SetNextWindowSize(ImVec2(420.0f, 380.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Move To", nullptr)) {
            return;
        }
        ImGui::Text("Move '%s' to:", m_targetName.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##MoveFilter", "Filter folders", m_moveFilter, sizeof(m_moveFilter));
        ImGui::BeginChild("##MoveFolders", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 1.6f),
                          ImGuiChildFlags_Borders);
        ContentBrowserEntry filterProbe{};
        for (const std::string& folder : m_moveDestinations) {
            filterProbe.Name = folder.empty() ? RootLabel() : folder;
            if (!MatchesSearch(filterProbe, m_moveFilter)) {
                continue;
            }
            if (ImGui::Selectable(filterProbe.Name.c_str(), folder == m_moveSelection)) {
                m_moveSelection = folder;
            }
        }
        ImGui::EndChild();
        const bool sameFolder = m_moveSelection == ParentResourceDirectory(m_targetPath);
        ImGui::BeginDisabled(sameFolder);
        if (ImGui::Button("Move", ImVec2(100.0f, 0.0f))) {
            m_pendingMove = PendingMove{m_targetPath, m_moveSelection};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        (void)context;
        ImGui::EndPopup();
    }

    void DrawDeleteModal(EditorContext& context) {
        if (m_openDelete) {
            ImGui::OpenPopup("Delete");
            m_openDelete = false;
        }
        CenterNextModal();
        if (!ImGui::BeginPopupModal("Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::Text("Delete %s '%s'?", m_deleteIsFolder ? "folder" : "file", m_targetName.c_str());
        if (m_deleteIsFolder) {
            ImGui::TextDisabled("The folder and everything inside it will be removed from disk.");
        }
        if (m_deleteAssetCount > 0) {
            ImGui::Text("%zu registered asset(s) will be removed from the registry.", m_deleteAssetCount);
        }
        const bool referenced = !m_deleteReferences.empty();
        if (referenced) {
            ImGui::Spacing();
            ImGui::TextColored(kWarningColor, "Warning: still referenced (%zu reference(s)):", m_deleteReferences.size());
            ImGui::BeginChild("##References", ImVec2(520.0f, std::min(160.0f, 22.0f * static_cast<float>(m_deleteReferences.size()) + 8.0f)),
                              ImGuiChildFlags_Borders);
            for (const AssetReference& reference : m_deleteReferences) {
                ImGui::BulletText("%s  <-  %s (%s)", FileNameOf(reference.AssetPath).c_str(),
                                  reference.ReferencedBy.c_str(), reference.Detail.c_str());
            }
            ImGui::EndChild();
            ImGui::TextWrapped("Scenes referencing a deleted asset will fail to load until the reference is fixed.");
            ImGui::Checkbox("Delete anyway", &m_deleteConfirmReferenced);
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(referenced && !m_deleteConfirmReferenced);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.25f, 0.25f, 1.0f));
        if (ImGui::Button("Delete", ImVec2(100.0f, 0.0f))) {
            context.GetAudioPreview().Stop();
            DeleteOptions options{};
            options.AllowReferenced = m_deleteConfirmReferenced;
            options.OpenScene = context.GetEditScene().get();
            const std::string target = m_targetPath;
            const FilesystemOperationResult result = DeleteResourceEntry(target, context.GetAssetManager(), options);
            if (result.Success) {
                context.GetContentBrowser().OnPathDeleted(target);
            }
            FinishOperation(context, result, {target}, "Deleted '" + m_targetName + "'");
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void DrawOpenSceneModal(EditorContext& context) {
        if (m_openSceneConfirm) {
            ImGui::OpenPopup("Open Scene");
            m_openSceneConfirm = false;
        }
        CenterNextModal();
        if (!ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::Text("Open '%s'?", FileNameOf(m_openScenePath).c_str());
        ImGui::TextDisabled("The current scene will be replaced; unsaved changes are lost.");
        if (ImGui::Button("Open", ImVec2(100.0f, 0.0f))) {
            if (context.GetHostServices().OpenScene) {
                context.GetHostServices().OpenScene(ResolveResourceRelativePath(m_openScenePath));
                SetStatus("Opened scene '" + FileNameOf(m_openScenePath) + "'", Severity::Success);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void DrawImportReportModal() {
        if (m_openImportReport) {
            ImGui::OpenPopup("Import Results");
            m_openImportReport = false;
        }
        CenterNextModal();
        if (!ImGui::BeginPopupModal("Import Results", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        if (ImGui::BeginTable("##ImportResults", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV,
                              ImVec2(640.0f, 0.0f))) {
            ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableHeadersRow();
            for (const ContentImportItemResult& item : m_importReport.Items) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(item.Source.filename().string().c_str());
                ImGui::TableSetColumnIndex(1);
                if (!item.Success) {
                    ImGui::TextColored(kErrorColor, "Failed");
                } else if (!item.Warning.empty()) {
                    ImGui::TextColored(kWarningColor, "Warning");
                } else {
                    ImGui::TextColored(kSuccessColor, item.Duplicate ? "Exists" : "OK");
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s%s%s", item.Message.c_str(), item.Warning.empty() ? "" : "\n",
                                   item.Warning.c_str());
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Close", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Navigation and moves requested while drawing are applied afterwards so cached listings are never
    // invalidated while they are being iterated.
    void ApplyPendingActions(EditorContext& context) {
        if (m_pendingMove.has_value()) {
            const PendingMove move = *m_pendingMove;
            m_pendingMove.reset();
            const FilesystemOperationResult result =
                MoveResourceEntry(move.Source, move.DestinationDirectory, context.GetAssetRegistry());
            if (result.Success) {
                context.GetContentBrowser().OnPathRelocated(move.Source, result.ResultPath);
            }
            FinishOperation(context, result, {move.Source, result.ResultPath},
                            "Moved '" + FileNameOf(move.Source) + "' to '" +
                                DisplayNameForDirectory(move.DestinationDirectory) + "'");
        }
        if (m_pendingNavigate.has_value()) {
            if (context.GetContentBrowser().NavigateTo(*m_pendingNavigate)) {
                context.GetDirectoryCache().InvalidateDirectory(*m_pendingNavigate);
                m_revealCurrent = true;
            }
            m_pendingNavigate.reset();
        }
    }

    EditorContext* m_context = nullptr;
    std::vector<ContentBrowserEntry> m_visibleEntries;
    float m_treeWidth = 200.0f;
    bool m_revealCurrent = true;
    bool m_focusSearch = false;
    std::optional<std::string> m_pendingNavigate;
    std::optional<PendingMove> m_pendingMove;

    std::string m_statusMessage;
    Severity m_statusSeverity = Severity::Info;
    std::size_t m_missingCount = 0;
    Clock::time_point m_lastRegistrySync{};

    // Modal state.
    char m_nameBuffer[256]{};
    std::string m_newFolderParent;
    std::string m_targetPath;
    std::string m_targetName;
    std::vector<std::string> m_moveDestinations;
    std::string m_moveSelection;
    char m_moveFilter[128]{};
    bool m_deleteIsFolder = false;
    std::size_t m_deleteAssetCount = 0;
    std::vector<AssetReference> m_deleteReferences;
    bool m_deleteConfirmReferenced = false;
    std::string m_openScenePath;
    ContentImportBatchResult m_importReport;
    bool m_openNewFolder = false;
    bool m_openRename = false;
    bool m_openMove = false;
    bool m_openDelete = false;
    bool m_openSceneConfirm = false;
    bool m_openImportReport = false;
};

} // namespace

std::unique_ptr<EditorPanel> CreateContentBrowserPanel() {
    return std::make_unique<ContentBrowserPanel>();
}

} // namespace Xora2DEditor
