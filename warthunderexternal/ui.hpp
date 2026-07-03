#pragma once
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

namespace UI {
    inline void DrawGlow(ImDrawList* draw, ImVec2 start, ImVec2 end, ImColor color, float rounding, float glowSize) {
        for (float i = 0; i < glowSize; i += 1.0f) {
            float alpha = 1.0f - (i / glowSize);
            ImColor stepColor = color;
            stepColor.Value.w *= (alpha * 0.1f);
            draw->AddRect(ImVec2(start.x - i, start.y - i), ImVec2(end.x + i, end.y + i), stepColor, rounding + i, 0, 2.0f);
        }
    }

    inline bool Toggle(const char* label, bool* v) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        float height = ImGui::GetFrameHeight();
        float width = height * 1.8f;
        float radius = height * 0.5f;

        ImGui::InvisibleButton(label, ImVec2(width, height));
        bool clicked = ImGui::IsItemClicked();
        if (clicked) *v = !*v;

        float t = *v ? 1.0f : 0.0f;
        ImU32 col_bg = ImGui::GetColorU32(*v ? ImVec4(0.86f, 0.17f, 0.17f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, height * 0.5f);
        draw_list->AddCircleFilled(ImVec2(p.x + radius + t * (width - radius * 2.0f), p.y + radius), radius - 1.5f, IM_COL32(255, 255, 255, 255));

        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
        ImGui::Text("%s", label);
        return clicked;
    }

    inline bool Tab(const char* label, bool active) {
        ImVec2 size = ImVec2(150, 35);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        bool clicked = ImGui::InvisibleButton(label, size);
        if (active) {
            draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImColor(219, 44, 44, 255), 4.0f);
            DrawGlow(draw, pos, ImVec2(pos.x + size.x, pos.y + size.y), ImColor(219, 44, 44, 255), 4.0f, 10.0f);
        }
        else if (ImGui::IsItemHovered()) {
            draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImColor(40, 40, 45, 255), 4.0f);
        }

        ImVec2 textSize = ImGui::CalcTextSize(label);
        draw->AddText(ImVec2(pos.x + (size.x - textSize.x) / 2.0f, pos.y + (size.y - textSize.y) / 2.0f), active ? ImColor(255, 255, 255, 255) : ImColor(150, 150, 150, 255), label);
        ImGui::Spacing();
        return clicked;
    }

    inline void Slider(const char* label, float* v, float v_min, float v_max, const char* format = "%.1f") {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.86f, 0.17f, 0.17f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
        ImGui::SliderFloat(label, v, v_min, v_max, format);
        ImGui::PopStyleColor(4);
    }

    inline bool Button(const char* label, ImVec2 size) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.17f, 0.17f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
        bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(3);
        return pressed;
    }
}