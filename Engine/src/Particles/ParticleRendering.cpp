#include "Xora2D/Particles/ParticleRendering.h"

#include "Xora2D/Particles/ParticleEffectAsset.h"
#include "Xora2D/Particles/ParticleSystemComponent.h"
#include "Xora2D/Renderer/Renderer2D.h"
#include "Xora2D/Scene/Components.h"

#include <glm/common.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace Xora2D {

void RenderParticleSystem(Renderer2D& renderer,
                          const ParticleSystemComponent& comp,
                          const TransformComponent& transform,
                          const ParticleEffectAsset* asset) {
    const bool hasAsset   = (asset != nullptr && asset->IsValid());
    const bool worldSpace = hasAsset ? asset->GetConfig().WorldSpace : false;
    const bool hasTexture = (comp.LoadedTexture != nullptr);

    // Base position (if local space: draw relative to transform)
    const glm::vec2 origin = worldSpace ? glm::vec2(0.0f) : transform.Translation;

    for (const Particle& p : comp.Pool) {
        if (!p.IsActive()) continue;

        const float t = p.LifetimeFraction();

        // Size
        float size = glm::mix(p.SizeBegin, p.SizeEnd, t);
        if (hasAsset && !asset->GetConfig().SizeCurve.Keys.empty()) {
            size *= asset->GetConfig().SizeCurve.Evaluate(t, 1.0f);
        }
        if (size <= 0.0f) continue;

        // Color
        glm::vec4 color = glm::mix(p.ColorBegin, p.ColorEnd, t);
        if (hasAsset && !asset->GetConfig().ColorOverLifetime.Keys.empty()) {
            color *= asset->GetConfig().ColorOverLifetime.Evaluate(t);
        }
        if (hasAsset && !asset->GetConfig().OpacityCurve.Keys.empty()) {
            color.a *= asset->GetConfig().OpacityCurve.Evaluate(t, 1.0f);
        }

        // Rotation
        const float rotation = glm::mix(p.RotationBegin, p.RotationEnd, t);

        // Screen-space position (local or world)
        const glm::vec2 pos = p.Position + origin;

        const Transform2D drawTransform{pos, glm::vec2(size, size), rotation};

        if (hasTexture) {
            renderer.DrawQuad(drawTransform, *comp.LoadedTexture, color);
        } else {
            renderer.DrawQuad(drawTransform, color);
        }
    }
}

} // namespace Xora2D
