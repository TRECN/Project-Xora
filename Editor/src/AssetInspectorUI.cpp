#include "Xora2DEditor/AssetInspectorUI.h"

#include "Xora2DEditor/Content/AssetPreview.h"
#include "Xora2DEditor/Content/ContentBrowserOperations.h"
#include "Xora2DEditor/Content/ContentBrowserPaths.h"
#include "Xora2DEditor/Content/EditorAssetAssignments.h"
#include "Xora2DEditor/EditorContext.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Renderer/Texture2D.h"
#include "Xora2D/Scene/Components.h"

#include "Xora2D/Prefab/PrefabUtility.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <string>

namespace Xora2DEditor {
namespace {

struct InspectorUiState {
    std::string Message;
    bool MessageIsError = false;
    std::string MessageKey; // selection the message belongs to
    bool AnimationPlaying = true;
    double AnimationStartTime = 0.0;
    std::string AnimationKey;
    bool ConfirmOpenScene = false;
};

InspectorUiState& UiState() {
    static InspectorUiState state;
    return state;
}

void SetMessage(const std::string& key, std::string message, bool isError) {
    InspectorUiState& state = UiState();
    state.MessageKey = key;
    state.Message = std::move(message);
    state.MessageIsError = isError;
}

ImTextureID ToTextureId(std::uint32_t rendererId) {
    return static_cast<ImTextureID>(static_cast<std::uintptr_t>(rendererId));
}

std::string FormatTime(std::filesystem::file_time_type time) {
    if (time == std::filesystem::file_time_type{}) {
        return "-";
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
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
    return buffer;
}

void PropertyRow(const char* label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextWrapped("%s", value.c_str());
}

bool BeginProperties(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        return false;
    }
    ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

// Fits (width x height) into a box no wider than the panel and no larger than maxSide, keeping aspect ratio.
ImVec2 FitPreview(float width, float height, float maxSide) {
    const float available = std::max(32.0f, std::min(ImGui::GetContentRegionAvail().x, maxSide));
    const float scale = std::min(available / std::max(width, 1.0f), available / std::max(height, 1.0f));
    return ImVec2(width * scale, height * scale);
}

void DrawCheckerboard(ImVec2 min, ImVec2 size) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float cell = 8.0f;
    drawList->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), IM_COL32(48, 50, 56, 255));
    for (float y = 0; y < size.y; y += cell) {
        for (float x = (static_cast<int>(y / cell) % 2) * cell; x < size.x; x += cell * 2) {
            drawList->AddRectFilled(ImVec2(min.x + x, min.y + y),
                                    ImVec2(min.x + std::min(x + cell, size.x), min.y + std::min(y + cell, size.y)),
                                    IM_COL32(64, 66, 74, 255));
        }
    }
}

void DrawTextureSection(EditorContext& context, const AssetDetails& details) {
    ImGui::SeparatorText("Texture");
    if (BeginProperties("TextureProps")) {
        if (details.Texture) {
            PropertyRow("Dimensions", std::to_string(details.Texture->Width) + " x " + std::to_string(details.Texture->Height));
            PropertyRow("Format", details.Texture->Format);
        }
        ImGui::EndTable();
    }
    const std::filesystem::path file = ResolveResourceRelativePath(details.ResourcePath);
    const AssetThumbnailCache::View thumbnail =
        context.GetThumbnailCache().Request(details.ResourcePath, file, details.LastModified);
    if (thumbnail.Status == AssetThumbnailCache::State::Ready) {
        const ImVec2 size = FitPreview(static_cast<float>(thumbnail.Width), static_cast<float>(thumbnail.Height), 256.0f);
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        DrawCheckerboard(cursor, size);
        ImGui::Image(ToTextureId(thumbnail.RendererId), size);
    } else if (thumbnail.Status == AssetThumbnailCache::State::Failed) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Preview unavailable: %s", thumbnail.Error.c_str());
    } else {
        ImGui::TextDisabled("Generating preview...");
    }
}

