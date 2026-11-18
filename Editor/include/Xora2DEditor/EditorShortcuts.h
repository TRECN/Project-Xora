#pragma once

namespace Xora2DEditor {

class EditorContext;
class EditorUI;

class EditorShortcuts {
public:
    static void HandleGlobalShortcuts(EditorContext& context, EditorUI& ui);
};

} // namespace Xora2DEditor