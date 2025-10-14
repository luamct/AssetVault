#include "ui.h"
#include "theme.h"
#include "utils.h"
#include "config.h"
#include "imgui.h"
#include "texture_manager.h"
#include "event_processor.h"
#include "audio_manager.h"
#include "asset.h"
#include "3d.h"
#include "search.h"
#include "logger.h"
#include "services.h"
#include "drag_drop.h"
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <filesystem>

// Clear all search and UI state when changing directories
void clear_ui_state(UIState& ui_state) {
  ui_state.results.clear();
  ui_state.results_ids.clear();
  ui_state.loaded_end_index = 0;
  ui_state.selected_asset.reset();
  ui_state.selected_asset_index = -1;
  ui_state.selected_asset_ids.clear();
  ui_state.model_preview_row = -1;
  ui_state.pending_search = false;
  ui_state.update_needed = true;
}

// TODO: move to utils.cpp
// Cross-platform file explorer opening
void open_file_in_explorer(const std::string& file_path) {
  // Extract directory path from full path (using forward slashes only)
  std::string dir_path = file_path;
  size_t last_slash = dir_path.find_last_of('/');
  if (last_slash != std::string::npos) {
    dir_path = dir_path.substr(0, last_slash);
  }
  
  std::string command;
  #ifdef _WIN32
    // Windows: Use explorer with /n flag for new window
    // Convert forward slashes to backslashes for Windows
    std::string windows_path = dir_path;
    std::replace(windows_path.begin(), windows_path.end(), '/', '\\');
    command = "explorer /n,\"" + windows_path + "\"";
  #elif __APPLE__
    // macOS: Use open to reveal the containing directory in Finder
    command = "open \"" + dir_path + "\"";
  #else
    // Linux: Use xdg-open to open containing directory
    command = "xdg-open \"" + dir_path + "\"";
  #endif
  
  // Execute the command
  int result = system(command.c_str());
  
  // Note: Windows Explorer commonly returns exit code 1 even when successful
  // Only log actual system failures (result == -1)
  if (result == -1) {
    LOG_ERROR("Failed to execute file explorer command: {}", command);
  }
}

// Render asset context menu
void render_asset_context_menu(const Asset& asset, const std::string& menu_id) {
  // Push white background color BEFORE BeginPopup
  ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::BACKGROUND_WHITE);
  
  if (ImGui::BeginPopup(menu_id.c_str())) {
    if (ImGui::MenuItem("Show in Explorer")) {
      LOG_INFO("Show in Explorer clicked for: {}", asset.path);
      open_file_in_explorer(asset.path);
    }
    
    if (ImGui::MenuItem("Copy Path")) {
      LOG_INFO("Copy Path clicked for: {}", asset.path);
      ImGui::SetClipboardText(asset.path.c_str());
    }
    
    if (ImGui::MenuItem("Show Properties")) {
      LOG_INFO("Show Properties clicked for: {}", asset.path);
      // TODO: Implement properties dialog
    }
    
    ImGui::EndPopup();
  }
  
  ImGui::PopStyleColor(); // Restore original popup background color
}

void render_clickable_path(const Asset& asset, UIState& ui_state) {
  const std::string& relative_path = asset.relative_path;

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
      bool is_active = std::find(ui_state.path_filters.begin(),
        ui_state.path_filters.end(),
        path_to_segment) != ui_state.path_filters.end();

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
          ui_state.path_filters.clear();
          ui_state.path_filter_active = false;
        }
        else {
          // Clear all existing filters and add this one
          ui_state.path_filters.clear();
          ui_state.path_filters.push_back(path_to_segment);
          ui_state.path_filter_active = true;
        }

        // Trigger search update
        ui_state.update_needed = true;
      }
    }
    else {
      // Render non-clickable segment (filename) as regular text
      ImGui::TextColored(Theme::TEXT_DARK, "%s", segments[i].c_str());
    }
  }
}

