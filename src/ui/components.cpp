#include "ui/components.h"
#include "texture_manager.h"

#include <algorithm>

SlicedSprite::SlicedSprite(const ImVec2& source,
    const ImVec2& size,
    const ImVec2& border_pixels,
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

void draw_dashed_separator(const ImVec2& start,
    float width,
    float thickness,
    float dash_length,
    float gap_length,
    ImU32 color) {
  if (width <= 0.0f || thickness <= 0.0f || dash_length <= 0.0f) {
    return;
  }

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  if (!draw_list) {
    return;
  }

  float x = start.x;
  float end_x = start.x + std::max(0.0f, width);
  float y_min = start.y;
  float y_max = start.y + thickness;

  while (x < end_x) {
    float dash_end = std::min(end_x, x + dash_length);
    draw_list->AddRectFilled(ImVec2(x, y_min), ImVec2(dash_end, y_max), color);
    x += dash_length + gap_length;
  }
}

void draw_solid_separator(const ImVec2& start,
    float width,
    float thickness,
    ImU32 color) {
  if (width <= 0.0f || thickness <= 0.0f) {
    return;
  }

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  if (!draw_list) {
    return;
  }

  float end_x = start.x + std::max(0.0f, width);
  float y_min = start.y;
  float y_max = start.y + thickness;
  draw_list->AddRectFilled(ImVec2(start.x, y_min), ImVec2(end_x, y_max), color);
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
    const SpriteAtlas& frame_atlas,
    const SlicedSprite& frame_default,
    const SlicedSprite& frame_selected) {
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
      const SlicedSprite& frame_def = (is_hovered || toggle_state)
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

void draw_nine_slice_image(const SpriteAtlas& atlas,
    const SlicedSprite& definition,
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
  float left_border = std::max(0.0f, definition.border.x * scale);
  float top_border = std::max(0.0f, definition.border.y * scale);
  float right_border = left_border;
  float bottom_border = top_border;

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

  float src_border_x = std::min(definition.border.x, definition.source_size.x * 0.5f);
  float src_border_y = std::min(definition.border.y, definition.source_size.y * 0.5f);

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

SlicedSprite make_16px_frame(int index, float pixel_scale) {
  const float frame_width = 16.0f;
  const float frame_height = 16.0f;
  ImVec2 source(
    frame_width * static_cast<float>(index),
    8.0f);
  return SlicedSprite(source, ImVec2(frame_width, frame_height), ImVec2(5.0f, 5.0f), pixel_scale);
}

SlicedSprite make_8px_frame(int index, int variant, float pixel_scale) {
  const float frame_width = 8.0f;
  const float frame_height = 8.0f;
  const float base_y = 32.0f;
  ImVec2 source(
    frame_width * static_cast<float>(variant),
    base_y + frame_height * static_cast<float>(index));
  return SlicedSprite(source, ImVec2(frame_width, frame_height), ImVec2(3.0f, 3.0f), pixel_scale);
}

SlicedSprite make_scrollbar_track_definition(int variant, float pixel_scale) {
  const float track_size = 8.0f;
  const float base_y = 24.0f;
  int clamped_variant = std::clamp(variant, 0, 2);
  ImVec2 source(
    track_size * static_cast<float>(clamped_variant),
    base_y);
  return SlicedSprite(source, ImVec2(track_size, track_size), ImVec2(0.0f, 3.0f), pixel_scale);
}

SlicedSprite make_scrollbar_thumb_definition(float pixel_scale) {
  const float sprite_size = 8.0f;
  const ImVec2 source(24.0f, 24.0f);
  return SlicedSprite(source, ImVec2(sprite_size, sprite_size), ImVec2(3.0f, 3.0f), pixel_scale);
}

ScrollbarState begin_scrollbar_child(const char* id,
    const ImVec2& size,
    const ScrollbarStyle& style,
    ImGuiWindowFlags flags) {
  ScrollbarState state;
  state.style = style;
  state.scrollbar_size = 8.0f * style.pixel_scale;
  state.child_open = true;

  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, state.scrollbar_size);
  ImGui::BeginChild(id, size, false, flags);

  state.window_pos = ImGui::GetWindowPos();
  state.window_size = ImGui::GetWindowSize();
  return state;
}

void end_scrollbar_child(ScrollbarState& state) {
  if (!state.child_open) {
    return;
  }
  state.window_pos = ImGui::GetWindowPos();
  state.window_size = ImGui::GetWindowSize();
  state.scroll_max_y = ImGui::GetScrollMaxY();
  state.scroll_y = ImGui::GetScrollY();
  state.scroll_max_x = ImGui::GetScrollMaxX();
  state.scroll_x = ImGui::GetScrollX();
  state.has_metrics = true;

  ImGui::EndChild();
  ImGui::PopStyleVar();
  state.child_open = false;
}

void draw_scrollbar_overlay(const ScrollbarState& state,
    const SpriteAtlas& atlas,
    const SlicedSprite& track_def,
    const SlicedSprite& thumb_def) {
  if (!state.has_metrics) {
    return;
  }
  if (atlas.texture_id == 0 || atlas.atlas_size.x <= 0.0f || atlas.atlas_size.y <= 0.0f) {
    return;
  }

  float scrollbar_size = state.scrollbar_size;
  bool has_vertical = state.scroll_max_y > 0.0f;
  bool has_horizontal = state.scroll_max_x > 0.0f;
  if (!has_vertical) {
    return;
  }

  ImVec2 bar_min(
    state.window_pos.x + state.window_size.x - scrollbar_size,
    state.window_pos.y);
  ImVec2 bar_max(
    state.window_pos.x + state.window_size.x,
    state.window_pos.y + state.window_size.y - (has_horizontal ? scrollbar_size : 0.0f));

  float bar_height = bar_max.y - bar_min.y;
  if (bar_height <= 0.0f) {
    return;
  }

  float viewable_y = bar_height;
  float total_y = viewable_y + state.scroll_max_y;
  float thumb_ratio = (total_y > 0.0f) ? (viewable_y / total_y) : 1.0f;
  float min_thumb = scrollbar_size * state.style.min_thumb_ratio;
  float thumb_height = std::max(min_thumb, bar_height * thumb_ratio);
  float scroll_range = std::max(1.0f, state.scroll_max_y);
  float thumb_y = bar_min.y + (bar_height - thumb_height) * (state.scroll_y / scroll_range);

  ImVec2 thumb_pos(bar_min.x, thumb_y);
  ImVec2 thumb_size(scrollbar_size, thumb_height);

  ImVec2 mouse = ImGui::GetIO().MousePos;
  bool thumb_hovered = (mouse.x >= thumb_pos.x && mouse.x <= thumb_pos.x + thumb_size.x &&
    mouse.y >= thumb_pos.y && mouse.y <= thumb_pos.y + thumb_size.y);
  bool thumb_active = thumb_hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);

  ImVec4 thumb_tint_vec = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  if (thumb_active) {
    thumb_tint_vec = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
  } else if (thumb_hovered) {
    thumb_tint_vec = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
  }
  ImU32 thumb_tint = ImGui::GetColorU32(thumb_tint_vec);

  draw_nine_slice_image(atlas, track_def, bar_min, ImVec2(scrollbar_size, bar_height), Theme::COLOR_WHITE_U32);
  draw_nine_slice_image(atlas, thumb_def, thumb_pos, thumb_size, thumb_tint);
}
