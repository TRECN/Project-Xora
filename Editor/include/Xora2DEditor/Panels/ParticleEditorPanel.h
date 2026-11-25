#pragma once

#include "Xora2DEditor/EditorPanel.h"

namespace Xora2DEditor {

class ParticleEditorPanel : public EditorPanel {
public:
    [[nodiscard]] const char* GetTitle() const override { return "Particle Editor"; }
    void OnImGui(EditorContext& context) override;
};

} // namespace Xora2DEditor
