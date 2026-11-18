#include "Xora2DEditor/Panels/SceneHierarchyPanel.h"

#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/EditorPanel.h"
#include "Xora2DEditor/EditorSceneCommands.h"
#include "Xora2DEditor/PrefabEditorUI.h"

#include "Xora2D/Prefab/PrefabComponents.h"
#include "Xora2D/Prefab/PrefabUtility.h"

#include "Xora2D/Scene/Scene.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Xora2DEditor {
namespace {

struct EntityListEntry {
    Xora2D::UUID Uuid;
    std::string Name;
};

class SceneHierarchyPanel final : public EditorPanel {
public:
    [[nodiscard]] const char* GetTitle() const override { return "Hierarchy"; }
    [[nodiscard]] std::string GetId() const override { return "Hierarchy"; }

    void OnImGui(EditorContext& context) override {
        if (ImGui::Begin(GetTitle())) {
            if (context.IsPlayModeActive()) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "Runtime scene");
                ImGui::SetItemTooltip("You are editing the play-mode copy. Changes are discarded when you press Stop.");
            }
            if (context.IsEditingPrefab()) {
                ImGui::TextColored(ImVec4(0.40f, 0.78f, 1.00f, 1.0f), "Prefab: %s",
                                   context.GetEditedPrefabName().c_str());
                ImGui::SetItemTooltip("Editing an isolated copy of the prefab. Save or Cancel in the toolbar.");
            }
            DrawToolbar(context);
            ImGui::Separator();
            DrawEntityList(context);

