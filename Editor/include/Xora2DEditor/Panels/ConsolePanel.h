#pragma once

#include "Xora2DEditor/EditorPanel.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Xora2DEditor {

class ConsolePanel final : public EditorPanel {
public:
    [[nodiscard]] const char* GetTitle() const override { return "Console"; }
    [[nodiscard]] std::string GetId() const override { return "Console"; }

    void OnAttach(EditorContext& context) override;
    void OnDetach() override;
    void OnImGui(EditorContext& context) override;

    void AppendLine(std::string line);

private:
    std::mutex m_mutex;
    std::vector<std::string> m_lines;
    bool m_autoScroll = true;
};

} // namespace Xora2DEditor