void DrawAudioSection(EditorContext& context, const AssetDetails& details) {
    ImGui::SeparatorText("Audio");
    if (details.Audio && BeginProperties("AudioProps")) {
        char duration[32];
        std::snprintf(duration, sizeof(duration), "%.2f s", details.Audio->DurationSeconds);
        PropertyRow("Duration", duration);
        PropertyRow("Format", details.Audio->Format);
        PropertyRow("Sample rate", std::to_string(details.Audio->SampleRate) + " Hz");
        PropertyRow("Channels", details.Audio->Channels == 1 ? "Mono" : (details.Audio->Channels == 2 ? "Stereo"
                                                                                                  : std::to_string(details.Audio->Channels)));
        ImGui::EndTable();
    }
    if (!details.Metadata) {
        ImGui::TextDisabled("Import this file to preview it.");
        return;
    }
    AudioPreviewPlayer& player = context.GetAudioPreview();
    const Xora2D::AssetHandle handle = details.Metadata->Handle;
    const bool previewingThis = player.IsPreviewing(handle);
    const bool paused = previewingThis && player.GetState() == AudioPreviewPlayer::State::Paused;
    if (!previewingThis || paused) {
        if (ImGui::Button(paused ? "Resume" : "Play")) {
            if (paused) {
                player.Resume();
            } else {
                std::string error;
                if (!player.Play(handle, context.GetAssetManager(), error)) {
                    SetMessage(details.ResourcePath, "Preview failed: " + error, true);
                }
            }
        }
    } else if (ImGui::Button("Pause")) {
        player.Pause();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!previewingThis);
    if (ImGui::Button("Stop")) {
        player.Stop();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(previewingThis ? (paused ? "Paused" : "Playing (editor preview)") : "Stopped");
}

void DrawScriptSection(const AssetDetails& details) {
    ImGui::SeparatorText("Python Script");
#if XORA2D_SCRIPTING_ENABLED
    ImGui::TextUnformatted("Scripting: enabled in this build");
#else
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "Scripting: disabled in this build");
#endif
    if (!details.Script) {
        return;
    }
    ImGui::Text("%zu lines%s", details.Script->LineCount, details.Script->PreviewTruncated ? " (preview truncated)" : "");
    ImGui::TextDisabled("Read-only preview (the script is not executed)");
    std::string& text = const_cast<std::string&>(details.Script->Preview);
    ImGui::InputTextMultiline("##ScriptPreview", text.data(), text.size() + 1,
                              ImVec2(-1.0f, std::max(160.0f, ImGui::GetContentRegionAvail().y - 60.0f)),
                              ImGuiInputTextFlags_ReadOnly);
}

void DrawSceneSection(EditorContext& context, const AssetDetails& details) {
    ImGui::SeparatorText("Scene");
    if (details.Scene && BeginProperties("SceneProps")) {
        PropertyRow("Scene name", details.Scene->SceneName.empty() ? "-" : details.Scene->SceneName);
        PropertyRow("Entities", std::to_string(details.Scene->EntityCount));
        ImGui::EndTable();
    }
    InspectorUiState& state = UiState();
    if (ImGui::Button("Open Scene")) {
        state.ConfirmOpenScene = true;
    }
    if (state.ConfirmOpenScene) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "Replace the open scene? Unsaved changes will be lost.");
        if (ImGui::Button("Open")) {
            state.ConfirmOpenScene = false;
            if (context.GetHostServices().OpenScene) {
                context.GetHostServices().OpenScene(ResolveResourceRelativePath(details.ResourcePath));
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            state.ConfirmOpenScene = false;
        }
    }
}