// Renders common asset information in standard order: Path, Extension, Type, Size, Modified
void render_common_asset_info(const Asset& asset, UIState& ui_state) {
  // Path
  ImGui::TextColored(Theme::TEXT_LABEL, "Path: ");
  ImGui::SameLine();
  render_clickable_path(asset, ui_state);

  // Extension
  ImGui::TextColored(Theme::TEXT_LABEL, "Extension: ");
  ImGui::SameLine();
  ImGui::Text("%s", asset.extension.c_str());

  // Type
  ImGui::TextColored(Theme::TEXT_LABEL, "Type: ");
  ImGui::SameLine();
  ImGui::Text("%s", get_asset_type_string(asset.type).c_str());

  // Size
  ImGui::TextColored(Theme::TEXT_LABEL, "Size: ");
  ImGui::SameLine();
  ImGui::Text("%s", format_file_size(asset.size).c_str());

  // Modified
  auto time_t = std::chrono::system_clock::to_time_t(asset.last_modified);
  std::tm tm_buf;
  safe_localtime(&tm_buf, &time_t);
  std::stringstream ss;
  ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
  ImGui::TextColored(Theme::TEXT_LABEL, "Modified: ");
  ImGui::SameLine();
  ImGui::Text("%s", ss.str().c_str());
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
static ImVec2 calculate_thumbnail_size(
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
  float padding_x, float padding_y, float corner_radius) {

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

  bool result = ImGui::InputText(label, buffer, buffer_size, ImGuiInputTextFlags_EnterReturnsTrue);

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

  // Draw button background with border
  ImGui::GetWindowDrawList()->AddRectFilled(button_min, button_max, Theme::ToImU32(bg_color), 8.0f);
  ImGui::GetWindowDrawList()->AddRect(button_min, button_max, Theme::ToImU32(border_color), 8.0f, 0, 2.0f);

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

void render_search_panel(
  UIState& ui_state,
  const SafeAssets& safe_assets,
  float panel_width, float panel_height) {
  ImGui::BeginChild("SearchRegion", ImVec2(panel_width, panel_height), true);

  // Get the actual usable content area (accounts for child window borders/padding)
  ImVec2 content_region = ImGui::GetContentRegionAvail();

  // Calculate centered position within content region - move search box up
  float content_search_x = (content_region.x - Config::SEARCH_BOX_WIDTH) * 0.5f;
  float content_search_y = (content_region.y - Config::SEARCH_BOX_HEIGHT) * 0.3f;

  // Get screen position for drawing (content area start + our offset)
  ImVec2 content_start = ImGui::GetCursorScreenPos();

  // Position and draw the fancy search text input
  ImGui::SetCursorPos(ImVec2(content_search_x, content_search_y));
  bool enter_pressed = fancy_text_input("##Search", ui_state.buffer, sizeof(ui_state.buffer),
    Config::SEARCH_BOX_WIDTH, 20.0f, 16.0f, 25.0f);

  // Handle search input
  std::string current_input(ui_state.buffer);

  if (enter_pressed) {
    // Immediate search on Enter key
    filter_assets(ui_state, safe_assets);
    ui_state.last_buffer = current_input;
    ui_state.input_tracking = current_input;
    ui_state.pending_search = false;
  }
  else if (current_input != ui_state.input_tracking) {
    // Debounced search: only mark as pending if input actually changed
    ui_state.input_tracking = current_input;
    ui_state.last_keypress_time = std::chrono::steady_clock::now();
    ui_state.pending_search = true;
  }

  // ============ TYPE FILTER TOGGLE BUTTONS ============

  // Position toggle buttons below the search box
  float toggles_y = content_search_y + Config::SEARCH_BOX_HEIGHT + 30.0f; // 30px gap below search box
  float toggle_button_height = 35.0f;
  float toggle_spacing = 20.0f;

  // Individual button widths - tweak these as needed
  float button_width_2d = 48.0f;      // "2D" is short
  float button_width_3d = 48.0f;      // "3D" is short
  float button_width_audio = 84.0f;   // "Audio" is longer
  float button_width_shader = 96.0f;  // "Shader" is longer
  float button_width_font = 72.0f;    // "Font" is medium
  float button_width_path = 72.0f;    // "Path" is medium

  // Calculate total width including path button if visible
  float total_toggle_width = button_width_2d + button_width_3d + button_width_audio +
    button_width_shader + button_width_font + (toggle_spacing * 4);

  // Add path button width if there's an active path filter
  if (!ui_state.path_filters.empty()) {
    total_toggle_width += button_width_path + toggle_spacing;
  }

  float toggles_start_x = content_search_x + (Config::SEARCH_BOX_WIDTH - total_toggle_width) * 0.5f;

  // Draw all toggle buttons using the dedicated function
  bool any_toggle_changed = false;
  float current_x = toggles_start_x;

  any_toggle_changed |= draw_type_toggle_button("2D", ui_state.type_filter_2d,
    content_start.x + current_x, content_start.y + toggles_y,
    button_width_2d, toggle_button_height);
  current_x += button_width_2d + toggle_spacing;

  any_toggle_changed |= draw_type_toggle_button("3D", ui_state.type_filter_3d,
    content_start.x + current_x, content_start.y + toggles_y,
    button_width_3d, toggle_button_height);
  current_x += button_width_3d + toggle_spacing;

  any_toggle_changed |= draw_type_toggle_button("Audio", ui_state.type_filter_audio,
    content_start.x + current_x, content_start.y + toggles_y,
    button_width_audio, toggle_button_height);
  current_x += button_width_audio + toggle_spacing;

  any_toggle_changed |= draw_type_toggle_button("Shader", ui_state.type_filter_shader,
    content_start.x + current_x, content_start.y + toggles_y,
    button_width_shader, toggle_button_height);
  current_x += button_width_shader + toggle_spacing;

  any_toggle_changed |= draw_type_toggle_button("Font", ui_state.type_filter_font,
    content_start.x + current_x, content_start.y + toggles_y,
    button_width_font, toggle_button_height);
  current_x += button_width_font + toggle_spacing;

  // Draw Path filter button if there's a path filter set
  if (!ui_state.path_filters.empty()) {
    // Draw the Path button
    bool path_clicked = draw_type_toggle_button("Path", ui_state.path_filter_active,
      content_start.x + current_x, content_start.y + toggles_y,
      button_width_path, toggle_button_height);

    // Add tooltip showing the full path on hover
    ImVec2 button_min(content_start.x + current_x, content_start.y + toggles_y);
    ImVec2 button_max(button_min.x + button_width_path, button_min.y + toggle_button_height);
    ImVec2 mouse_pos = ImGui::GetMousePos();
    bool is_hovered = (mouse_pos.x >= button_min.x && mouse_pos.x <= button_max.x &&
      mouse_pos.y >= button_min.y && mouse_pos.y <= button_max.y);

    if (is_hovered && !ui_state.path_filters.empty()) {
      ImGui::SetTooltip("%s", ui_state.path_filters[0].c_str());
    }

    // Handle path filter toggle
    if (path_clicked) {
      any_toggle_changed = true;
    }
  }

  // If any toggle changed, trigger immediate search
  if (any_toggle_changed) {
    filter_assets(ui_state, safe_assets);
    ui_state.pending_search = false;
  }

  ImGui::EndChild();
}

namespace {
  bool g_request_assets_path_popup = false;

  bool render_assets_directory_modal(UIState& ui_state) {
    bool directory_changed = false;
    if (g_request_assets_path_popup) {
      ImGui::OpenPopup("Select Assets Directory");
      g_request_assets_path_popup = false;

      // Initialize to current assets directory if set, otherwise home directory
      if (!ui_state.assets_directory.empty()) {
        ui_state.assets_path_selected = ui_state.assets_directory;
      }
      else {
        ui_state.assets_path_selected = get_home_directory();
      }
    }

    bool popup_style_pushed = false;
    if (ImGui::IsPopupOpen("Select Assets Directory")) {
      ImGuiViewport* viewport = ImGui::GetMainViewport();
      ImVec2 viewport_size = viewport->Size;
      ImVec2 popup_size(viewport_size.x * 0.40f, viewport_size.y * 0.50f);
      ImVec2 popup_pos(viewport->Pos.x + viewport_size.x * 0.30f,
        viewport->Pos.y + viewport_size.y * 0.25f);
      ImGui::SetNextWindowSize(popup_size);
      ImGui::SetNextWindowPos(popup_pos);
      ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.6f));
      popup_style_pushed = true;
    }

    if (ImGui::BeginPopupModal("Select Assets Directory", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
      namespace fs = std::filesystem;
      if (ui_state.assets_path_selected.empty()) {
        ui_state.assets_path_selected = get_home_directory();
      }

      fs::path current_path(ui_state.assets_path_selected);
      std::error_code fs_error;
      std::string selected_path = !ui_state.assets_path_selected.empty()
        ? ui_state.assets_path_selected
        : get_home_directory();

      ImGui::TextColored(Theme::TEXT_LABEL, "Assets directory:");
      ImGui::SameLine();
      ImGui::TextWrapped("%s", selected_path.c_str());

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      if (!fs::exists(current_path, fs_error) || !fs::is_directory(current_path, fs_error)) {
        ImGui::TextColored(Theme::TEXT_WARNING, "Directory unavailable");
      }
      else {
        std::string display_path = current_path.u8string();
        ImGui::TextWrapped("%s", display_path.c_str());

        ImGui::Spacing();
        float list_height = ImGui::GetContentRegionAvail().y - (ImGui::GetFrameHeightWithSpacing() * 2.0f);
        list_height = std::max(list_height, 160.0f);
        ImGui::BeginChild("AssetsDirectoryList", ImVec2(0.0f, list_height), true);

        // Navigate to parent directory
        fs::path parent_path = current_path.parent_path();
        if (!parent_path.empty()) {
          if (ImGui::Selectable("..", false)) {
            ui_state.assets_path_selected = parent_path.generic_u8string();
          }
        }

        fs::directory_iterator dir_iter(current_path, fs_error);
        if (fs_error) {
          ImGui::TextColored(Theme::TEXT_WARNING, "Unable to read directory contents.");
          fs_error.clear();
        }
        else {
          std::vector<fs::directory_entry> directories;
          for (const auto& entry : dir_iter) {
            std::error_code entry_error;
            if (!entry.is_directory(entry_error)) {
              continue;
            }

            std::string folder_name = entry.path().filename().u8string();
            if (folder_name.empty()) {
              continue;
            }

            if (!folder_name.empty() && folder_name[0] == '.') {
              continue;
            }

            directories.push_back(entry);
          }

          std::sort(directories.begin(), directories.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.path().filename().u8string() < b.path().filename().u8string();
          });

          for (const auto& entry : directories) {
            std::string folder_name = entry.path().filename().u8string();
            if (ImGui::Selectable(folder_name.c_str(), false)) {
              ui_state.assets_path_selected = entry.path().generic_u8string();
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
              ui_state.assets_path_selected = entry.path().generic_u8string();
              LOG_INFO("Assets directory selected: {}", ui_state.assets_path_selected);
              directory_changed = true;
              ImGui::CloseCurrentPopup();
            }
          }
        }

        ImGui::EndChild();

        ImGui::Spacing();
      }

      if (ImGui::Button("Select", ImVec2(160.0f, 0.0f))) {
        if (!ui_state.assets_path_selected.empty()) {
          LOG_INFO("Assets directory selected: {}", ui_state.assets_path_selected);
          directory_changed = true;
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }
    if (popup_style_pushed) {
      ImGui::PopStyleColor();
    }

    return directory_changed;
  }
}

void render_progress_panel(UIState& ui_state, SafeAssets& safe_assets,
  float panel_width, float panel_height) {
  ImGui::BeginChild("ProgressRegion", ImVec2(panel_width, panel_height), true);

  // Unified progress bar for all asset processing
  bool show_progress = Services::event_processor().has_pending_work();

  // Header row: left = status (only when processing), right = FPS
  {
    // Left: status label only when processing
    if (show_progress) {
      ImGui::TextColored(Theme::TEXT_HEADER, "Processing Assets");
    }

    // Right: FPS
    ImGuiIO& io = ImGui::GetIO();
    char fps_buf[32];
    snprintf(fps_buf, sizeof(fps_buf), "%.1f FPS", io.Framerate);
    ImVec2 fps_size = ImGui::CalcTextSize(fps_buf);
    float right_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - fps_size.x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(right_x);
    ImGui::Text("%s", fps_buf);
  }

  if (show_progress) {
    // Progress bar data from event processor
    float progress = Services::event_processor().get_progress();
    size_t processed = Services::event_processor().get_total_processed();
    size_t total = Services::event_processor().get_total_queued();

    // Vertically center the progress bar within the panel child
    float bar_height = ImGui::GetFrameHeight();
    float target_y = (panel_height - bar_height) * 0.5f;
    if (target_y > ImGui::GetCursorPosY()) {
      ImGui::SetCursorPosY(target_y);
    }

    // Draw progress bar without text overlay
    ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), "");

    // Overlay centered text on the progress bar
    char progress_text[64];
    snprintf(progress_text, sizeof(progress_text), "%zu/%zu", processed, total);

    ImVec2 text_size = ImGui::CalcTextSize(progress_text);
    ImVec2 progress_bar_screen_pos = ImGui::GetItemRectMin();
    ImVec2 progress_bar_screen_size = ImGui::GetItemRectSize();

    // Center text on progress bar
    ImVec2 text_pos = ImVec2(
      progress_bar_screen_pos.x + (progress_bar_screen_size.x - text_size.x) * 0.5f,
      progress_bar_screen_pos.y + (progress_bar_screen_size.y - text_size.y) * 0.5f);

    ImGui::GetWindowDrawList()->AddText(text_pos, Theme::ToImU32(Theme::TEXT_DARK), progress_text);
  }

  // Bottom-left assets path button
  float button_height = ImGui::GetFrameHeight();
  float bottom_margin = 12.0f;
  float left_margin = 12.0f;
  ImVec2 button_pos(left_margin, panel_height - button_height - bottom_margin);
  button_pos.y = std::max(button_pos.y, ImGui::GetCursorPosY());
  ImGui::SetCursorPos(button_pos);
  if (ImGui::Button("Assets Path", ImVec2(150.0f, 0.0f))) {
    g_request_assets_path_popup = true;
  }

  ImGui::EndChild();

  // Handle assets directory change
  if (render_assets_directory_modal(ui_state)) {
    const std::string new_path = ui_state.assets_path_selected;
    ui_state.assets_directory = new_path;

    // Stop all services and clear all data
    Services::stop(&safe_assets);

    clear_ui_state(ui_state);

    if (!Services::database().upsert_config_value(Config::CONFIG_KEY_ASSETS_DIRECTORY, new_path)) {
      LOG_WARN("Failed to persist assets directory configuration: {}", new_path);
    }

    // Restart event processor with new assets directory
    if (!Services::event_processor().start(ui_state.assets_directory)) {
      LOG_ERROR("Failed to restart event processor after assets directory change");
    }

    scan_for_changes(ui_state.assets_directory, std::vector<Asset>(), safe_assets);

    // File event callback to queue events for processing
    auto file_event_callback = [](const FileEvent& event) {
      LOG_TRACE("[NEW_EVENT] type = {}, asset = {}", FileWatcher::file_event_type_to_string(event.type), event.path);
      Services::event_processor().queue_event(event);
    };

    if (!Services::file_watcher().start(ui_state.assets_directory, file_event_callback, &safe_assets)) {
      LOG_ERROR("Failed to start file watcher for path: {}", ui_state.assets_directory);
    }
  }
}

