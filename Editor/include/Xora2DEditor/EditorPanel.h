#pragma once

#include <string>

namespace Xora2DEditor {

class EditorContext;

// Base type for dockable editor panels.
class EditorPanel {
public:
    virtual ~EditorPanel() = default;

    [[nodiscard]] virtual const char* GetTitle() const = 0;
    [[nodiscard]] virtual std::string GetId() const { return GetTitle(); }

    [[nodiscard]] bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

    virtual void OnAttach(EditorContext& /*context*/) {}
    virtual void OnDetach() {}
    virtual void OnImGui(EditorContext& context) = 0;

private:
    bool m_visible = true;
};

} // namespace Xora2DEditor