void DrawPrefabSection(EditorContext& context, const AssetDetails& details) {
    ImGui::SeparatorText("Prefab");
    if (details.Prefab && BeginProperties("PrefabProps")) {
        const PrefabDetails& prefab = *details.Prefab;
        PropertyRow("Prefab name", prefab.PrefabName.empty() ? "-" : prefab.PrefabName);
        PropertyRow("Revision", std::to_string(prefab.Revision));
        PropertyRow("Format version", std::to_string(prefab.FormatVersion));
        PropertyRow("Entities", std::to_string(prefab.EntityCount));
        std::string names;
        for (const std::string& name : prefab.EntityNames) {
            names += (names.empty() ? "" : ", ") + name;
        }
        PropertyRow("Contents", names.empty() ? "-" : names);
        std::string components;
        for (const std::string& component : prefab.Components) {
            if (component != "TagComponent" && component != "TransformComponent") {
                components += (components.empty() ? "" : ", ") + component;
            }
        }
        PropertyRow("Components", components.empty() ? "Transform only" : components);
        ImGui::EndTable();
        for (const std::string& missing : prefab.MissingReferences) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "Missing asset: %s", missing.c_str());
        }
    }
    if (!details.Metadata || !details.Error.empty()) {
        ImGui::TextDisabled(!details.Metadata ? "Not registered: Refresh the Content panel to register it."
                                              : "The prefab is invalid and cannot be instantiated.");
        return;
    }
    const Xora2D::AssetHandle handle = details.Metadata->Handle;
    InspectorUiState& state = UiState();
    if (const auto scene = context.GetScene()) {
        const std::size_t instances = Xora2D::FindPrefabInstances(*scene, handle).size();
        ImGui::TextDisabled("%zu instance(s) in the %s", instances,
                            context.IsPlayModeActive() ? "runtime scene" : "open scene");
    }
    ImGui::BeginDisabled(context.IsEditingPrefab());
    if (ImGui::Button("Instantiate")) {
        const auto result = context.InstantiatePrefab(handle);
        state.Message = result.Message;
        state.MessageIsError = !result.Success;
        state.MessageKey = details.ResourcePath;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(context.IsPlayModeActive());
    if (ImGui::Button("Open Prefab")) {
        const auto open = context.OpenPrefabEditor(handle);
        state.Message = open.Message;
        state.MessageIsError = !open.Success;
        state.MessageKey = details.ResourcePath;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(context.IsEditingPrefab());
    if (ImGui::Button("Refresh Instances")) {
        std::size_t refreshed = 0;
        if (const auto scene = context.GetScene()) {
            for (const Xora2D::UUID& instance : Xora2D::FindPrefabInstances(*scene, handle)) {
                refreshed += Xora2D::RefreshPrefabInstance(*scene, instance, context.GetAssetManager()).Success ? 1 : 0;
            }
        }
        state.Message = "Refreshed " + std::to_string(refreshed) + " instance(s); overrides were kept";
        state.MessageIsError = false;
        state.MessageKey = details.ResourcePath;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Explicitly update instances in the open scene to this prefab (overrides are kept)");
}

void DrawAnimationSection(EditorContext& context, const AssetDetails& details) {
    ImGui::SeparatorText("Animation Clip");
    if (!details.Animation) {
        return;
    }
    const AnimationDetails& animation = *details.Animation;
    if (BeginProperties("AnimProps")) {
        PropertyRow("Clip name", animation.ClipName);
        PropertyRow("Frames", std::to_string(animation.FrameCount));
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%.2f s", animation.TotalDurationSeconds);
        PropertyRow("Duration", buffer);
        std::snprintf(buffer, sizeof(buffer), "%.2fx", animation.PlaybackSpeed);
        PropertyRow("Speed", buffer);
        PropertyRow("Loop", animation.Loop ? "Yes" : "No");
        PropertyRow("Texture", animation.TexturePath.empty() ? animation.TextureHandle.ToString() : animation.TexturePath);
        ImGui::EndTable();
    }

    if (animation.FrameCount == 0 || !animation.TextureHandle.IsValid()) {
        return;
    }
    Xora2D::AssetManager& assets = context.GetAssetManager();
    if (!assets.GetRegistry().Contains(animation.TextureHandle) || !assets.LoadTexture(animation.TextureHandle).Success) {
        ImGui::TextDisabled("Texture not available for preview.");
        return;
    }
    const std::shared_ptr<Xora2D::Texture2D> texture = assets.GetLoadedTexture(animation.TextureHandle);
    if (texture == nullptr) {
        return;
    }

    InspectorUiState& state = UiState();
    if (state.AnimationKey != details.ResourcePath) {
        state.AnimationKey = details.ResourcePath;
        state.AnimationStartTime = ImGui::GetTime();
        state.AnimationPlaying = true;
    }
    const float speed = std::max(animation.PlaybackSpeed, 0.01f);
    double elapsed = state.AnimationPlaying ? (ImGui::GetTime() - state.AnimationStartTime) * speed : 0.0;
    const double total = std::max(static_cast<double>(animation.TotalDurationSeconds), 0.0001);
    elapsed = animation.Loop ? std::fmod(elapsed, total) : std::min(elapsed, total - 0.0001);
    std::size_t frame = 0;
    for (double accumulated = 0.0; frame + 1 < animation.FrameCount; ++frame) {
        accumulated += animation.FrameDurations[frame];
        if (elapsed < accumulated) {
            break;
        }
    }

    const auto& uv = animation.FrameUVs[frame];
    const float frameWidth = std::abs(uv[2] - uv[0]) * static_cast<float>(texture->GetWidth());
    const float frameHeight = std::abs(uv[3] - uv[1]) * static_cast<float>(texture->GetHeight());
    const ImVec2 size = FitPreview(frameWidth, frameHeight, 160.0f);
    DrawCheckerboard(ImGui::GetCursorScreenPos(), size);
    // Engine textures are stored bottom-up (OpenGL); frame UVs use v0 = bottom, so flip for ImGui's top-left origin.
    ImGui::Image(ToTextureId(texture->GetRendererId()), size, ImVec2(uv[0], uv[3]), ImVec2(uv[2], uv[1]));
    ImGui::Text("Frame %zu / %zu", frame + 1, animation.FrameCount);
    ImGui::SameLine();
    if (ImGui::SmallButton(state.AnimationPlaying ? "Pause" : "Play")) {
        state.AnimationPlaying = !state.AnimationPlaying;
        state.AnimationStartTime = ImGui::GetTime();
    }
}

void DrawAssignment(EditorContext& context, const AssetDetails& details) {
    if (!details.Metadata) {
        return;
    }
    const char* target = AssignmentTargetLabel(details.Metadata->Type);
    if (target == nullptr) {
        return;
    }
    ImGui::SeparatorText("Assign");
    const Xora2D::Entity entity = context.GetSelectedEntity();
    if (!entity.IsValid() || context.GetScene() == nullptr) {
        ImGui::TextDisabled("Select an entity in the Hierarchy to assign this asset,");
        ImGui::TextDisabled("or drag it onto a matching Inspector field.");
        return;
    }
    const std::string entityName =
        entity.HasComponent<Xora2D::TagComponent>() ? entity.GetComponent<Xora2D::TagComponent>().Tag : "Entity";
    const std::string label = std::string("Assign to '") + entityName + "' (" + target + ")";
    ImGui::BeginDisabled(!details.Metadata->Available);
    if (ImGui::Button(label.c_str())) {
        const AssetAssignmentResult result =
            AssignAssetToEntity(*context.GetScene(), entity, details.Metadata->Handle, context.GetAssetRegistry());
        SetMessage(details.ResourcePath, result.Message, !result.Success);
        if (result.Success) {
            // Show the updated component immediately.
            context.SetInspectorFocus(InspectorFocus::Entity);
        }
    }
    ImGui::EndDisabled();
}

void DrawRegistryActions(EditorContext& context, const AssetDetails& details) {
    if (details.ExistsOnDisk && !details.Metadata && AssetTypeForKind(details.Kind) != Xora2D::AssetType::Unknown &&
        AssetRelativePathFromResourcePath(details.ResourcePath).has_value()) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "This file is not in the asset registry.");
        if (ImGui::Button("Import (register in place)")) {
            const FilesystemOperationResult result = RegisterExistingFile(details.ResourcePath, context.GetAssetManager());
            SetMessage(details.ResourcePath, result.Success ? "Registered" : result.Message, !result.Success);
            if (result.Success) {
                (void)context.PersistAssetRegistry();
                context.NotifyContentChanged(details.ResourcePath);
            }
        }
    }
    if (details.Metadata && !details.ExistsOnDisk) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "The file for this asset is missing.");
        ImGui::TextWrapped("Restore the file at its original path (or move it back) and press Refresh, or remove "
                           "the registry entry. Scenes that reference it will fail to load until resolved.");
        if (ImGui::Button("Remove from registry")) {
            const FilesystemOperationResult result = ForgetMissingAsset(details.Metadata->Handle, context.GetAssetManager());
            SetMessage(details.ResourcePath, result.Success ? "Registry entry removed" : result.Message, !result.Success);
            if (result.Success) {
                (void)context.PersistAssetRegistry();
                context.NotifyContentChanged(details.ResourcePath);
                context.GetContentBrowser().ClearSelection();
            }
        }
    }
}

} // namespace