void render_asset_grid(UIState& ui_state, TextureManager& texture_manager,
  SafeAssets& safe_assets, float panel_width, float panel_height) {
  ImGui::BeginChild("AssetGrid", ImVec2(panel_width, panel_height), true);

  // Show total results count if we have results
  if (!ui_state.results.empty()) {
    ImGui::Text("Showing %d of %zu results", ui_state.loaded_end_index, ui_state.results.size());
    ImGui::Separator();
  }

  // Create an inner scrolling region so the header above stays visible
  ImGui::BeginChild("AssetGridScroll", ImVec2(0, 0), false);

  // Calculate grid layout upfront since all items have the same size
  float available_width = panel_width - 20.0f;                     // Account for padding
  float item_height = Config::THUMBNAIL_SIZE + Config::TEXT_MARGIN + Config::TEXT_HEIGHT; // Full item height including text
  // Each item takes THUMBNAIL_SIZE + GRID_SPACING (spacing after each item, including last one)
  // This ensures GRID_SPACING at the end of each row to avoid scrollbar overlap
  int columns = static_cast<int>(available_width / (Config::THUMBNAIL_SIZE + Config::GRID_SPACING));
  if (columns < 1)
    columns = 1;

  // Calculate visible range
  float current_scroll_y = ImGui::GetScrollY();
  float viewport_height = ImGui::GetWindowHeight();

  // Calculate visible range for lazy loading
  float row_height = item_height + Config::GRID_SPACING;
  int first_visible_row = static_cast<int>(current_scroll_y / row_height);
  int last_visible_row = static_cast<int>((current_scroll_y + viewport_height) / row_height);

  // Add margin for smooth scrolling (1 row above/below)
  first_visible_row = std::max(0, first_visible_row - 1);
  last_visible_row = last_visible_row + 1;

  int first_visible_item = first_visible_row * columns;
  int last_visible_item = std::min(ui_state.loaded_end_index,
    (last_visible_row + 1) * columns);

  // Check if we need to load more items (when approaching the end of loaded items)
  int load_threshold_row = (ui_state.loaded_end_index - UIState::LOAD_BATCH_SIZE / 2) / columns;
  if (last_visible_row >= load_threshold_row && ui_state.loaded_end_index < static_cast<int>(ui_state.results.size())) {
    // Load more items
    ui_state.loaded_end_index = std::min(
      ui_state.loaded_end_index + UIState::LOAD_BATCH_SIZE,
      static_cast<int>(ui_state.results.size())
    );
  }

  // Reserve space for all loaded items to enable proper scrolling
  int total_loaded_rows = (ui_state.loaded_end_index + columns - 1) / columns;
  float total_content_height = total_loaded_rows * row_height;

  // Save current cursor position
  ImVec2 grid_start_pos = ImGui::GetCursorPos();

  // Reserve space for the entire loaded content
  ImGui::Dummy(ImVec2(0, total_content_height));

  // Display filtered assets in a proper grid - only process visible items within loaded range
  for (int i = first_visible_item; i < last_visible_item && i < ui_state.loaded_end_index; i++) {
    // Calculate grid position
    int row = static_cast<int>(i) / columns;
    int col = static_cast<int>(i) % columns;

    // Calculate absolute position for this grid item relative to grid start
    float x_pos = grid_start_pos.x + col * (Config::THUMBNAIL_SIZE + Config::GRID_SPACING);
    float y_pos = grid_start_pos.y + row * (item_height + Config::GRID_SPACING);

    // Set cursor to the calculated position
    ImGui::SetCursorPos(ImVec2(x_pos, y_pos));

    ImGui::BeginGroup();

    // Load texture (all items in loop are visible now)
    const TextureCacheEntry& texture_entry = texture_manager.get_asset_texture(ui_state.results[i]);

    // Calculate display size based on asset type
    ImVec2 display_size(Config::THUMBNAIL_SIZE, Config::THUMBNAIL_SIZE);

    // Check if this asset has actual thumbnail dimensions (textures or 3D model thumbnails)
    bool has_thumbnail_dimensions = false;
    if (ui_state.results[i].type == AssetType::_2D || ui_state.results[i].type == AssetType::_3D) {
      // TextureCacheEntry already contains the dimensions, no need for separate call
      has_thumbnail_dimensions = (texture_entry.width > 0 && texture_entry.height > 0);
    }

    if (has_thumbnail_dimensions) {
      display_size = calculate_thumbnail_size(
        texture_entry.width, texture_entry.height,
        Config::THUMBNAIL_SIZE, Config::THUMBNAIL_SIZE,
        Config::MAX_THUMBNAIL_UPSCALE_FACTOR
      ); // upscaling for grid
    }
    else {
      // For type icons, use a fixed fraction of the thumbnail size
      display_size = ImVec2(Config::THUMBNAIL_SIZE * Config::ICON_SCALE, Config::THUMBNAIL_SIZE * Config::ICON_SCALE);
    }

    // Create a fixed-size container for consistent layout
    ImVec2 container_size(Config::THUMBNAIL_SIZE,
      Config::THUMBNAIL_SIZE + Config::TEXT_MARGIN + Config::TEXT_HEIGHT); // Thumbnail + text area
    ImVec2 container_pos = ImGui::GetCursorScreenPos();

    // Draw background for the container (same as app background)
    ImGui::GetWindowDrawList()->AddRectFilled(
      container_pos, ImVec2(container_pos.x + container_size.x, container_pos.y + container_size.y),
      Theme::ToImU32(Theme::BACKGROUND_LIGHT_BLUE_1));

    // Center the image/icon in the thumbnail area
    float image_x_offset = (Config::THUMBNAIL_SIZE - display_size.x) * 0.5f;
    float image_y_offset = (Config::THUMBNAIL_SIZE - display_size.y) * 0.5f;
    ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);

    ImGui::PushStyleColor(ImGuiCol_Button, Theme::COLOR_TRANSPARENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::COLOR_TRANSPARENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COLOR_SEMI_TRANSPARENT);
    // Remove frame padding so ImageButton is exactly display_size (no extra space added)
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

    // Display thumbnail image - now exactly matches our grid sizing
    ImGui::SetCursorScreenPos(image_pos);
    if (ImGui::ImageButton(
      ("##Thumbnail" + std::to_string(i)).c_str(),
      (ImTextureID) (intptr_t) texture_entry.get_texture_id(),
      display_size)) {

      ImGuiIO& io = ImGui::GetIO();
      // Check for Cmd (macOS) or Ctrl (Windows/Linux) modifier
      bool modifier_pressed = io.KeySuper || io.KeyCtrl;
      uint32_t clicked_id = ui_state.results[i].id;

      if (modifier_pressed) {
        // Multi-selection mode: toggle the clicked asset
        if (ui_state.selected_asset_ids.count(clicked_id)) {
          // Asset is already selected, remove it
          ui_state.selected_asset_ids.erase(clicked_id);
          LOG_DEBUG("Removed from selection: {}", ui_state.results[i].name);

          // If we removed the currently previewed asset, update preview to another selected asset
          if (ui_state.selected_asset && ui_state.selected_asset->id == clicked_id) {
            if (!ui_state.selected_asset_ids.empty()) {
              // Find first selected asset to preview
              for (const auto& result : ui_state.results) {
                if (ui_state.selected_asset_ids.count(result.id)) {
                  ui_state.selected_asset = result;
                  ui_state.selected_asset_index = static_cast<int>(&result - &ui_state.results[0]);
                  break;
                }
              }
            } else {
              // No more selected assets
              ui_state.selected_asset.reset();
              ui_state.selected_asset_index = -1;
            }
          }
        } else {
          // Asset not selected, add it
          ui_state.selected_asset_ids.insert(clicked_id);
          ui_state.selected_asset_index = static_cast<int>(i);
          ui_state.selected_asset = ui_state.results[i];
          LOG_DEBUG("Added to selection: {}", ui_state.results[i].name);
        }
      } else {
        // Normal click: clear all selections and select only this asset
        ui_state.selected_asset_ids.clear();
        ui_state.selected_asset_ids.insert(clicked_id);
        ui_state.selected_asset_index = static_cast<int>(i);
        ui_state.selected_asset = ui_state.results[i];
        LOG_DEBUG("Selected (single): {}", ui_state.results[i].name);
      }
    }

    ImGui::PopStyleVar();  // Restore frame padding

    // Get the actual rendered bounds of the ImageButton for the selection border
    ImVec2 thumbnail_min = ImGui::GetItemRectMin();
    ImVec2 thumbnail_max = ImGui::GetItemRectMax();

    // Handle drag-and-drop to external applications (Finder, Explorer, etc.)
    // Only initiate drag once per gesture to avoid multiple drag sessions
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f)) {
      if (!ui_state.drag_initiated) {
        // Get mouse position for drag origin
        ImVec2 mouse_pos = ImGui::GetMousePos();

        std::vector<std::string> files_to_drag;

        // If multiple assets are selected, drag all selected assets
        if (ui_state.selected_asset_ids.size() > 1) {
          // Collect all selected assets and their related files
          for (const auto& result : ui_state.results) {
            if (ui_state.selected_asset_ids.count(result.id)) {
              std::vector<std::string> related = find_related_files(result);
              files_to_drag.insert(files_to_drag.end(), related.begin(), related.end());
            }
          }
          LOG_DEBUG("Started drag for {} selected assets (with {} total file(s))",
                    ui_state.selected_asset_ids.size(), files_to_drag.size());
        } else {
          // Single asset: find all related files (e.g., MTL for OBJ, textures for 3D models)
          files_to_drag = find_related_files(ui_state.results[i]);
          LOG_DEBUG("Started drag for: {} (with {} related file(s))",
                    ui_state.results[i].name, files_to_drag.size());
        }

        // Start drag operation with all files
        if (Services::drag_drop_manager().is_supported()) {
          if (Services::drag_drop_manager().begin_file_drag(files_to_drag, mouse_pos)) {
            ui_state.drag_initiated = true;  // Mark drag as initiated
          }
        }
      }
    }

    // Handle right-click context menu (no selection change)
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
      ImGui::OpenPopup(("AssetContextMenu##" + std::to_string(i)).c_str());
    }

    // Render context menu using dedicated method
    render_asset_context_menu(ui_state.results[i], "AssetContextMenu##" + std::to_string(i));

    ImGui::PopStyleColor(3);

    // Check if this asset is selected (for visual highlights)
    bool is_selected = ui_state.selected_asset_ids.count(ui_state.results[i].id) > 0;

    // Draw selection highlight border around the actual thumbnail bounds if this asset is selected
    if (is_selected) {
      ImGui::GetWindowDrawList()->AddRect(
        thumbnail_min,
        thumbnail_max,
        Theme::ToImU32(Theme::ACCENT_BLUE_1),
        4.0f,  // Corner rounding
        0,     // Flags
        3.0f   // Border thickness
      );
    }

    // Position text at the bottom of the container
    ImGui::SetCursorScreenPos(ImVec2(container_pos.x, container_pos.y + Config::THUMBNAIL_SIZE + Config::TEXT_MARGIN));

    // Asset name below thumbnail with selection highlight
    std::string truncated_name = truncate_filename(ui_state.results[i].name, Config::TEXT_MAX_LENGTH);
    ImVec2 text_size = ImGui::CalcTextSize(truncated_name.c_str());
    float text_x_offset = (Config::THUMBNAIL_SIZE - text_size.x) * 0.5f;

    // Draw blue background for selected text (similar to OS file explorers)
    if (is_selected) {
      ImVec2 text_bg_min(container_pos.x, container_pos.y + Config::THUMBNAIL_SIZE + Config::TEXT_MARGIN);
      ImVec2 text_bg_max(container_pos.x + Config::THUMBNAIL_SIZE,
                         text_bg_min.y + Config::TEXT_HEIGHT);
      ImGui::GetWindowDrawList()->AddRectFilled(
        text_bg_min,
        text_bg_max,
        Theme::ToImU32(Theme::ACCENT_BLUE_1),
        2.0f  // Slight rounding for the text background
      );
    }

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + text_x_offset);

    // Use white text for selected items, normal text color otherwise
    if (is_selected) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
      ImGui::TextWrapped("%s", truncated_name.c_str());
      ImGui::PopStyleColor();
    } else {
      ImGui::TextWrapped("%s", truncated_name.c_str());
    }

    ImGui::EndGroup();
  }

  // Show message if no assets found
  if (ui_state.results.empty()) {
    bool assets_empty;
    {
      auto [lock, assets] = safe_assets.read();
      assets_empty = assets.empty();
    }
    if (assets_empty) {
      ImGui::TextColored(Theme::TEXT_DISABLED_DARK, "No assets found. Add files to the 'assets' directory.");
    }
    else {
      ImGui::TextColored(Theme::TEXT_DISABLED_DARK, "No assets match your search.");
    }
  }

  // Deselect all when clicking on grid background (not on any asset)
  if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
    ui_state.selected_asset_ids.clear();
    ui_state.selected_asset_index = -1;
    ui_state.selected_asset.reset();
  }

  // End inner scrolling region (grid)
  ImGui::EndChild();

  // Reset drag state when mouse button is released
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    ui_state.drag_initiated = false;
  }

  // End outer container
  ImGui::EndChild();
}

