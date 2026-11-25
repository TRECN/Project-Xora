#pragma once

#include "Xora2D/Particles/ParticleTypes.h"

namespace Xora2D {

class Renderer2D;
class ParticleEffectAsset;
struct ParticleSystemComponent;
struct TransformComponent;

// Renders all active particles in a component using the shared Renderer2D batch.
// Must be called between BeginScene / EndScene.
void RenderParticleSystem(Renderer2D& renderer,
                          const ParticleSystemComponent& component,
                          const TransformComponent& transform,
                          const ParticleEffectAsset* asset);

} // namespace Xora2D
