#pragma once

#include "imgui.h"
#include "theme.h"

#include <string>

// Describes how to cut an atlas sprite into 3x3 regions for pixel-perfect scaling.
struct SlicedSprite {
  ImVec2 source_pos;
  ImVec2 source_size;
  // Border thickness in pixels for left, right, top, bottom order.
  ImVec4 border;
  // Optional uniform scaling factor for the entire sprite (1px -> scale px)
  float pixel_scale;
  bool fill_center;

  SlicedSprite(const ImVec2& source,
      const ImVec2& size,
      const ImVec4& border_pixels,
      float scale = 1.0f,
      bool fill = true);
};

struct SpriteAtlas;

void draw_nine_slice_image(const SpriteAtlas& atlas,
    const SlicedSprite& definition,
    const ImVec2& dest_pos,
    const ImVec2& dest_size,
    float ui_scale,
    ImU32 tint = Theme::COLOR_WHITE_U32);

// Draw a dashed horizontal separator with pixel-art style segments.
void draw_dashed_separator(const ImVec2& start,
    float width,
    float thickness = 3.0f,
    float dash_length = 6.0f,
    float gap_length = 4.0f,
    ImU32 color = Theme::ToImU32(Theme::BACKGROUND_WHITE));

// Draw a solid horizontal separator.
void draw_solid_separator(const ImVec2& start,
    float width,
    float thickness = 2.0f,
    ImU32 color = Theme::ToImU32(Theme::SEPARATOR_GRAY));

SlicedSprite make_scrollbar_track_definition(int variant, float pixel_scale = 1.0f);
SlicedSprite make_scrollbar_thumb_definition(float pixel_scale = 1.0f);

struct ScrollbarStyle {
  float pixel_scale = 2.0f;
  float min_thumb_ratio = 0.9f; // relative to scrollbar width
};

struct ScrollbarState {
  ScrollbarStyle style;
  float scrollbar_size = 0.0f;
  ImVec2 window_pos = ImVec2(0.0f, 0.0f);
  ImVec2 window_size = ImVec2(0.0f, 0.0f);
  float scroll_y = 0.0f;
  float scroll_max_y = 0.0f;
  float scroll_x = 0.0f;
  float scroll_max_x = 0.0f;
  bool child_open = false;
  bool has_metrics = false;
};

ScrollbarState begin_scrollbar_child(const char* id,
    const ImVec2& size,
    const ScrollbarStyle& style = {},
    ImGuiWindowFlags flags = 0);
void end_scrollbar_child(ScrollbarState& state);
void draw_scrollbar_overlay(const ScrollbarState& state,
    const SpriteAtlas& atlas,
    const SlicedSprite& track_def,
    const SlicedSprite& thumb_def,
    float ui_scale);

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

bool draw_wrapped_settings_entry_with_frame(const char* id,
    const std::string& text,
    const ImVec4& text_color,
    const SpriteAtlas& atlas,
    const SlicedSprite& frame_def,
    float ui_scale,
    ImVec2 padding = ImVec2(8.0f, 4.0f));

bool fancy_text_input(const char* label, char* buffer, size_t buffer_size, float width,
    float padding_x = 20.0f, float padding_y = 16.0f, float corner_radius = 25.0f);

bool draw_type_toggle_button(const char* label, bool& toggle_state, float x_pos, float y_pos,
    float button_width, float button_height, const ImVec4& active_color,
    const SpriteAtlas& frame_atlas,
    const SlicedSprite& frame_default,
    const SlicedSprite& frame_selected,
    float ui_scale);

// Pixel-art tag chip with 8px frame backing.
void draw_tag_chip(const std::string& text,
    const ImVec4& fill_color,
    const ImVec4& text_color,
    const char* id_suffix,
    const SpriteAtlas& atlas,
    const SlicedSprite& frame_def,
    float ui_scale,
    ImVec2 padding = ImVec2(10.0f, 4.0f));

bool draw_pixel_radio_button(const char* id,
    bool selected,
    const SpriteAtlas& atlas,
    float ui_scale,
    float pixel_scale = 2.0f);

bool draw_pixel_checkbox(const char* id,
    bool& value,
    const SpriteAtlas& atlas,
    float ui_scale,
    float pixel_scale = 2.0f);

bool draw_small_frame_button(const char* id,
    const char* label,
    const SpriteAtlas& atlas,
    const ImVec2& size,
    float ui_scale,
    float pixel_scale = 3.0f);

SlicedSprite make_16px_frame(int index, float pixel_scale = 1.0f);
SlicedSprite make_8px_frame(int index, int variant, float pixel_scale = 1.0f);
SlicedSprite make_modal_combined_frame(float pixel_scale = 2.0f);