void render_preview_panel(UIState& ui_state, TextureManager& texture_manager,
  Model& current_model, Camera3D& camera, float panel_width, float panel_height) {
  ImGui::BeginChild("AssetPreview", ImVec2(panel_width, panel_height), true);

  // Use fixed panel dimensions for stable calculations
  float avail_width = panel_width - Config::PREVIEW_INTERNAL_PADDING; // Account for ImGui padding and margins
  float avail_height = avail_width;                           // Square aspect ratio for preview area

  // Track previously selected asset for cleanup
  static uint32_t prev_selected_id = 0;
  static AssetType prev_selected_type = AssetType::Unknown;

  // Handle asset selection changes (by id)
  if ((ui_state.selected_asset ? ui_state.selected_asset->id : 0) != prev_selected_id) {
    if (prev_selected_type == AssetType::Audio && Services::audio_manager().has_audio_loaded()) {
      Services::audio_manager().unload_audio();
    }
    prev_selected_id = ui_state.selected_asset ? ui_state.selected_asset->id : 0;
    prev_selected_type = (ui_state.selected_asset.has_value()) ? ui_state.selected_asset->type : AssetType::Unknown;
  }

  // Validate current selection against filtered results (no disk access)
  if (ui_state.selected_asset_index >= 0) {
    // Bounds check for highlight index only
    if (ui_state.selected_asset_index >= static_cast<int>(ui_state.results.size())) {
      ui_state.selected_asset_index = -1;
    }
  }

  // Clean up selected_asset_ids: remove any IDs that are no longer in results
  std::vector<uint32_t> ids_to_remove;
  for (uint32_t id : ui_state.selected_asset_ids) {
    if (ui_state.results_ids.find(id) == ui_state.results_ids.end()) {
      ids_to_remove.push_back(id);
    }
  }
  for (uint32_t id : ids_to_remove) {
    ui_state.selected_asset_ids.erase(id);
  }

  // If the preview asset is no longer in results, clear it
  if (ui_state.selected_asset &&
      ui_state.results_ids.find(ui_state.selected_asset->id) == ui_state.results_ids.end()) {
    ui_state.selected_asset_index = -1;
    ui_state.selected_asset.reset();
  }

  if (ui_state.selected_asset.has_value()) {
    const Asset& selected_asset = *ui_state.selected_asset;

    // Check if selected asset is a model
    if (selected_asset.type == AssetType::_3D && texture_manager.is_preview_initialized()) {
      // Load the model if it's different from the currently loaded one
      if (selected_asset.path != current_model.path) {
        LOG_DEBUG("=== Loading Model in Main ===");
        LOG_DEBUG("Selected asset: {}", selected_asset.path);
        Model model;
        bool load_success = load_model(selected_asset.path, model, texture_manager);
        if (load_success) {
          set_current_model(current_model, model);
          camera.reset(); // Reset camera to default view for new model
          LOG_DEBUG("Model loaded successfully in main");
        }
        else {
          LOG_DEBUG("Failed to load model in main");
        }
        LOG_DEBUG("===========================");
      }

      // Get the current model for displaying info
      const Model& current_model_ref = get_current_model(current_model);

      // 3D Preview Viewport for models
      ImVec2 viewport_size(avail_width, avail_height);

      // Render the 3D preview to framebuffer texture
      int fb_width = static_cast<int>(avail_width);
      int fb_height = static_cast<int>(avail_height);
      render_3d_preview(fb_width, fb_height, current_model, texture_manager, camera);

      // Center the viewport in the panel (same logic as 2D previews)
      ImVec2 container_pos = ImGui::GetCursorScreenPos();
      float image_x_offset = (avail_width - viewport_size.x) * 0.5f;
      float image_y_offset = (avail_height - viewport_size.y) * 0.5f;
      ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);
      ImGui::SetCursorScreenPos(image_pos);

      // Draw border around the viewport
      ImVec2 border_min = image_pos;
      ImVec2 border_max(border_min.x + viewport_size.x, border_min.y + viewport_size.y);
      ImGui::GetWindowDrawList()->AddRect(border_min, border_max, Theme::COLOR_BORDER_GRAY_U32, 8.0f, 0, 1.0f);

      // Display the 3D viewport
      ImGui::Image((ImTextureID) (intptr_t) texture_manager.get_preview_texture(), viewport_size);

      // Handle mouse interactions for 3D camera control
      bool is_image_hovered = ImGui::IsItemHovered();

      if (is_image_hovered) {
        ImGuiIO& io = ImGui::GetIO();

        // Handle mouse wheel for zoom
        if (io.MouseWheel != 0.0f) {
          if (io.MouseWheel > 0.0f) {
            camera.zoom *= Config::PREVIEW_3D_ZOOM_FACTOR; // Zoom in
          }
          else {
            camera.zoom /= Config::PREVIEW_3D_ZOOM_FACTOR; // Zoom out
          }
          // Clamp zoom to reasonable bounds
          camera.zoom = std::max(0.1f, std::min(camera.zoom, 10.0f));
        }

        // Handle double-click for reset (check this first)
        if (ImGui::IsMouseDoubleClicked(0)) {
          camera.reset();
          camera.is_dragging = false; // Cancel any dragging
        }
        // Handle single click to start dragging (only if not double-click)
        else if (ImGui::IsMouseClicked(0)) {
          camera.is_dragging = true;
          camera.last_mouse_x = io.MousePos.x;
          camera.last_mouse_y = io.MousePos.y;
        }
      }

      // Handle dragging (can continue even if mouse leaves image area)
      if (camera.is_dragging) {
        ImGuiIO& io = ImGui::GetIO();

        // Check if mouse button is still held down
        if (io.MouseDown[0]) {
          float delta_x = io.MousePos.x - camera.last_mouse_x;
          float delta_y = io.MousePos.y - camera.last_mouse_y;

          // Only update if there's actual movement
          if (delta_x != 0.0f || delta_y != 0.0f) {
            // Update rotation based on mouse movement using config sensitivity
            camera.rotation_y += delta_x * Config::PREVIEW_3D_ROTATION_SENSITIVITY; // Horizontal rotation (left/right)
            camera.rotation_x += delta_y * Config::PREVIEW_3D_ROTATION_SENSITIVITY; // Vertical rotation (up/down)

            // Clamp vertical rotation to avoid flipping
            camera.rotation_x = std::max(-89.0f, std::min(camera.rotation_x, 89.0f));

            camera.last_mouse_x = io.MousePos.x;
            camera.last_mouse_y = io.MousePos.y;
          }
        }
        else {
          // Mouse button released, stop dragging
          camera.is_dragging = false;
        }
      }

      // Restore cursor for info below
      ImGui::SetCursorScreenPos(container_pos);
      ImGui::Dummy(ImVec2(0, avail_height + 10));

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Common asset information
      render_common_asset_info(selected_asset, ui_state);

      // 3D-specific information
      if (current_model_ref.loaded) {
        int vertex_count =
          static_cast<int>(current_model_ref.vertices.size() / 8);             // 8 floats per vertex (3 pos + 3 normal + 2 tex)
        int face_count = static_cast<int>(current_model_ref.indices.size() / 3); // 3 indices per triangle

        ImGui::TextColored(Theme::TEXT_LABEL, "Vertices: ");
        ImGui::SameLine();
        ImGui::Text("%d", vertex_count);

        ImGui::TextColored(Theme::TEXT_LABEL, "Faces: ");
        ImGui::SameLine();
        ImGui::Text("%d", face_count);
      }
    }
    else if (selected_asset.type == AssetType::Audio && Services::audio_manager().is_initialized()) {
      // Audio handling for sound assets

      // Load the audio file if it's different from the currently loaded one
      const std::string asset_path = selected_asset.path;
      const std::string current_file = Services::audio_manager().get_current_file();

      if (asset_path != current_file) {
        LOG_DEBUG("Main: Audio file changed from '{}' to '{}'", current_file, asset_path);
        bool loaded = Services::audio_manager().load_audio(asset_path);
        if (loaded) {
          // Set initial volume to match our slider default
          Services::audio_manager().set_volume(0.5f);
          // Auto-play if enabled
          if (ui_state.auto_play_audio) {
            Services::audio_manager().play();
          }
        }
        else {
          LOG_DEBUG("Main: Failed to load audio, current_file is now '{}'", Services::audio_manager().get_current_file());
        }
      }

      // Display audio icon in preview area
      const TextureCacheEntry& audio_entry = texture_manager.get_asset_texture(selected_asset);
      if (audio_entry.get_texture_id() != 0) {
        float icon_dim = Config::ICON_SCALE * std::min(avail_width, avail_height);
        ImVec2 icon_size(icon_dim, icon_dim);

        // Center the icon
        ImVec2 container_pos = ImGui::GetCursorScreenPos();
        float image_x_offset = (avail_width - icon_size.x) * 0.5f;
        float image_y_offset = (avail_height - icon_size.y) * 0.5f;
        ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);
        ImGui::SetCursorScreenPos(image_pos);

        ImGui::Image((ImTextureID) (intptr_t) audio_entry.get_texture_id(), icon_size);

        // Restore cursor for controls below
        ImGui::SetCursorScreenPos(container_pos);
        ImGui::Dummy(ImVec2(0, avail_height + 10));
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Audio controls - single row layout
      if (Services::audio_manager().has_audio_loaded()) {
        float duration = Services::audio_manager().get_duration();
        float position = Services::audio_manager().get_position();
        bool is_playing = Services::audio_manager().is_playing();

        // Format time helper lambda
        auto format_time = [](float seconds) -> std::string {
          int mins = static_cast<int>(seconds) / 60;
          int secs = static_cast<int>(seconds) % 60;
          char buffer[16];
          snprintf(buffer, sizeof(buffer), "%02d:%02d", mins, secs);
          return std::string(buffer);
          };

        // Create a single row with all controls
        ImGui::BeginGroup();

        // 1. Square Play/Pause button with transparent background
        const float button_size = 32.0f;

        // Store the baseline Y position BEFORE drawing the button for proper alignment
        float baseline_y = ImGui::GetCursorPosY();

        unsigned int icon_texture = is_playing ? texture_manager.get_pause_icon() : texture_manager.get_play_icon();

        // Make button background transparent
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 0.1f)); // Very light hover
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.7f, 0.7f, 0.2f));  // Light press

        if (ImGui::ImageButton("##PlayPause", (ImTextureID) (intptr_t) icon_texture, ImVec2(button_size, button_size))) {
          if (is_playing) {
            Services::audio_manager().pause();
          }
          else {
            Services::audio_manager().play();
          }
        }

        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 8); // 8px spacing

        // 2. Current timestamp
        ImGui::SetCursorPosY(baseline_y + button_size * 0.5f - 6.0);
        ImGui::Text("%s", format_time(position).c_str());

        ImGui::SameLine(0, 16);

        // 3. Custom seek bar - thin line with circle handle
        static bool seeking = false;
        static float seek_position = 0.0f;

        if (!seeking) {
          seek_position = position;
        }

        const float seek_bar_width = 120.0f;
        const float seek_bar_height = 4.0f; // Thin line height

        // Use our custom seek bar - vertically centered
        ImGui::SetCursorPosY(baseline_y + button_size * 0.5f - seek_bar_height); // Center based on handle radius
        bool seek_changed = audio_seek_bar("##CustomSeek", &seek_position, 0.0f, duration, seek_bar_width, seek_bar_height);

        if (seek_changed) {
          seeking = true;
          Services::audio_manager().set_position(seek_position);
        }

        // Reset seeking flag when not actively dragging
        if (seeking && !ImGui::IsItemActive()) {
          seeking = false;
        }

        ImGui::SameLine(0, 12);

        // 4. Total duration
        ImGui::SetCursorPosY(baseline_y + button_size * 0.5f - 6.0f);
        ImGui::Text("%s", format_time(duration).c_str());

        ImGui::SameLine(0, 12);

        // 5. Speaker icon - vertically centered
        const float icon_size = 24.0f;
        ImGui::SetCursorPosY(baseline_y + (button_size - 0.5 * icon_size) * 0.5f);
        ImGui::Image((ImTextureID) (intptr_t) texture_manager.get_speaker_icon(), ImVec2(icon_size, icon_size));

        ImGui::SameLine(0, 6);

        // 6. Volume slider - custom horizontal seek bar style
        static float audio_volume = 0.5f; // Start at 50%
        const float volume_width = 60.0f;  // Small horizontal slider
        const float volume_height = 3.0f;   // Thinner than seek bar

        ImGui::SetCursorPosY(baseline_y + button_size * 0.5f);

        if (audio_seek_bar("##VolumeBar", &audio_volume, 0.0f, 1.0f, volume_width, volume_height)) {
          Services::audio_manager().set_volume(audio_volume);
        }

        // Show percentage on hover
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Volume: %d%%", (int) (audio_volume * 100));
        }

        ImGui::EndGroup();

        // Auto-play checkbox below the player
        ImGui::Spacing();
        ImGui::Checkbox("Auto-play", &ui_state.auto_play_audio);
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Common asset information
      render_common_asset_info(selected_asset, ui_state);
    }
    else if (selected_asset.extension == ".gif") {
      // Animated GIF Preview - load on-demand (similar to 3D models)
      // Check if we need to load the animation
      if (!ui_state.current_animation || ui_state.current_animation_path != selected_asset.path) {
        LOG_DEBUG("[UI] Loading animated GIF on-demand: {}", selected_asset.path);
        ui_state.current_animation = texture_manager.load_animated_gif(selected_asset.path);
        ui_state.current_animation_path = selected_asset.path;
      }

      if (ui_state.current_animation && !ui_state.current_animation->frame_textures.empty()) {
        // Calculate current frame based on elapsed time
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - ui_state.current_animation->animation_start_time).count();

        // Note: stb_image returns delays in milliseconds (despite GIF spec using centiseconds)
        const int DEFAULT_FRAME_DELAY = 100; // 100ms default for frames with 0 delay

        // Calculate total animation duration in milliseconds
        int total_delay = 0;
        for (int delay : ui_state.current_animation->frame_delays) {
          total_delay += (delay > 0) ? delay : DEFAULT_FRAME_DELAY;
        }

        // Find current frame based on accumulated delays
        int current_frame = 0;
        if (total_delay > 0) {
          // Loop animation
          int time_in_loop = static_cast<int>(elapsed_ms % total_delay);

          // Find which frame corresponds to this time
          int accumulated = 0;
          for (size_t i = 0; i < ui_state.current_animation->frame_delays.size(); i++) {
            int frame_delay = (ui_state.current_animation->frame_delays[i] > 0)
              ? ui_state.current_animation->frame_delays[i]
              : DEFAULT_FRAME_DELAY;
            accumulated += frame_delay;
            if (time_in_loop < accumulated) {
              current_frame = static_cast<int>(i);
              break;
            }
          }
        }

        // Calculate preview size
        ImVec2 preview_size(avail_width, avail_height);
        if (ui_state.current_animation->width > 0 && ui_state.current_animation->height > 0) {
          preview_size = calculate_thumbnail_size(
            ui_state.current_animation->width,
            ui_state.current_animation->height,
            avail_width, avail_height,
            Config::MAX_PREVIEW_UPSCALE_FACTOR
          );
        }

        // Center the preview image in the panel
        ImVec2 container_pos = ImGui::GetCursorScreenPos();
        float image_x_offset = (avail_width - preview_size.x) * 0.5f;
        float image_y_offset = (avail_height - preview_size.y) * 0.5f;
        ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);
        ImGui::SetCursorScreenPos(image_pos);

        // Draw border around the image
        ImVec2 border_min = image_pos;
        ImVec2 border_max(border_min.x + preview_size.x, border_min.y + preview_size.y);
        ImGui::GetWindowDrawList()->AddRect(border_min, border_max, Theme::COLOR_BORDER_GRAY_U32, 8.0f, 0, 1.0f);

        // Display current frame
        unsigned int frame_texture = ui_state.current_animation->frame_textures[current_frame];
        ImGui::Image((ImTextureID) (intptr_t) frame_texture, preview_size);

        // Restore cursor for info below
        ImGui::SetCursorScreenPos(container_pos);
        ImGui::Dummy(ImVec2(0, avail_height + 10));
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Common asset information
      render_common_asset_info(selected_asset, ui_state);

      // GIF-specific information (dimensions)
      if (ui_state.current_animation) {
        ImGui::TextColored(Theme::TEXT_LABEL, "Dimensions: ");
        ImGui::SameLine();
        ImGui::Text("%dx%d", ui_state.current_animation->width, ui_state.current_animation->height);

        ImGui::TextColored(Theme::TEXT_LABEL, "Frames: ");
        ImGui::SameLine();
        ImGui::Text("%zu", ui_state.current_animation->frame_textures.size());
      }
    }
    else {
      // 2D Preview for non-GIF assets
      const TextureCacheEntry& preview_entry = texture_manager.get_asset_texture(selected_asset);
      if (preview_entry.get_texture_id() != 0) {
        ImVec2 preview_size(avail_width, avail_height);

        if (selected_asset.type == AssetType::_2D) {
          // TextureCacheEntry already contains dimensions
          if (preview_entry.width > 0 && preview_entry.height > 0) {
            preview_size = calculate_thumbnail_size(preview_entry.width, preview_entry.height, avail_width, avail_height, Config::MAX_PREVIEW_UPSCALE_FACTOR);
          }
        }
        else {
          // For type icons, use ICON_SCALE * min(available_width, available_height)
          float icon_dim = Config::ICON_SCALE * std::min(avail_width, avail_height);
          preview_size = ImVec2(icon_dim, icon_dim);
        }

        // Center the preview image in the panel (same logic as grid)
        ImVec2 container_pos = ImGui::GetCursorScreenPos();
        float image_x_offset = (avail_width - preview_size.x) * 0.5f;
        float image_y_offset = (avail_height - preview_size.y) * 0.5f;
        ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);
        ImGui::SetCursorScreenPos(image_pos);

        // Draw border around the image
        ImVec2 border_min = image_pos;
        ImVec2 border_max(border_min.x + preview_size.x, border_min.y + preview_size.y);
        ImGui::GetWindowDrawList()->AddRect(border_min, border_max, Theme::COLOR_BORDER_GRAY_U32, 8.0f, 0, 1.0f);

        // Display static image
        ImGui::Image((ImTextureID) (intptr_t) preview_entry.get_texture_id(), preview_size);

        // Restore cursor for info below
        ImGui::SetCursorScreenPos(container_pos);
        ImGui::Dummy(ImVec2(0, avail_height + 10));
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Common asset information
      render_common_asset_info(selected_asset, ui_state);

      // 2D-specific information
      if (selected_asset.type == AssetType::_2D) {
        int width, height;
        if (texture_manager.get_texture_dimensions(selected_asset.path, width, height)) {
          ImGui::TextColored(Theme::TEXT_LABEL, "Dimensions: ");
          ImGui::SameLine();
          ImGui::Text("%dx%d", width, height);
        }
      }
    }
  }
  else {
    ImGui::TextColored(Theme::TEXT_DISABLED_DARK, "No asset selected");
    ImGui::TextColored(Theme::TEXT_DISABLED_DARK, "Click on an asset to preview");
  }

  ImGui::EndChild();
}
