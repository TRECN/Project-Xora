#include "Xora2DEditor/Panels/SceneHierarchyPanel.h"

#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/EditorPanel.h"
#include "Xora2DEditor/EditorSceneCommands.h"

#include "Xora2D/Scene/Scene.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <memory>
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
            DrawToolbar(context);
            ImGui::Separator();
            DrawEntityList(context);
        }
        ImGui::End();

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
                    const Xora2D::Entity entity = context.GetSelectedEntity();
                    if (entity.IsValid()) {
                        RenameEntity(entity, m_renameBuffer);
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
        if (!hasSelection) {
            ImGui::EndDisabled();
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
            if (ImGui::Selectable(entry.Name.c_str(), selected)) {
                context.SelectEntityByUUID(entry.Uuid);
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
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        if (!anyVisible) {
            ImGui::TextDisabled("No entities match the current filter.");
        }
    }

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