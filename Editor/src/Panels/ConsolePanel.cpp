#include "Xora2DEditor/Panels/ConsolePanel.h"

#include "Xora2D/Core/Logger.h"

#include <chrono>
#include <imgui.h>

namespace Xora2DEditor {

void ConsolePanel::OnAttach(EditorContext& /*context*/) {
    Xora2D::Logger::SetSink([this](Xora2D::LogLevel level, std::string_view message) {
        AppendLine(std::string(Xora2D::Logger::FormatLine(level, message, std::chrono::system_clock::now())));
    });
}

void ConsolePanel::OnDetach() {
    Xora2D::Logger::ClearSink();
}

void ConsolePanel::OnImGui(EditorContext& /*context*/) {
    if (ImGui::Begin(GetTitle())) {
        if (ImGui::Button("Clear")) {
            std::lock_guard lock(m_mutex);
            m_lines.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_autoScroll);

        ImGui::Separator();
        if (ImGui::BeginChild("scrolling", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar)) {
            std::lock_guard lock(m_mutex);
            for (const std::string& line : m_lines) {
                ImGui::TextUnformatted(line.c_str());
            }
            if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void ConsolePanel::AppendLine(std::string line) {
    std::lock_guard lock(m_mutex);
    m_lines.push_back(std::move(line));
    constexpr std::size_t kMaxLines = 800;
    if (m_lines.size() > kMaxLines) {
        m_lines.erase(m_lines.begin(), m_lines.begin() + (m_lines.size() - kMaxLines));
    }
}

} // namespace Xora2DEditor