#include "ui/components.h"

#include <algorithm>

bool draw_icon_button(const IconButtonParams& params) {
  if (params.id == nullptr || params.size <= 0.0f) {
    return false;
  }

  ImGui::SetCursorPos(params.cursor_pos);
  ImGui::PushID(params.id);

  ImVec2 button_size(params.size, params.size);
  if (!params.enabled) {
    ImGui::BeginDisabled();
  }

  bool clicked = ImGui::InvisibleButton("Button", button_size);

  if (!params.enabled) {
    ImGui::EndDisabled();
  }

  bool hovered = params.enabled && ImGui::IsItemHovered();
  bool active = params.enabled && ImGui::IsItemActive();

  ImVec2 min = ImGui::GetItemRectMin();
  ImVec2 max = ImGui::GetItemRectMax();

  if ((hovered || active) && params.highlight_color.w > 0.0f) {
    ImVec4 highlight = params.highlight_color;
    if (active) {
      highlight.w = std::min(1.0f, highlight.w + 0.2f);
    }
    ImGui::GetWindowDrawList()->AddRectFilled(min, max,
      Theme::ToImU32(highlight), params.corner_radius);
  }

  ImVec4 icon_color = params.colors.normal;
  if (active) {
    icon_color = params.colors.active;
  }
  else if (!params.enabled) {
    icon_color = params.colors.disabled;
  }

  float padding = params.icon_padding >= 0.0f ? params.icon_padding : std::max(2.0f, params.size * 0.15f);

  if (params.icon_texture != 0) {
    ImVec2 icon_min(min.x + padding, min.y + padding);
    ImVec2 icon_max(max.x - padding, max.y - padding);
    ImGui::GetWindowDrawList()->AddImage(
      (ImTextureID) (intptr_t) params.icon_texture,
      icon_min,
      icon_max,
      ImVec2(0.0f, 0.0f),
      ImVec2(1.0f, 1.0f),
      Theme::ToImU32(icon_color));
  }
  else if (params.fallback_label && params.fallback_label[0] != '\0') {
    ImVec2 text_size = ImGui::CalcTextSize(params.fallback_label);
    ImVec2 text_pos(
      min.x + (button_size.x - text_size.x) * 0.5f,
      min.y + (button_size.y - text_size.y) * 0.5f);
    ImGui::GetWindowDrawList()->AddText(text_pos, Theme::ToImU32(icon_color), params.fallback_label);
  }

  ImGui::PopID();
  return params.enabled && clicked;
}

bool draw_wrapped_settings_entry(const char* id, const std::string& text,
    const ImVec4& text_color) {
  ImGui::PushID(id);
  float wrap_limit = ImGui::GetCursorPos().x + ImGui::GetColumnWidth();
  ImGui::PushTextWrapPos(wrap_limit);
  ImGui::TextColored(text_color, "%s", text.c_str());
  ImGui::PopTextWrapPos();

  ImVec2 min = ImGui::GetItemRectMin();
  ImVec2 max = ImGui::GetItemRectMax();
  ImVec2 size = ImVec2(max.x - min.x, max.y - min.y);

  ImGui::SetCursorScreenPos(min);
  bool clicked = ImGui::InvisibleButton("WrappedEntry", size);
  bool hovered = ImGui::IsItemHovered();

  if (hovered) {
    ImGui::GetWindowDrawList()->AddRectFilled(min, max,
      Theme::ToImU32(Theme::COLOR_SEMI_TRANSPARENT), 6.0f);
  }

  ImGui::SetCursorScreenPos(ImVec2(min.x, max.y));
  ImGui::PopID();
  return clicked;
}

bool fancy_text_input(const char* label, char* buffer, size_t buffer_size, float width,
    float padding_x, float padding_y, float corner_radius) {
  ImGui::PushItemWidth(width);

  float font_height = ImGui::GetFontSize();
  float actual_input_height = font_height + (padding_y * 2.0f);

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, corner_radius);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding_x, padding_y));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::SEARCH_BOX_BG);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::SEARCH_BOX_BG_HOVERED);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::SEARCH_BOX_BG_ACTIVE);

  ImVec2 shadow_offset(2.0f, 2.0f);
  ImVec2 input_pos = ImGui::GetCursorScreenPos();
  ImVec2 shadow_min(input_pos.x + shadow_offset.x, input_pos.y + shadow_offset.y);
  ImVec2 shadow_max(shadow_min.x + width, shadow_min.y + actual_input_height);

  ImGui::GetWindowDrawList()->AddRectFilled(
    shadow_min, shadow_max,
    ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.12f)),
    corner_radius);

  bool result = ImGui::InputText(label, buffer, buffer_size, ImGuiInputTextFlags_EnterReturnsTrue);

  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(2);
  ImGui::PopItemWidth();
  return result;
}

bool draw_type_toggle_button(const char* label, bool& toggle_state, float x_pos, float y_pos,
    float button_width, float button_height, const ImVec4& active_color) {
  ImVec2 button_min(x_pos, y_pos);
  ImVec2 button_max(button_min.x + button_width, button_min.y + button_height);

  ImVec2 mouse_pos = ImGui::GetMousePos();
  bool is_hovered = (mouse_pos.x >= button_min.x && mouse_pos.x <= button_max.x &&
    mouse_pos.y >= button_min.y && mouse_pos.y <= button_max.y);

  ImVec4 bg_color = Theme::BACKGROUND_WHITE;
  if (toggle_state) {
    bg_color = active_color;
  }
  else if (is_hovered) {
    bg_color = Theme::TOGGLE_HOVER_BG;
  }

  ImVec4 border_color = toggle_state ? active_color : Theme::TOGGLE_OFF_BORDER;
  ImVec4 text_color = toggle_state ? Theme::TOGGLE_ON_TEXT : Theme::TOGGLE_OFF_TEXT;

  float button_rounding = button_height * 0.5f;
  float border_thickness = 1.0f;

  ImGui::GetWindowDrawList()->AddRectFilled(button_min, button_max, Theme::ToImU32(bg_color), button_rounding);
  ImGui::GetWindowDrawList()->AddRect(button_min, button_max, Theme::ToImU32(border_color), button_rounding, 0, border_thickness);

  ImVec2 text_size = ImGui::CalcTextSize(label);
  ImVec2 text_pos(
    button_min.x + (button_width - text_size.x) * 0.5f,
    button_min.y + (button_height - text_size.y) * 0.5f);
  ImGui::GetWindowDrawList()->AddText(text_pos, Theme::ToImU32(text_color), label);

  bool clicked = false;
  if (is_hovered && ImGui::IsMouseClicked(0)) {
    toggle_state = !toggle_state;
    clicked = true;
  }

  return clicked;
}