            // Drop a prefab from the Content panel anywhere in the Hierarchy to instantiate it.
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            if (ImGui::BeginDragDropTargetCustom(window->InnerRect, window->ID)) {
                if (const auto prefab = AcceptPrefabDragDrop(context)) {
                    const auto result = context.InstantiatePrefab(*prefab);
                    ReportPrefabResult(result.Success, result.Message);
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::End();

        DrawCreatePrefabPopup(context);

        if (m_showDeleteConfirm) {
            ImGui::OpenPopup("ConfirmDeleteEntity");
            m_showDeleteConfirm = false;
        }
        if (ImGui::BeginPopupModal("ConfirmDeleteEntity", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Delete the selected entity?");
            ImGui::TextDisabled("This cannot be undone.");
            if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f))) {
                if (auto scene = context.GetScene()) {
                    const auto uuid = context.GetSelection().GetSelectedUUID();
                    if (uuid.has_value()) {
                        DestroyEntity(*scene, context.GetSelection(), *uuid);
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (m_showRenamePopup) {
            ImGui::OpenPopup("RenameEntity");
            m_showRenamePopup = false;
        }
        if (ImGui::BeginPopupModal("RenameEntity", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Name", m_renameBuffer, sizeof(m_renameBuffer));
            if (ImGui::Button("OK", ImVec2(120.0f, 0.0f))) {
                if (auto scene = context.GetScene()) {
                    Xora2D::Entity entity = context.GetSelectedEntity();
                    if (entity.IsValid()) {
                        const Xora2D::PrefabPropertySnapshot before = Xora2D::CapturePrefabProperties(entity);
                        RenameEntity(entity, m_renameBuffer);
                        RecordInstanceEdit(entity, before, context);
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

private:
    void DrawToolbar(EditorContext& context) {
        if (ImGui::Button("Create Empty Entity")) {
            if (auto scene = context.GetScene()) {
                const Xora2D::Entity created = CreateEmptyEntity(*scene);
                context.SelectEntity(created);
            }
        }
        ImGui::SameLine();
        const auto scenePtr = context.GetScene();
        const bool hasSelection = scenePtr && context.GetSelection().IsSelectionValid(*scenePtr);
        if (!hasSelection) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Delete")) {
            m_showDeleteConfirm = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Rename")) {
            const Xora2D::Entity entity = context.GetSelectedEntity();
            if (entity.IsValid()) {
                std::memset(m_renameBuffer, 0, sizeof(m_renameBuffer));
                std::strncpy(m_renameBuffer, entity.GetName().c_str(), sizeof(m_renameBuffer) - 1);
                m_showRenamePopup = true;
            }
        }
        // Prefab actions on their own row (the panel is narrow by default).
        ImGui::BeginDisabled(context.IsPlayModeActive() || context.IsEditingPrefab());
        if (ImGui::Button("Create Prefab")) {
            OpenCreatePrefab(context, context.GetSelectedEntity());
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Save the selected entity (and optionally others) as a reusable prefab asset");
        if (!hasSelection) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(context.IsEditingPrefab());
        if (ImGui::Button("Instantiate...")) {
            ImGui::OpenPopup("InstantiatePrefabMenu");
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Add an instance of a prefab (or drag a prefab from the Content panel)");
        if (ImGui::BeginPopup("InstantiatePrefabMenu")) {
            bool any = false;
            for (const Xora2D::AssetMetadata& meta : context.GetAssetRegistry().ListAssets()) {
                if (meta.Type != Xora2D::AssetType::Prefab) {
                    continue;
                }
                any = true;
                const std::string label = meta.Name + "##" + meta.Handle.ToString();
                if (ImGui::MenuItem(label.c_str(), nullptr, false, meta.Available)) {
                    const auto result = context.InstantiatePrefab(meta.Handle);
                    ReportPrefabResult(result.Success, result.Message);
                }
                ImGui::SetItemTooltip("Assets/%s%s", meta.RelativePath.c_str(), meta.Available ? "" : " (missing)");
            }
            if (!any) {
                ImGui::TextDisabled("No prefabs in this project yet");
            }
            ImGui::EndPopup();
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##HierarchyFilter", "Search entities...", m_filterBuffer, sizeof(m_filterBuffer));
    }

    void DrawEntityList(EditorContext& context) {
        const auto scene = context.GetScene();
        if (scene == nullptr) {
            ImGui::TextWrapped("No active scene.");
            return;
        }

        context.GetSelection().Sanitize(*scene);

        std::vector<EntityListEntry> entries;
        entries.reserve(scene->GetEntityCount());
        for (const Xora2D::UUID& uuid : scene->GetEntityUUIDs()) {
            const Xora2D::Entity entity = scene->FindEntityByUUID(uuid);
            if (!entity.IsValid()) {
                continue;
            }
            entries.push_back(EntityListEntry{.Uuid = uuid, .Name = entity.GetName()});
        }
        std::sort(entries.begin(), entries.end(), [](const EntityListEntry& a, const EntityListEntry& b) {
            const int nameCmp = a.Name.compare(b.Name);
            if (nameCmp != 0) {
                return nameCmp < 0;
            }
            return a.Uuid.ToString() < b.Uuid.ToString();
        });

        const std::string filter = m_filterBuffer;
        const auto matchesFilter = [&](const EntityListEntry& entry) {
            if (filter.empty()) {
                return true;
            }
            return entry.Name.find(filter) != std::string::npos;
        };

        if (entries.empty()) {
            ImGui::TextDisabled("Scene is empty. Create an entity to begin.");
            return;
        }

        bool anyVisible = false;
        for (const EntityListEntry& entry : entries) {
            if (!matchesFilter(entry)) {
                continue;
            }
            anyVisible = true;

            ImGui::PushID(entry.Uuid.ToString().c_str());
            const bool selected = context.GetSelection().IsSelected(entry.Uuid);
            const Xora2D::Entity entity = scene->FindEntityByUUID(entry.Uuid);
            const bool isInstance = Xora2D::IsPrefabInstance(entity);
            std::string label = entry.Name;
            ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            if (isInstance) {
                const auto& link = entity.GetComponent<Xora2D::PrefabInstanceComponent>();
                const Xora2D::AssetMetadata* meta = context.GetAssetRegistry().FindByHandle(link.Prefab);
                const auto prefab = context.GetAssetManager().GetLoadedPrefab(link.Prefab);
                color = ImVec4(0.40f, 0.78f, 1.00f, 1.0f);
                label += "  [prefab";
                if (meta == nullptr) {
                    color = ImVec4(0.95f, 0.45f, 0.40f, 1.0f);
                    label += ", missing";
                } else if (prefab != nullptr && link.Revision < prefab->GetRevision()) {
                    label += ", outdated";
                }
                if (!link.Overrides.empty()) {
                    label += ", " + std::to_string(link.Overrides.size()) + " override(s)";
                }
                label += "]";
            }
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            if (ImGui::Selectable(label.c_str(), selected)) {
                context.SelectEntityByUUID(entry.Uuid);
            }
            ImGui::PopStyleColor();
            if (isInstance) {
                const auto& link = entity.GetComponent<Xora2D::PrefabInstanceComponent>();
                const Xora2D::AssetMetadata* meta = context.GetAssetRegistry().FindByHandle(link.Prefab);
                ImGui::SetItemTooltip("Instance of prefab %s",
                                      meta != nullptr ? ("Assets/" + meta->RelativePath).c_str()
                                                      : (link.Prefab.ToString() + " (missing)").c_str());
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Select")) {
                    context.SelectEntityByUUID(entry.Uuid);
                }
                if (ImGui::MenuItem("Rename")) {
                    context.SelectEntityByUUID(entry.Uuid);
                    std::memset(m_renameBuffer, 0, sizeof(m_renameBuffer));
                    std::strncpy(m_renameBuffer, entry.Name.c_str(), sizeof(m_renameBuffer) - 1);
                    m_showRenamePopup = true;
                }
                if (ImGui::MenuItem("Delete")) {
                    context.SelectEntityByUUID(entry.Uuid);
                    m_showDeleteConfirm = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Create Prefab...", nullptr, false,
                                    !context.IsPlayModeActive() && !context.IsEditingPrefab())) {
                    context.SelectEntityByUUID(entry.Uuid);
                    OpenCreatePrefab(context, entity);
                }
                if (isInstance) {
                    const auto link = entity.GetComponent<Xora2D::PrefabInstanceComponent>();
                    if (ImGui::MenuItem("Open Prefab", nullptr, false, !context.IsPlayModeActive())) {
                        const auto open = context.OpenPrefabEditor(link.Prefab);
                        ReportPrefabResult(open.Success, open.Message);
                    }
                    if (ImGui::MenuItem("Unpack Prefab")) {
                        const auto unpack = Xora2D::UnpackPrefabInstance(*scene, link.InstanceId);
                        ReportPrefabResult(unpack.Success, unpack.Message);
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        if (!anyVisible) {
            ImGui::TextDisabled("No entities match the current filter.");
        }
    }

    static void RecordInstanceEdit(Xora2D::Entity entity, const Xora2D::PrefabPropertySnapshot& before,
                                   EditorContext& context) {
        if (!Xora2D::IsPrefabInstance(entity)) {
            return;
        }
        const auto handle = entity.GetComponent<Xora2D::PrefabInstanceComponent>().Prefab;
        const auto prefab = context.GetAssetManager().LoadPrefab(handle).Success
                                ? context.GetAssetManager().GetLoadedPrefab(handle)
                                : nullptr;
        (void)Xora2D::RecordPrefabEdits(entity, before, prefab.get());
    }

    void OpenCreatePrefab(EditorContext& context, const Xora2D::Entity& entity) {
        m_createIncluded.clear();
        if (entity.IsValid()) {
            m_createIncluded.insert(entity.GetUUID().ToString());
            std::memset(m_prefabName, 0, sizeof(m_prefabName));
            std::strncpy(m_prefabName, entity.GetName().c_str(), sizeof(m_prefabName) - 1);
        }
        if (m_prefabFolder[0] == '\0') {
            std::strncpy(m_prefabFolder, "Prefabs", sizeof(m_prefabFolder) - 1);
        }
        m_createError.clear();
        m_linkEntities = true;
        m_openCreatePrefab = true;
        (void)context;
    }

    void DrawCreatePrefabPopup(EditorContext& context) {
        if (m_openCreatePrefab) {
            ImGui::OpenPopup("Create Prefab");
            m_openCreatePrefab = false;
        }
        ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Create Prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        const auto scene = context.GetEditScene();
        ImGui::InputText("Name", m_prefabName, sizeof(m_prefabName));
        ImGui::InputText("Folder", m_prefabFolder, sizeof(m_prefabFolder));
        ImGui::SetItemTooltip("Folder inside Assets/ (created if needed)");
        ImGui::TextDisabled("Will create: Assets/%s%s%s.prefab.yaml", m_prefabFolder, m_prefabFolder[0] ? "/" : "",
                            m_prefabName);

        ImGui::SeparatorText("Entities in the prefab");
        ImGui::BeginChild("##PrefabEntities", ImVec2(440.0f, 160.0f), ImGuiChildFlags_Borders);
        std::vector<Xora2D::UUID> ordered;
        if (scene != nullptr) {
            const std::vector<Xora2D::UUID> ids = scene->GetEntityUUIDs();
            for (auto it = ids.rbegin(); it != ids.rend(); ++it) { // creation order
                const Xora2D::Entity entity = scene->FindEntityByUUID(*it);
                bool included = m_createIncluded.contains(it->ToString());
                const std::string label = entity.GetName() + "##" + it->ToString();
                if (ImGui::Checkbox(label.c_str(), &included)) {
                    if (included) {
                        m_createIncluded.insert(it->ToString());
                    } else {
                        m_createIncluded.erase(it->ToString());
                    }
                }
                if (included) {
                    ordered.push_back(*it);
                }
            }
        }
        ImGui::EndChild();
        ImGui::Checkbox("Replace the entities with a linked instance", &m_linkEntities);

        const std::string nameError = Xora2D::ValidatePrefabName(m_prefabName);
        const std::string& error = !nameError.empty() ? nameError : m_createError;
        if (!error.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", error.c_str());
        }
        ImGui::BeginDisabled(!nameError.empty() || ordered.empty());
        if (ImGui::Button("Create", ImVec2(120.0f, 0.0f))) {
            Xora2D::PrefabCreateOptions options{};
            options.Name = m_prefabName;
            options.AssetFolder = m_prefabFolder;
            options.LinkSourceEntities = m_linkEntities;
            const auto result = context.CreatePrefab(ordered, options);
            if (result.Success) {
                ReportPrefabResult(true, result.Message);
                ImGui::CloseCurrentPopup();
            } else {
                m_createError = result.Message;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    char m_prefabName[128]{};
    char m_prefabFolder[256]{};
    std::set<std::string> m_createIncluded;
    std::string m_createError;
    bool m_linkEntities = true;
    bool m_openCreatePrefab = false;
    char m_filterBuffer[128]{};
    char m_renameBuffer[256]{};
    bool m_showDeleteConfirm = false;
    bool m_showRenamePopup = false;
};

} // namespace

std::unique_ptr<EditorPanel> CreateSceneHierarchyPanel() {
    return std::make_unique<SceneHierarchyPanel>();
}

} // namespace Xora2DEditor