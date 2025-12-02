#pragma once

#include "imgui.h"
#include "theme.h"

#include <string>

// Describes how to cut an atlas sprite into 3x3 regions for pixel-perfect scaling.
struct NineSliceDefinition {
  ImVec2 source_pos;
  ImVec2 source_size;
  // Uniform border thickness in pixels
  float border;
  // Optional uniform scaling factor for the entire sprite (1px -> scale px)
  float pixel_scale;
  bool fill_center;

  NineSliceDefinition();

  NineSliceDefinition(const ImVec2& source,
      const ImVec2& size,
      float border_pixels,
      float scale = 1.0f,
      bool fill = true);
};

struct SpriteAtlas;

// 3-slice variant for elements that stretch along a single axis (e.g., scrollbars).
struct ThreeSliceDefinition {
  ImVec2 source_pos;
  ImVec2 source_size;
  float edge;
  float pixel_scale;

  ThreeSliceDefinition();

  ThreeSliceDefinition(const ImVec2& source,
      const ImVec2& size,
      float edge_pixels,
      float scale = 1.0f);
};

void draw_nine_slice_image(const SpriteAtlas& atlas,
    const NineSliceDefinition& definition,
    const ImVec2& dest_pos,
    const ImVec2& dest_size,
    ImU32 tint = Theme::COLOR_WHITE_U32);
void draw_three_slice_image(const SpriteAtlas& atlas,
    const ThreeSliceDefinition& definition,
    const ImVec2& dest_pos,
    const ImVec2& dest_size,
    bool vertical,
    ImU32 tint = Theme::COLOR_WHITE_U32);

ThreeSliceDefinition make_scrollbar_track_definition(int variant, float pixel_scale = 1.0f);
ThreeSliceDefinition make_scrollbar_thumb_definition(float pixel_scale = 1.0f);

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
    const ThreeSliceDefinition& track_def,
    const ThreeSliceDefinition& thumb_def);

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
    float button_width, float button_height, const ImVec4& active_color,
    const SpriteAtlas& frame_atlas,
    const NineSliceDefinition& frame_default,
    const NineSliceDefinition& frame_selected);

NineSliceDefinition make_16px_frame(int index, float pixel_scale = 1.0f);
NineSliceDefinition make_8px_frame(int index, int variant, float pixel_scale = 1.0f);
