#include "ui.h"
#include "theme.h"
#include "utils.h"
#include "config.h"
#include "imgui.h"
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <algorithm>

void render_clickable_path(const std::string& full_path, SearchState& search_state) {
  // Get relative path from assets folder
  std::string relative_path = get_relative_asset_path(full_path);

  // Split path into segments
  std::vector<std::string> segments;
  std::stringstream ss(relative_path);
  std::string segment;

  while (std::getline(ss, segment, '/')) {
    if (!segment.empty()) {
      segments.push_back(segment);
    }
  }

  // Render each segment as a clickable link (exclude the last segment if it's a file)
  // Only directory segments should be clickable, not the filename itself
  size_t clickable_segments = segments.size();
  if (clickable_segments > 0) {
    clickable_segments = segments.size() - 1; // Exclude the filename
  }

  // Get available width for wrapping
  float available_width = ImGui::GetContentRegionAvail().x;
  float current_line_width = 0.0f;

  for (size_t i = 0; i < segments.size(); ++i) {
    bool is_clickable = (i < clickable_segments);

    // Calculate the width this segment would take (including separator if not first)
    float separator_width = 0.0f;
    if (i > 0) {
      separator_width = ImGui::CalcTextSize(" / ").x + 4.0f; // Add spacing (2.0f before + 2.0f after)
    }

    // Text width calculation (same for both clickable and non-clickable now)
    float segment_width = ImGui::CalcTextSize(segments[i].c_str()).x;

    // Check if we need to wrap to next line
    if (i > 0) {
      if (current_line_width + separator_width + segment_width > available_width) {
        // Add separator at end of current line, then wrap to new line
        ImGui::SameLine(0, 2.0f);
        ImGui::TextColored(Theme::TEXT_SECONDARY, " /");
        current_line_width = segment_width;
      }
      else {
        // Continue on same line with separator
        current_line_width += separator_width + segment_width;
        ImGui::SameLine(0, 2.0f); // Small spacing between segments
        ImGui::TextColored(Theme::TEXT_SECONDARY, " / ");
        ImGui::SameLine(0, 2.0f);
      }
    }
    else {
      // First segment
      current_line_width = segment_width;
    }

    // Build the path up to this segment
    std::string path_to_segment;
    for (size_t j = 0; j <= i; ++j) {
      if (j > 0) path_to_segment += "/";
      path_to_segment += segments[j];
    }

    if (is_clickable) {
      // Check if this path is already in the filter
      bool is_active = std::find(search_state.path_filters.begin(),
        search_state.path_filters.end(),
        path_to_segment) != search_state.path_filters.end();

      // Choose color based on active state
      ImVec4 link_color = is_active ? Theme::ACCENT_BLUE_2 : Theme::ACCENT_BLUE_1;

      // Render as clickable text link
      ImGui::PushStyleColor(ImGuiCol_Text, link_color);
      ImGui::Text("%s", segments[i].c_str());
      ImGui::PopStyleColor();

      // Get item rect for interaction and underline
      ImVec2 text_min = ImGui::GetItemRectMin();
      ImVec2 text_max = ImGui::GetItemRectMax();
      bool is_hovered = ImGui::IsItemHovered();

      // Draw underline on hover
      if (is_hovered) {
        ImGui::GetWindowDrawList()->AddLine(
          ImVec2(text_min.x, text_max.y - 1.0f),
          ImVec2(text_max.x, text_max.y - 1.0f),
          ImGui::GetColorU32(link_color),
          1.0f
        );

        // Change cursor to hand
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      }

      // Handle click
      if (is_hovered && ImGui::IsMouseClicked(0)) {
        // Single path filter mode - only one can be active at a time
        if (is_active) {
          // If this path is already active, deactivate it (clear all filters)
          search_state.path_filters.clear();
          search_state.path_filter_active = false;
        }
        else {
          // Clear all existing filters and add this one
          search_state.path_filters.clear();
          search_state.path_filters.push_back(path_to_segment);
          search_state.path_filter_active = true;
        }

        // Trigger search update
        search_state.update_needed = true;
      }
    }
    else {
      // Render non-clickable segment (filename) as regular text
      ImGui::TextColored(Theme::TEXT_DARK, "%s", segments[i].c_str());
    }
  }
}

// Custom slider component for audio seek bar
bool audio_seek_bar(const char* id, float* value, float min_value, float max_value, float width, float height) {
  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

  // Calculate dimensions
  const float handle_radius = height * 2.0f; // Circle handle is ~4x the line height
  const ImVec2 size(width, handle_radius * 2.0f);

  // Create invisible button for interaction
  ImGui::InvisibleButton(id, size);
  bool hovered = ImGui::IsItemHovered();
  bool active = ImGui::IsItemActive();

  // Calculate value based on mouse position when dragging
  bool value_changed = false;
  if (active) {
    ImVec2 mouse_pos = ImGui::GetMousePos();
    float mouse_x = mouse_pos.x - cursor_pos.x;
    float new_value = (mouse_x / width) * (max_value - min_value) + min_value;
    if (new_value < min_value) new_value = min_value;
    if (new_value > max_value) new_value = max_value;
    if (*value != new_value) {
      *value = new_value;
      value_changed = true;
    }
  }

  // Calculate current position
  float position_ratio = (max_value > min_value) ? (*value - min_value) / (max_value - min_value) : 0.0f;
  if (position_ratio < 0.0f) position_ratio = 0.0f;
  if (position_ratio > 1.0f) position_ratio = 1.0f;
  float handle_x = cursor_pos.x + position_ratio * width;

  // Colors
  const ImU32 line_color_played = ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.3f, 1.0f));    // Darker line (played portion - before handle)
  const ImU32 line_color_unplayed = ImGui::GetColorU32(ImVec4(0.7f, 0.7f, 0.7f, 1.0f));  // Lighter line (unplayed portion - after handle)
  const ImU32 handle_color = hovered || active ?
    ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)) :    // White when hovered/active
    ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 1.0f));     // Light gray normally

  // Draw the seek bar
  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  // Line center Y position
  float line_y = cursor_pos.y + size.y * 0.5f;

  // Draw played portion (left of handle) - darker
  if (position_ratio > 0.0f) {
    draw_list->AddRectFilled(
      ImVec2(cursor_pos.x, line_y - height * 0.5f),
      ImVec2(handle_x, line_y + height * 0.5f),
      line_color_played,
      height * 0.5f
    );
  }

  // Draw unplayed portion (right of handle) - lighter
  if (position_ratio < 1.0f) {
    draw_list->AddRectFilled(
      ImVec2(handle_x, line_y - height * 0.5f),
      ImVec2(cursor_pos.x + width, line_y + height * 0.5f),
      line_color_unplayed,
      height * 0.5f
    );
  }

  // Draw circular handle
  draw_list->AddCircleFilled(
    ImVec2(handle_x, line_y),
    handle_radius,
    handle_color,
    16  // Number of segments for smooth circle
  );

  return value_changed;
}

