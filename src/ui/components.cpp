#include "ui/components.h"

#include <algorithm>

NineSliceDefinition::NineSliceDefinition()
  : source_pos(0.0f, 0.0f),
    source_size(0.0f, 0.0f),
    border(0.0f),
    pixel_scale(1.0f),
    fill_center(true) {}

NineSliceDefinition::NineSliceDefinition(const ImVec2& source,
    const ImVec2& size,
    float border_pixels,
    float scale,
    bool fill)
  : source_pos(source),
    source_size(size),
    border(border_pixels),
    pixel_scale(std::max(1.0f, scale)),
    fill_center(fill) {}

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

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, corner_radius);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding_x, padding_y));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::COLOR_TRANSPARENT);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::COLOR_TRANSPARENT);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::COLOR_TRANSPARENT);

  bool result = ImGui::InputText(label, buffer, buffer_size, ImGuiInputTextFlags_EnterReturnsTrue);

  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(3);
  ImGui::PopItemWidth();
  return result;
}

bool draw_type_toggle_button(const char* label, bool& toggle_state, float x_pos, float y_pos,
    float button_width, float button_height, const ImVec4& active_color,
    const NineSliceAtlas& frame_atlas,
    const NineSliceDefinition& frame_default,
    const NineSliceDefinition& frame_selected) {
  ImVec2 button_min(x_pos, y_pos);
  ImVec2 button_size(button_width, button_height);
  ImVec2 button_max(button_min.x + button_width, button_min.y + button_height);

  ImGui::SetCursorScreenPos(button_min);
  ImGui::PushID(label);
  bool pressed = ImGui::InvisibleButton("ToggleButton", button_size);
  bool is_hovered = ImGui::IsItemHovered();
  ImGui::PopID();

  bool clicked = false;
  if (pressed) {
    toggle_state = !toggle_state;
    clicked = true;
  }

  ImVec4 bg_color = toggle_state ? active_color : Theme::COLOR_TRANSPARENT;

  ImVec4 text_color = toggle_state ? Theme::TOGGLE_ON_TEXT : Theme::TOGGLE_OFF_TEXT;

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  if (draw_list) {
    if (bg_color.w > 0.0f) {
      draw_list->AddRectFilled(button_min, button_max, Theme::ToImU32(bg_color));
    }

    if (frame_atlas.texture_id != 0) {
      const NineSliceDefinition& frame_def = (is_hovered || toggle_state)
        ? frame_selected
        : frame_default;
      draw_nine_slice_image(frame_atlas, frame_def, button_min, button_size);
    }
    else {
      ImVec4 border_color = toggle_state ? active_color : Theme::TOGGLE_OFF_BORDER;
      float button_rounding = button_height * 0.5f;
      float border_thickness = 1.0f;
      draw_list->AddRect(button_min, button_max, Theme::ToImU32(border_color), button_rounding, 0, border_thickness);
    }

    ImVec2 text_size = ImGui::CalcTextSize(label);
    ImVec2 text_pos(
      button_min.x + (button_width - text_size.x) * 0.5f,
      button_min.y + (button_height - text_size.y) * 0.5f);
    draw_list->AddText(text_pos, Theme::ToImU32(text_color), label);
  }

  return clicked;
}

void draw_nine_slice_image(const NineSliceAtlas& atlas,
    const NineSliceDefinition& definition,
    const ImVec2& dest_pos,
    const ImVec2& dest_size,
    ImU32 tint) {
  if (atlas.texture_id == 0 || atlas.atlas_size.x <= 0.0f || atlas.atlas_size.y <= 0.0f) {
    return;
  }

  if (definition.source_size.x <= 0.0f || definition.source_size.y <= 0.0f) {
    return;
  }

  float scale = std::max(1.0f, definition.pixel_scale);
  float uniform_border = std::max(0.0f, definition.border * scale);
  float left_border = uniform_border;
  float top_border = uniform_border;
  float right_border = uniform_border;
  float bottom_border = uniform_border;

  // Ensure borders don't exceed source dimensions
  float max_horizontal = left_border + right_border;
  float src_width = definition.source_size.x * scale;
  if (max_horizontal > src_width && max_horizontal > 0.0f) {
    float clamp_scale = src_width / max_horizontal;
    left_border *= clamp_scale;
    right_border *= clamp_scale;
  }

  float max_vertical = top_border + bottom_border;
  float src_height = definition.source_size.y * scale;
  if (max_vertical > src_height && max_vertical > 0.0f) {
    float clamp_scale = src_height / max_vertical;
    top_border *= clamp_scale;
    bottom_border *= clamp_scale;
  }

  float dest_left = std::min(left_border, dest_size.x * 0.5f);
  float dest_right = std::min(right_border, dest_size.x - dest_left);
  float dest_top = std::min(top_border, dest_size.y * 0.5f);
  float dest_bottom = std::min(bottom_border, dest_size.y - dest_top);

  float src_border_x = std::min(definition.border, definition.source_size.x * 0.5f);
  float src_border_y = std::min(definition.border, definition.source_size.y * 0.5f);

  float src_x[4] = {
    definition.source_pos.x,
    definition.source_pos.x + src_border_x,
    definition.source_pos.x + definition.source_size.x - src_border_x,
    definition.source_pos.x + definition.source_size.x
  };
  float src_y[4] = {
    definition.source_pos.y,
    definition.source_pos.y + src_border_y,
    definition.source_pos.y + definition.source_size.y - src_border_y,
    definition.source_pos.y + definition.source_size.y
  };

  float dst_x[4] = {
    dest_pos.x,
    dest_pos.x + dest_left,
    dest_pos.x + dest_size.x - dest_right,
    dest_pos.x + dest_size.x
  };
  float dst_y[4] = {
    dest_pos.y,
    dest_pos.y + dest_top,
    dest_pos.y + dest_size.y - dest_bottom,
    dest_pos.y + dest_size.y
  };

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  if (!draw_list) {
    return;
  }

  const float inv_atlas_width = 1.0f / atlas.atlas_size.x;
  const float inv_atlas_height = 1.0f / atlas.atlas_size.y;

  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      if (!definition.fill_center && row == 1 && col == 1) {
        continue;
      }
      float x0 = dst_x[col];
      float x1 = dst_x[col + 1];
      float y0 = dst_y[row];
      float y1 = dst_y[row + 1];

      if (x1 <= x0 || y1 <= y0) {
        continue;
      }

      float u0 = src_x[col] * inv_atlas_width;
      float u1 = src_x[col + 1] * inv_atlas_width;
      float v0 = src_y[row] * inv_atlas_height;
      float v1 = src_y[row + 1] * inv_atlas_height;

      draw_list->AddImage(atlas.texture_id,
        ImVec2(x0, y0), ImVec2(x1, y1),
        ImVec2(u0, v0), ImVec2(u1, v1),
        tint);
    }
  }
}

NineSliceDefinition make_16px_frame(int index, float pixel_scale) {
  const float frame_width = 16.0f;
  const float frame_height = 16.0f;
  ImVec2 source(
    frame_width * static_cast<float>(index),
    8.0f);
  return NineSliceDefinition(source, ImVec2(frame_width, frame_height), 5.0f, pixel_scale);
}

NineSliceDefinition make_8px_frame(int index, int variant, float pixel_scale) {
  const float frame_width = 8.0f;
  const float frame_height = 8.0f;
  const float base_y = 32.0f;
  ImVec2 source(
    frame_width * static_cast<float>(variant),
    base_y + frame_height * static_cast<float>(index));
  return NineSliceDefinition(source, ImVec2(frame_width, frame_height), 3.0f, pixel_scale);
}