void DrawAssetInspector(EditorContext& context) {
    ContentBrowserState& browser = context.GetContentBrowser();
    std::string resourcePath;
    if (browser.GetSelectedHandle().has_value()) {
        // Resolve through the registry so the inspector follows the asset across renames/moves.
        if (const Xora2D::AssetMetadata* meta = context.GetAssetRegistry().FindByHandle(*browser.GetSelectedHandle())) {
            resourcePath = ResourcePathFromAssetRelativePath(meta->RelativePath);
        }
    } else if (browser.GetSelectedPath().has_value()) {
        resourcePath = *browser.GetSelectedPath();
    }
    if (resourcePath.empty()) {
        ImGui::TextWrapped("Select an asset in the Content Browser to inspect it.");
        return;
    }

    std::error_code errorCode;
    const std::filesystem::path file = ResolveResourceRelativePath(resourcePath);
    const std::filesystem::file_time_type modified =
        file.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(file, errorCode);
    const AssetDetails* details =
        context.GetAssetDetailsLoader().Request(resourcePath, errorCode ? std::filesystem::file_time_type{} : modified,
                                                context.GetAssetRegistry().ListAssets());

    const std::string title = details && details->Metadata ? details->Metadata->Name : FileNameOf(resourcePath);
    ImGui::TextUnformatted(title.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", ContentItemKindLabel(ClassifyContentFile(FileNameOf(resourcePath))));
    ImGui::Separator();
    if (details == nullptr) {
        ImGui::TextDisabled("Loading asset details...");
        return;
    }

    if (BeginProperties("AssetProps")) {
        PropertyRow("File", details->FileName);
        PropertyRow("Path", details->ResourcePath);
        PropertyRow("Type", details->Metadata ? ContentItemKindLabel(details->Kind) : ContentItemKindLabel(details->Kind));
        if (details->Metadata) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Handle");
            ImGui::TableSetColumnIndex(1);
            const std::string handleText = details->Metadata->Handle.ToString();
            ImGui::TextUnformatted(handleText.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) {
                ImGui::SetClipboardText(handleText.c_str());
            }
        }
        PropertyRow("Size", details->ExistsOnDisk ? FormatFileSize(details->FileSizeBytes) : "-");
        PropertyRow("Modified", FormatTime(details->LastModified));
        std::string status;
        if (details->Kind == ContentItemKind::Scene) {
            status = "Scene file (referenced by path)";
        } else if (details->Metadata) {
            status = details->ExistsOnDisk ? "Imported" : "Missing on disk";
        } else if (AssetTypeForKind(details->Kind) != Xora2D::AssetType::Unknown) {
            status = "Not imported";
        } else {
            status = "Not an engine asset";
        }
        PropertyRow("Import status", status);
        ImGui::EndTable();
    }
    if (!details->Error.empty() && details->ExistsOnDisk) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", details->Error.c_str());
    }

    DrawRegistryActions(context, *details);
    if (details->ExistsOnDisk) {
        switch (details->Kind) {
        case ContentItemKind::Texture:
            DrawTextureSection(context, *details);
            break;
        case ContentItemKind::Audio:
            DrawAudioSection(context, *details);
            break;
        case ContentItemKind::Script:
            DrawScriptSection(*details);
            break;
        case ContentItemKind::Scene:
            DrawSceneSection(context, *details);
            break;
        case ContentItemKind::Animation:
            DrawAnimationSection(context, *details);
            break;
        case ContentItemKind::Prefab:
            DrawPrefabSection(context, *details);
            break;
        default:
            break;
        }
    }
    DrawAssignment(context, *details);

    const InspectorUiState& state = UiState();
    if (!state.Message.empty() && state.MessageKey == resourcePath) {
        ImGui::Spacing();
        ImGui::TextColored(state.MessageIsError ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.5f, 0.85f, 0.55f, 1.0f),
                           "%s", state.Message.c_str());
    }
}

} // namespace Xora2DEditor
