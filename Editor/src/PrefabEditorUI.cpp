#include "Xora2DEditor/PrefabEditorUI.h"

#include "Xora2DEditor/Content/ContentBrowserPaths.h"
#include "Xora2DEditor/Content/EditorAssetAssignments.h"
#include "Xora2DEditor/EditorContext.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Prefab/Prefab.h"
#include "Xora2D/Prefab/PrefabComponents.h"
#include "Xora2D/Scene/Scene.h"

#include <imgui.h>

#include <cstring>
#include <vector>

namespace Xora2DEditor {

namespace {

const ImVec4 kPrefabBlue(0.40f, 0.78f, 1.00f, 1.0f);
const ImVec4 kWarning(0.95f, 0.75f, 0.30f, 1.0f);
const ImVec4 kError(0.95f, 0.45f, 0.40f, 1.0f);
const ImVec4 kOk(0.45f, 0.85f, 0.50f, 1.0f);

std::string g_lastMessage;
bool g_lastSuccess = true;

void RevealAsset(EditorContext& context, Xora2D::AssetHandle handle) {
    const Xora2D::AssetMetadata* meta = context.GetAssetRegistry().FindByHandle(handle);
    if (meta == nullptr) {
        return;
    }
    const std::string resourcePath = ResourcePathFromAssetRelativePath(meta->RelativePath);
    (void)context.GetContentBrowser().NavigateTo(ParentResourceDirectory(resourcePath));
    context.GetContentBrowser().SelectAsset(handle, resourcePath);
    context.SetInspectorFocus(InspectorFocus::Asset);
}

} // namespace

void ReportPrefabResult(bool success, const std::string& message) {
    g_lastSuccess = success;
    g_lastMessage = message;
    if (success) {
        XORA_LOG_INFO("{}", message);
    } else {
        XORA_LOG_WARN("{}", message);
    }
}

const std::string& GetLastPrefabMessage(bool* success) {
    if (success != nullptr) {
        *success = g_lastSuccess;
    }
    return g_lastMessage;
}

void PrefabEditTracker::Begin(const Xora2D::Scene* scene, const Xora2D::Entity& entity) {
    if (!Xora2D::IsPrefabInstance(entity)) {
        m_valid = false;
        return;
    }
    if (m_valid && m_scene == scene && m_entity == entity.GetUUID()) {
        return;
    }
    m_scene = scene;
    m_entity = entity.GetUUID();
    m_snapshot = Xora2D::CapturePrefabProperties(entity);
    m_valid = true;
}

void PrefabEditTracker::End(Xora2D::Entity entity, EditorContext& context, bool possibleEdit) {
    if (!m_valid || !possibleEdit || !entity.IsValid() || entity.GetUUID() != m_entity ||
        !Xora2D::IsPrefabInstance(entity)) {
        return;
    }
    const Xora2D::AssetHandle handle = entity.GetComponent<Xora2D::PrefabInstanceComponent>().Prefab;
    std::shared_ptr<const Xora2D::Prefab> prefab;
    if (context.GetAssetManager().LoadPrefab(handle).Success) {
        prefab = context.GetAssetManager().GetLoadedPrefab(handle);
    }
    (void)Xora2D::RecordPrefabEdits(entity, m_snapshot, prefab.get());
    m_snapshot = Xora2D::CapturePrefabProperties(entity);
}

bool DrawPrefabInstanceSection(Xora2D::Entity entity, EditorContext& context) {
    if (!Xora2D::IsPrefabInstance(entity)) {
        return false;
    }
    bool changed = false;
    const auto link = entity.GetComponent<Xora2D::PrefabInstanceComponent>(); // copy: actions may modify it
    Xora2D::AssetManager& assets = context.GetAssetManager();
    const Xora2D::AssetMetadata* meta = context.GetAssetRegistry().FindByHandle(link.Prefab);
    std::shared_ptr<const Xora2D::Prefab> prefab;
    if (meta != nullptr && meta->Type == Xora2D::AssetType::Prefab && assets.LoadPrefab(link.Prefab).Success) {
        prefab = assets.GetLoadedPrefab(link.Prefab);
    }
    const bool playing = context.IsPlayModeActive();
    const std::shared_ptr<Xora2D::Scene> scene = context.GetScene();
    const std::size_t instanceSize =
        scene != nullptr ? Xora2D::FindPrefabInstanceEntities(*scene, link.InstanceId).size() : 0;

    ImGui::PushID("PrefabInstance");
    ImGui::SeparatorText("Prefab Instance");
    if (meta != nullptr) {
        ImGui::TextColored(kPrefabBlue, "%s", meta->Name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Assets/%s", meta->RelativePath.c_str());
    } else {
        ImGui::TextColored(kError, "Missing prefab asset %s", link.Prefab.ToString().c_str());
        ImGui::TextWrapped("The entity keeps its data. Restore the prefab file (or Unpack) to continue.");
    }
    if (prefab != nullptr) {
        const bool sourceExists = prefab->FindEntity(link.SourceEntity).IsValid();
        if (!sourceExists) {
            ImGui::TextColored(kWarning, "This entity is no longer part of the prefab (Refresh unlinks it)");
        } else if (link.Revision < prefab->GetRevision()) {
            ImGui::TextColored(kWarning, "Outdated: instance revision %u, prefab revision %u", link.Revision,
                               prefab->GetRevision());
            ImGui::SetItemTooltip("The prefab changed since this instance was synchronized. Refresh applies the "
                                  "prefab changes while keeping this instance's overrides.");
        } else {
            ImGui::TextColored(kOk, "Up to date (revision %u)", prefab->GetRevision());
        }
    }
    if (instanceSize > 1) {
        ImGui::TextDisabled("Instance of %zu entities; Apply / Revert All / Refresh act on all of them", instanceSize);
    }

    // Actions --------------------------------------------------------------------------------------------------
    const auto action = [&](const Xora2D::PrefabResult& result) {
        ReportPrefabResult(result.Success, result.Message);
        changed = true;
    };
    ImGui::BeginDisabled(prefab == nullptr || playing);
    if (ImGui::Button("Apply to Prefab") && scene != nullptr) {
        action(Xora2D::ApplyPrefabInstance(*scene, link.InstanceId, assets));
        if (const auto* updated = context.GetAssetRegistry().FindByHandle(link.Prefab)) {
            context.NotifyContentChanged(ResourcePathFromAssetRelativePath(updated->RelativePath));
        }
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(playing ? "Disabled in Play mode: runtime changes never modify prefab assets"
                                  : "Write this instance into the prefab asset (other instances update on Refresh)");
    ImGui::SameLine();
    ImGui::BeginDisabled(prefab == nullptr || scene == nullptr);
    if (ImGui::Button("Revert All")) {
        action(Xora2D::RevertAllPrefabOverrides(*scene, link.InstanceId, assets));
    }
    ImGui::SetItemTooltip("Discard every override of this instance (restores the prefab values)");
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        action(Xora2D::RefreshPrefabInstance(*scene, link.InstanceId, assets));
    }
    ImGui::SetItemTooltip("Update to the prefab's current revision, keeping this instance's overrides");
    ImGui::EndDisabled();

    ImGui::BeginDisabled(meta == nullptr || playing);
    if (ImGui::Button("Open Prefab")) {
        const EditorSceneFileResult open = context.OpenPrefabEditor(link.Prefab);
        ReportPrefabResult(open.Success, open.Message);
        ImGui::EndDisabled();
        ImGui::PopID();
        return true; // the active scene changed
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(meta == nullptr);
    if (ImGui::Button("Select Asset")) {
        RevealAsset(context, link.Prefab);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(scene == nullptr);
    if (ImGui::Button("Unpack")) {
        action(Xora2D::UnpackPrefabInstance(*scene, link.InstanceId));
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Detach the instance from the prefab (entities and values are kept)");

    // Overrides ------------------------------------------------------------------------------------------------
    if (entity.IsValid() && Xora2D::IsPrefabInstance(entity)) {
        const std::vector<std::string> overrides = entity.GetComponent<Xora2D::PrefabInstanceComponent>().Overrides;
        if (overrides.empty()) {
            ImGui::TextDisabled("No overrides on this entity");
        } else {
            ImGui::Text("Overrides (%zu)", overrides.size());
            for (const std::string& property : overrides) {
                ImGui::PushID(property.c_str());
                const Xora2D::PrefabPropertyInfo* info = Xora2D::FindPrefabProperty(property);
                ImGui::Bullet();
                ImGui::TextColored(kPrefabBlue, "%s", info != nullptr ? info->Label : property.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", property.c_str());
                ImGui::SameLine();
                ImGui::BeginDisabled(prefab == nullptr);
                if (ImGui::SmallButton("Revert")) {
                    action(Xora2D::RevertPrefabOverride(entity, property, assets));
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
    }
    bool lastSuccess = true;
    const std::string& last = GetLastPrefabMessage(&lastSuccess);
    if (!last.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(lastSuccess ? kOk : kError, "%s", last.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();
    ImGui::PopID();
    return changed;
}

void DrawPrefabEditBar(EditorContext& context) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kPrefabBlue, "EDITING PREFAB: %s", context.GetEditedPrefabName().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(isolated copy - the open scene is untouched)");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.24f, 1.0f));
    if (ImGui::Button("Save Prefab")) {
        const EditorSceneFileResult save = context.SavePrefabEditor();
        ReportPrefabResult(save.Success, save.Message);
    }
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Write the prefab asset (new revision) and return to the scene");
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        context.ClosePrefabEditor();
        ReportPrefabResult(true, "Prefab editing cancelled; changes discarded");
    }
    ImGui::SetItemTooltip("Discard prefab changes and return to the scene");
}

std::optional<Xora2D::AssetHandle> AcceptPrefabDragDrop(EditorContext& context) {
    std::optional<Xora2D::AssetHandle> accepted;
    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetDragDropPayload);
    if (payload != nullptr && payload->DataSize == static_cast<int>(sizeof(AssetDragPayload))) {
        AssetDragPayload data{};
        std::memcpy(&data, payload->Data, sizeof(data));
        const Xora2D::AssetHandle handle(data.Handle);
        const Xora2D::AssetMetadata* meta = context.GetAssetRegistry().FindByHandle(handle);
        if (meta != nullptr && meta->Type == Xora2D::AssetType::Prefab) {
            accepted = handle;
        }
    }
    return accepted;
}

} // namespace Xora2DEditor