// Function to calculate aspect-ratio-preserving dimensions with upscaling limit
ImVec2 calculate_thumbnail_size(
  int original_width, int original_height, float max_width, float max_height, float max_upscale_factor) {
  float aspect_ratio = static_cast<float>(original_width) / static_cast<float>(original_height);

  float calculated_width = max_width;
  float calculated_height = max_width / aspect_ratio;
  if (calculated_height > max_height) {
    calculated_height = max_height;
    calculated_width = max_height * aspect_ratio;
  }

  // Limit upscaling to the specified factor
  float width_scale = calculated_width / original_width;
  float height_scale = calculated_height / original_height;
  if (width_scale > max_upscale_factor || height_scale > max_upscale_factor) {
    float scale_factor = std::min(max_upscale_factor, std::min(width_scale, height_scale));
    calculated_width = original_width * scale_factor;
    calculated_height = original_height * scale_factor;
  }

  return ImVec2(calculated_width, calculated_height);
}

// Fancy text input box with rounded corners and shadow
bool fancy_text_input(const char* label, char* buffer, size_t buffer_size, float width,
  float padding_x, float padding_y, float corner_radius, ImGuiInputTextFlags flags) {

  ImGui::PushItemWidth(width);

  // Calculate the actual height of the text input box
  float font_height = ImGui::GetFontSize();
  float actual_input_height = font_height + (padding_y * 2.0f);

  // Style the text input with rounded corners
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, corner_radius);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding_x, padding_y));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // White background
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.98f, 0.98f, 0.98f, 1.0f)); // Slightly darker on hover
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.95f, 0.95f, 0.95f, 1.0f)); // Even darker when active

  // Draw shadow effect behind the text input
  ImVec2 shadow_offset(2.0f, 2.0f);
  ImVec2 input_pos = ImGui::GetCursorScreenPos();
  ImVec2 shadow_min(input_pos.x + shadow_offset.x, input_pos.y + shadow_offset.y);
  ImVec2 shadow_max(shadow_min.x + width, shadow_min.y + actual_input_height);

  ImGui::GetWindowDrawList()->AddRectFilled(
    shadow_min, shadow_max,
    ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.12f)),
    corner_radius
  );

  bool result = ImGui::InputText(label, buffer, buffer_size, flags);

  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(2);
  ImGui::PopItemWidth();

  return result;
}

// Custom toggle button drawing function
bool draw_type_toggle_button(const char* label, bool& toggle_state, float x_pos, float y_pos,
  float button_width, float button_height) {
  ImVec2 button_min(x_pos, y_pos);
  ImVec2 button_max(button_min.x + button_width, button_min.y + button_height);

  // Check if mouse is hovering over button
  ImVec2 mouse_pos = ImGui::GetMousePos();
  bool is_hovered = (mouse_pos.x >= button_min.x && mouse_pos.x <= button_max.x &&
    mouse_pos.y >= button_min.y && mouse_pos.y <= button_max.y);

  // Choose colors based on state
  ImVec4 bg_color = toggle_state ? Theme::TOGGLE_ON_BG :
    (is_hovered ? Theme::TOGGLE_HOVER_BG : Theme::TOGGLE_OFF_BG);
  ImVec4 border_color = toggle_state ? Theme::TOGGLE_ON_BORDER : Theme::TOGGLE_OFF_BORDER;
  ImVec4 text_color = toggle_state ? Theme::TOGGLE_ON_TEXT : Theme::TOGGLE_OFF_TEXT;

  // Draw button background
  ImGui::GetWindowDrawList()->AddRectFilled(
    button_min, button_max,
    Theme::ToImU32(bg_color),
    8.0f  // Rounded corners
  );

  // Draw button border
  ImGui::GetWindowDrawList()->AddRect(
    button_min, button_max,
    Theme::ToImU32(border_color),
    8.0f,  // Rounded corners
    0,     // No corner flags
    2.0f   // Border thickness
  );

  // Draw text centered in button
  ImVec2 text_size = ImGui::CalcTextSize(label);
  ImVec2 text_pos(
    button_min.x + (button_width - text_size.x) * 0.5f,
    button_min.y + (button_height - text_size.y) * 0.5f
  );

  ImGui::GetWindowDrawList()->AddText(
    text_pos,
    Theme::ToImU32(text_color),
    label
  );

  // Handle click detection
  bool clicked = false;
  if (is_hovered && ImGui::IsMouseClicked(0)) {
    toggle_state = !toggle_state;
    clicked = true;
  }

  return clicked;
}
