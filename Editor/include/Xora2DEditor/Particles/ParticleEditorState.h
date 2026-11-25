#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Particles/ParticleEffectAsset.h"
#include "Xora2D/Particles/ParticleSystemComponent.h"
#include "Xora2D/Renderer/ColorRenderTarget.h"
#include "Xora2D/Renderer/OrthographicCamera.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"

#include <memory>

namespace Xora2DEditor {

class EditorContext;

// Runtime state for the Particle Editor panel: the asset being edited, the preview scene/component,
// and a rendered-to-texture viewport.
class ParticleEditorState {
public:
    Xora2D::AssetHandle EditedAsset;
    Xora2D::ParticleEffectConfig Config;
    bool Dirty = false;

    std::shared_ptr<Xora2D::Scene> PreviewScene;
    Xora2D::OrthographicCamera PreviewCamera;
    std::unique_ptr<Xora2D::ColorRenderTarget> PreviewTarget;

    void Open(Xora2D::AssetHandle handle, EditorContext& context);
    void ApplyConfigToPreview();
    bool Save(EditorContext& context);
    void RestartPreview();
    void Close();

    [[nodiscard]] bool IsOpen() const { return EditedAsset.IsValid(); }

private:
    Xora2D::Entity m_emitterEntity;
};

} // namespace Xora2DEditor
