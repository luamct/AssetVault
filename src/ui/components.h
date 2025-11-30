#pragma once

#include "imgui.h"
#include "theme.h"

#include <string>

struct IconButtonColors {
  ImVec4 normal = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  ImVec4 active = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  ImVec4 disabled = ImVec4(1.0f, 1.0f, 1.0f, 0.4f);
};

struct IconButtonParams {
  const char* id = "";
  ImVec2 cursor_pos = ImVec2(0.0f, 0.0f);
  float size = 0.0f;
  unsigned int icon_texture = 0;
  const char* fallback_label = nullptr;
  IconButtonColors colors;
  bool enabled = true;
  float corner_radius = 8.0f;
  float icon_padding = -1.0f;  // Negative uses automatic padding based on size
  ImVec4 highlight_color = Theme::COLOR_SEMI_TRANSPARENT;
};

bool draw_icon_button(const IconButtonParams& params);

bool draw_wrapped_settings_entry(const char* id, const std::string& text,
    const ImVec4& text_color);

bool fancy_text_input(const char* label, char* buffer, size_t buffer_size, float width,
    float padding_x = 20.0f, float padding_y = 16.0f, float corner_radius = 25.0f);

bool draw_type_toggle_button(const char* label, bool& toggle_state, float x_pos, float y_pos,
    float button_width, float button_height,
    const ImVec4& active_color = Theme::TOGGLE_ON_BG);
