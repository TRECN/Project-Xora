#include "Xora2DEditor/Panels/ParticleEditorPanel.h"

#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/Particles/ParticleEditorState.h"

#include "Xora2D/Particles/ParticleEffectAsset.h"
#include "Xora2D/Particles/ParticleTypes.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>

namespace Xora2DEditor {
namespace {

float Clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void DrawColorField(const char* label, glm::vec4& color) {
    float c[4] = {color.r, color.g, color.b, color.a};
    if (ImGui::ColorEdit4(label, c)) {
        color = {c[0], c[1], c[2], c[3]};
    }
}

void DrawFloatCurveEditor(const char* label, Xora2D::FloatCurve& curve) {
    ImGui::PushID(label);
    ImGui::Text("%s", label);
    ImGui::SameLine();
    if (ImGui::Button("+##AddKey")) {
        Xora2D::FloatCurveKey key;
        key.Time = curve.Keys.empty() ? 0.0f : 1.0f;
        key.Value = 1.0f;
        curve.Keys.push_back(key);
    }
    for (std::size_t i = 0; i < curve.Keys.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        auto& k = curve.Keys[i];
        float t = k.Time;
        float v = k.Value;
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::DragFloat("t", &t, 0.01f, 0.0f, 1.0f)) {
            k.Time = Clampf(t, 0.0f, 1.0f);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::DragFloat("v", &v, 0.01f)) {
            k.Value = v;
        }
        ImGui::SameLine();
        if (ImGui::Button("X")) {
            curve.Keys.erase(curve.Keys.begin() + static_cast<std::ptrdiff_t>(i));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}

} // namespace

void ParticleEditorPanel::OnImGui(EditorContext& context) {
    if (const auto req = context.ConsumeParticleEditorRequest()) {
        context.GetParticleEditor().Open(*req, context);
    }

    ParticleEditorState& state = context.GetParticleEditor();

    if (!state.IsOpen()) {
        ImGui::TextDisabled("No particle effect open. Double-click a .particle.yaml in the Content Browser.");
        return;
    }

    if (ImGui::Button("Save")) {
        state.Save(context);
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart Preview")) {
        state.RestartPreview();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        state.Close();
        return;
    }

    if (state.Dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(unsaved)");
    }

    ImGui::Separator();

    const float availWidth = ImGui::GetContentRegionAvail().x;
    const float previewWidth = 300.0f;
    const float propsWidth = availWidth - previewWidth - ImGui::GetStyle().ItemSpacing.x;

    ImGui::BeginChild("##ParticleProps", ImVec2(propsWidth, 0.0f), false);
    {
        auto& cfg = state.Config;
        bool changed = false;

        if (ImGui::CollapsingHeader("Emission", ImGuiTreeNodeFlags_DefaultOpen)) {
            int maxP = static_cast<int>(cfg.MaxParticles);
            if (ImGui::DragInt("Max Particles", &maxP, 1.0f, 1, 100000)) {
                cfg.MaxParticles = static_cast<std::uint32_t>(std::max(1, maxP));
                changed = true;
            }
            changed |= ImGui::DragFloat("Emission Rate", &cfg.EmissionRate, 1.0f, 0.0f, 10000.0f);
            changed |= ImGui::Checkbox("Looping", &cfg.Looping);
            changed |= ImGui::DragFloat("Duration", &cfg.Duration, 0.1f, 0.0f, 60.0f);
            changed |= ImGui::DragFloat("Start Delay", &cfg.StartDelay, 0.1f, 0.0f, 10.0f);
            changed |= ImGui::Checkbox("World Space", &cfg.WorldSpace);
        }

        if (ImGui::CollapsingHeader("Lifetime & Speed", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::DragFloat("Lifetime Min", &cfg.LifetimeMin, 0.01f, 0.001f, 100.0f);
            changed |= ImGui::DragFloat("Lifetime Max", &cfg.LifetimeMax, 0.01f, 0.001f, 100.0f);
            changed |= ImGui::DragFloat("Speed Min", &cfg.SpeedMin, 0.01f, 0.0f, 100.0f);
            changed |= ImGui::DragFloat("Speed Max", &cfg.SpeedMax, 0.01f, 0.0f, 100.0f);
            {
                float dir[2] = {cfg.Direction.x, cfg.Direction.y};
                if (ImGui::DragFloat2("Direction", dir, 0.01f, -1.0f, 1.0f)) {
                    cfg.Direction = {dir[0], dir[1]};
                    changed = true;
                }
            }
            changed |= ImGui::DragFloat("Spread Angle", &cfg.SpreadAngle, 1.0f, 0.0f, 360.0f);
            {
                float grav[2] = {cfg.Gravity.x, cfg.Gravity.y};
                if (ImGui::DragFloat2("Gravity", grav, 0.1f)) {
                    cfg.Gravity = {grav[0], grav[1]};
                    changed = true;
                }
            }
        }

        if (ImGui::CollapsingHeader("Size & Rotation")) {
            changed |= ImGui::DragFloat("Size Begin", &cfg.SizeBegin, 0.01f, 0.0f, 100.0f);
            changed |= ImGui::DragFloat("Size End", &cfg.SizeEnd, 0.01f, 0.0f, 100.0f);
            DrawFloatCurveEditor("Size Curve", cfg.SizeCurve);
            changed |= ImGui::DragFloat("Rotation Begin", &cfg.RotationBegin, 1.0f);
            changed |= ImGui::DragFloat("Rotation End", &cfg.RotationEnd, 1.0f);
            changed |= ImGui::DragFloat("Angular Vel Min", &cfg.AngularVelocityMin, 1.0f);
            changed |= ImGui::DragFloat("Angular Vel Max", &cfg.AngularVelocityMax, 1.0f);
        }

        if (ImGui::CollapsingHeader("Color")) {
            DrawColorField("Color Begin", cfg.ColorBegin);
            if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
            DrawColorField("Color End", cfg.ColorEnd);
            if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
            DrawFloatCurveEditor("Opacity Curve", cfg.OpacityCurve);
        }

        if (changed) {
            state.Dirty = true;
            state.ApplyConfigToPreview();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##ParticlePreview", ImVec2(previewWidth, 0.0f), true);
    {
        ImGui::TextDisabled("Live Preview");
        if (state.PreviewScene) {
            state.PreviewScene->OnUpdate(1.0f / 60.0f);
            ImGui::TextDisabled("MaxParticles: %u", state.Config.MaxParticles);
            ImGui::TextDisabled("EmissionRate: %.1f/s", state.Config.EmissionRate);
        }
    }
    ImGui::EndChild();
}

} // namespace Xora2DEditor
