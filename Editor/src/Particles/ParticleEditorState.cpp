#include "Xora2DEditor/Particles/ParticleEditorState.h"

#include "Xora2DEditor/EditorContext.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Particles/ParticleEffectAsset.h"
#include "Xora2D/Particles/ParticleSystemComponent.h"
#include "Xora2D/Renderer/ColorRenderTarget.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Scene.h"

namespace Xora2DEditor {

void ParticleEditorState::Open(Xora2D::AssetHandle handle, EditorContext& context) {
    Close();

    if (!handle.IsValid()) {
        return;
    }

    context.GetAssetManager().LoadParticleEffect(handle);
    const auto asset = context.GetAssetManager().GetLoadedParticleEffect(handle);
    if (!asset || !asset->IsValid()) {
        XORA_LOG_ERROR("ParticleEditorState: failed to load particle effect for editing");
        return;
    }

    EditedAsset = handle;
    Config = asset->GetConfig();
    Dirty = false;

    PreviewScene = Xora2D::Scene::Create();
    PreviewScene->SetAssetManager(&context.GetAssetManager());
    PreviewScene->SetViewportSize(512.0f, 512.0f);

    m_emitterEntity = PreviewScene->CreateEntity("Emitter");
    auto& ps = m_emitterEntity.AddComponent<Xora2D::ParticleSystemComponent>();
    ps.EffectAsset = handle;
    ps.EmissionEnabled = true;
    ps.Play();

    PreviewCamera.SetViewportSize(512.0f, 512.0f);
    PreviewCamera.SetZoom(1.0f);

    if (!PreviewTarget) {
        PreviewTarget = Xora2D::ColorRenderTarget::Create();
    }
    PreviewTarget->Resize(512, 512);
}

void ParticleEditorState::ApplyConfigToPreview() {
    if (!IsOpen() || !m_emitterEntity.IsValid()) {
        return;
    }
    if (!m_emitterEntity.HasComponent<Xora2D::ParticleSystemComponent>()) {
        return;
    }
    m_emitterEntity.GetComponent<Xora2D::ParticleSystemComponent>().Clear();
}

void ParticleEditorState::RestartPreview() {
    if (!IsOpen() || !m_emitterEntity.IsValid()) {
        return;
    }
    if (m_emitterEntity.HasComponent<Xora2D::ParticleSystemComponent>()) {
        m_emitterEntity.GetComponent<Xora2D::ParticleSystemComponent>().Restart();
    }
}

bool ParticleEditorState::Save(EditorContext& context) {
    if (!IsOpen()) {
        return false;
    }
    const Xora2D::AssetMetadata* meta = context.GetAssetRegistry().FindByHandle(EditedAsset);
    if (!meta) {
        XORA_LOG_ERROR("ParticleEditorState: asset not found in registry");
        return false;
    }
    const std::filesystem::path path = Xora2D::ResolveAssetPath(meta->RelativePath);
    const Xora2D::AssetResult result = Xora2D::ParticleEffectAsset::SaveToFile(path, Config);
    if (!result.Success) {
        XORA_LOG_ERROR("ParticleEditorState: failed to save particle effect");
        return false;
    }
    context.GetAssetManager().ReloadAsset(EditedAsset);
    Dirty = false;
    return true;
}

void ParticleEditorState::Close() {
    EditedAsset = {};
    Dirty = false;
    PreviewScene.reset();
    PreviewTarget.reset();
    m_emitterEntity = {};
}

} // namespace Xora2DEditor
