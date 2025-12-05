#include "ui/ui.h"
#include "theme.h"
#include "config.h"
#include "texture_manager.h"
#include "utils.h"
#include "services.h"
#include "drag_drop.h"
#include "logger.h"
#include "ui/components.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_set>

constexpr float GRID_ZOOM_BASE_UNIT = 80.0f;
constexpr float GRID_ZOOM_STEP = 0.4f;
constexpr ZoomLevel GRID_ZOOM_DEFAULT_LEVEL = ZoomLevel::Level3;
constexpr int GRID_ZOOM_MIN_LEVEL = static_cast<int>(ZoomLevel::Level0);
constexpr int GRID_ZOOM_MAX_LEVEL = static_cast<int>(ZoomLevel::Level5);
constexpr float RESULTS_TEXT_HEIGHT = 20.0f;
constexpr float RESULTS_GRID_SPACING = 15.0f;
constexpr float RESULTS_THUMBNAIL_CORNER_RADIUS = 9.0f;

constexpr int zoom_level_index(ZoomLevel level) {
  return static_cast<int>(level);
}

float zoom_level_to_multiplier(ZoomLevel level) {
  return 1.0f + zoom_level_index(level) * GRID_ZOOM_STEP;
}

float zoom_level_to_thumbnail_size(ZoomLevel level) {
  return zoom_level_to_multiplier(level) * GRID_ZOOM_BASE_UNIT;
}

bool apply_grid_zoom_delta(UIState& ui_state, int delta) {
  int current = zoom_level_index(ui_state.grid_zoom_level);
  int next = std::clamp(current + delta, GRID_ZOOM_MIN_LEVEL, GRID_ZOOM_MAX_LEVEL);
  if (next == current) {
    return false;
  }
  ui_state.grid_zoom_level = static_cast<ZoomLevel>(next);
  Config::set_grid_zoom_level(next);
  return true;
}

void ensure_grid_zoom_level(UIState& ui_state) {
  int level = zoom_level_index(ui_state.grid_zoom_level);
  if (level < GRID_ZOOM_MIN_LEVEL || level > GRID_ZOOM_MAX_LEVEL) {
    ui_state.grid_zoom_level = GRID_ZOOM_DEFAULT_LEVEL;
    Config::set_grid_zoom_level(zoom_level_index(GRID_ZOOM_DEFAULT_LEVEL));
  }
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
    ImGui::EndPopup();
  }
  
  ImGui::PopStyleColor(); // Restore original popup background color
}

void render_asset_grid(UIState& ui_state, TextureManager& texture_manager,
  SafeAssets& safe_assets, float panel_width, float panel_height) {
  ImGui::BeginChild("AssetGrid", ImVec2(panel_width, panel_height), false);

  ensure_grid_zoom_level(ui_state);
  float zoom_multiplier = 1.0f;
  float thumbnail_size = 0.0f;
  int zoom_index = 0;
  bool can_zoom_out = false;
  bool can_zoom_in = false;

  auto refresh_zoom_state = [&]() {
    zoom_multiplier = zoom_level_to_multiplier(ui_state.grid_zoom_level);
    thumbnail_size = zoom_level_to_thumbnail_size(ui_state.grid_zoom_level);
    zoom_index = zoom_level_index(ui_state.grid_zoom_level);
    can_zoom_out = zoom_index > GRID_ZOOM_MIN_LEVEL;
    can_zoom_in = zoom_index < GRID_ZOOM_MAX_LEVEL;
  };

  refresh_zoom_state();

  auto log_zoom_change = [&](const char* direction) {
    LOG_INFO("Grid zoom {}: level={} upscale={:.1f} thumbnail={:.1f}",
      direction, zoom_index, zoom_multiplier, thumbnail_size);
  };

  auto apply_zoom_delta_and_log = [&](int delta, const char* direction) {
    if (apply_grid_zoom_delta(ui_state, delta)) {
      refresh_zoom_state();
      log_zoom_change(direction);
    }
  };

  size_t total_indexed_assets = 0;
  {
    auto [lock, assets] = safe_assets.read();
    total_indexed_assets = assets.size();
  }

  // Keyboard shortcuts: Cmd/Ctrl + '=' or '-' to adjust zoom
  ImGuiIO& grid_io = ImGui::GetIO();
  bool modifier_down = (grid_io.KeySuper || grid_io.KeyCtrl);
  if (modifier_down && !grid_io.WantTextInput) {
    bool zoom_in_pressed = ImGui::IsKeyPressed(ImGuiKey_Equal, false) ||
      ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false);
    bool zoom_out_pressed = ImGui::IsKeyPressed(ImGuiKey_Minus, false) ||
      ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false);

    if (zoom_in_pressed) {
      apply_zoom_delta_and_log(1, "increased");
    }
    if (zoom_out_pressed) {
      apply_zoom_delta_and_log(-1, "decreased");
    }
  }

  // Always show results header (status text + zoom buttons)
  ImVec2 label_pos = ImGui::GetCursorPos();
  ImGuiStyle& style = ImGui::GetStyle();
  float button_size = ImGui::GetFrameHeight() * 1.25;

  ImFont* larger_font = Theme::get_primary_font_large();
  if (larger_font) {
    ImGui::PushFont(larger_font);
  }

  float text_line_height = ImGui::GetTextLineHeight();
  float text_y_offset = std::max(0.0f, (button_size - text_line_height) * 0.5f);
  ImGui::SetCursorPos(ImVec2(label_pos.x, label_pos.y + text_y_offset));

  bool open_assets_modal_from_header = false;
  const bool has_assets_directory = !ui_state.assets_directory.empty();
  if (!has_assets_directory) {
    const char* prompt = "Select an assets folder to index";
    ImGui::TextColored(Theme::ACCENT_BLUE_1, "%s", prompt);

    ImVec2 prompt_size = ImGui::GetItemRectSize();
    float folder_button_size = button_size * 0.8f;
    float text_top = label_pos.y + text_y_offset;
    float folder_button_y = text_top + (text_line_height - folder_button_size) * 0.5f;
    folder_button_y = std::max(folder_button_y, label_pos.y);

    IconButtonParams folder_button;
    folder_button.id = "SelectAssetsFolderIcon";
    folder_button.cursor_pos = ImVec2(label_pos.x + prompt_size.x + style.ItemSpacing.x, folder_button_y);
    folder_button.size = folder_button_size;
    folder_button.icon_texture = texture_manager.get_folder_icon();
    folder_button.fallback_label = "F";

    if (draw_icon_button(folder_button)) {
      open_assets_modal_from_header = true;
    }
  }
  else {
    size_t matched_count = ui_state.results.size();
    float base_font_size = ImGui::GetFontSize();
    float font_scale = (base_font_size + 2.0f) / std::max(1.0f, base_font_size);
    ImGui::SetWindowFontScale(font_scale);
    ImGui::Text("Showing %zu out of %zu assets", matched_count, total_indexed_assets);
    ImGui::SetWindowFontScale(1.0f);
  }

  if (larger_font) {
    ImGui::PopFont();
  }
  ImVec2 label_size = ImGui::GetItemRectSize();

  float total_button_width = button_size * 2.0f + style.ItemSpacing.x;
  float button_x = ImGui::GetWindowContentRegionMax().x - total_button_width;
  float button_y = label_pos.y;

  ImVec2 minus_pos(button_x, button_y);
  ImVec2 plus_pos(button_x + button_size + style.ItemSpacing.x, button_y);

  unsigned int zoom_out_icon = texture_manager.get_zoom_out_icon();
  unsigned int zoom_in_icon = texture_manager.get_zoom_in_icon();
  IconButtonParams minus_button;
  minus_button.id = "GridScaleMinus";
  minus_button.cursor_pos = minus_pos;
  minus_button.size = button_size;
  minus_button.icon_texture = zoom_out_icon;
  minus_button.fallback_label = "-";
  minus_button.enabled = can_zoom_out;
  minus_button.highlight_color = Theme::ACCENT_BLUE_1_ALPHA_80;
  if (draw_icon_button(minus_button)) {
    apply_zoom_delta_and_log(-1, "decreased");
  }

  IconButtonParams plus_button = minus_button;
  plus_button.id = "GridScalePlus";
  plus_button.cursor_pos = plus_pos;
  plus_button.icon_texture = zoom_in_icon;
  plus_button.fallback_label = "+";
  plus_button.enabled = can_zoom_in;
  if (draw_icon_button(plus_button)) {
    apply_zoom_delta_and_log(1, "increased");
  }

  // Reset cursor to line height below the taller of text/buttons before separator
  float header_height = std::max(label_size.y, button_size);
  ImVec2 cursor = ImGui::GetCursorPos();
  cursor.x = label_pos.x;
  cursor.y = label_pos.y + header_height;
  ImGui::SetCursorPos(cursor);

  const float separator_thickness = 2.0f;
  const float separator_padding = 0.0f;
  ImGui::Dummy(ImVec2(0.0f, separator_padding));
  ImVec2 separator_start = ImGui::GetCursorScreenPos();
  float separator_width = ImGui::GetContentRegionAvail().x;
  draw_solid_separator(separator_start, separator_width, separator_thickness,
    Theme::ToImU32(Theme::SEPARATOR_GRAY));
  ImGui::Dummy(ImVec2(0.0f, separator_thickness + separator_padding));

  if (open_assets_modal_from_header) {
    open_assets_directory_modal(ui_state);
  }

  // Create an inner scrolling region so the header above stays visible
  ScrollbarStyle scrollbar_style;
  scrollbar_style.pixel_scale = 2.0f;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BACKGROUND_LIGHT_BLUE_1);
  ScrollbarState scrollbar_state = begin_scrollbar_child(
    "AssetGridScroll",
    ImVec2(0, 0),
    scrollbar_style);

  const auto animation_now = std::chrono::steady_clock::now();

  struct ItemLayout {
    ImVec2 position;
    ImVec2 display_size;
    float row_height = 0.0f;
  };

  struct RowInfo {
    int start_index = 0;
    int end_index = 0; // exclusive
    float y = 0.0f;
    float height = 0.0f;
  };

  constexpr float GRID_RIGHT_MARGIN = 24.0f;  // Extra space so the last column stays clear of the scrollbar
  const float label_height = RESULTS_TEXT_HEIGHT;
  float available_width = panel_width - 20.0f - GRID_RIGHT_MARGIN; // Account for padding and scrollbar margin
  available_width = std::max(available_width, thumbnail_size);

  ImVec2 grid_start_pos = ImGui::GetCursorPos();
  ImVec2 grid_screen_start = ImGui::GetCursorScreenPos();
  ImDrawList* grid_draw_list = ImGui::GetWindowDrawList();
  grid_draw_list->ChannelsSplit(2);
  grid_draw_list->ChannelsSetCurrent(0);

  ImGuiIO& selection_io = ImGui::GetIO();
  bool selection_modifier_pressed = selection_io.KeySuper || selection_io.KeyCtrl;

  bool drag_preview_active = ui_state.drag_select_active;
  ImVec2 drag_preview_start = ui_state.drag_select_start;
  ImVec2 drag_preview_end = ui_state.drag_select_end;
  bool left_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  bool grid_window_hovered_now = ImGui::IsWindowHovered();

  if (drag_preview_active) {
    if (left_mouse_down && grid_window_hovered_now) {
      drag_preview_end = ImGui::GetMousePos();
    }
  } else if (ui_state.drag_select_started && left_mouse_down && grid_window_hovered_now) {
    ImVec2 current_pos = ImGui::GetMousePos();
    float drag_distance = std::abs(current_pos.x - drag_preview_start.x) +
      std::abs(current_pos.y - drag_preview_start.y);
    if (drag_distance > 5.0f) {
      drag_preview_active = true;
      drag_preview_end = current_pos;
    }
  }

  ImVec2 drag_preview_min(0.0f, 0.0f);
  ImVec2 drag_preview_max(0.0f, 0.0f);
  if (drag_preview_active) {
    drag_preview_min = ImVec2(
      std::min(drag_preview_start.x, drag_preview_end.x),
      std::min(drag_preview_start.y, drag_preview_end.y)
    );
    drag_preview_max = ImVec2(
      std::max(drag_preview_start.x, drag_preview_end.x),
      std::max(drag_preview_start.y, drag_preview_end.y)
    );
  }

  std::vector<ItemLayout> item_layouts;
  std::vector<RowInfo> row_infos;

  int loaded_count = std::min(ui_state.loaded_end_index, static_cast<int>(ui_state.results.size()));
  // Track which GIFs are rendered this frame so we can prune stale playback state later.
  std::unordered_set<std::string> active_gif_paths;

  if (loaded_count > 0) {
    item_layouts.resize(loaded_count);
    active_gif_paths.reserve(static_cast<size_t>(loaded_count));

    std::vector<ImVec2> base_sizes(loaded_count);
    for (int i = 0; i < loaded_count; ++i) {
      const Asset& asset = ui_state.results[i];
      const TextureCacheEntry& texture_entry = texture_manager.get_asset_texture(asset);

      ImVec2 size(thumbnail_size * Config::ICON_SCALE,
                  thumbnail_size * Config::ICON_SCALE);

      if (texture_entry.width > 0 && texture_entry.height > 0) {
        float width = static_cast<float>(texture_entry.width);
        float height = static_cast<float>(texture_entry.height);

        if (height > 0.0f) {
          float target_height = thumbnail_size;
          float scale = target_height / height;

          if (asset.type == AssetType::_3D && scale > 1.0f) {
            scale = 1.0f;
            target_height = height;
          } else {
            target_height = height * scale;
          }

          float target_width = width * scale;

          if (target_height > thumbnail_size) {
            float clamp_scale = thumbnail_size / target_height;
            target_height = thumbnail_size;
            target_width *= clamp_scale;
          }

          size = ImVec2(target_width, target_height);
        }
      }

      base_sizes[i] = size;

    }

    float y_cursor = grid_start_pos.y;
    int index = 0;
    while (index < loaded_count) {
      int row_start = index;
      float row_width = 0.0f;
      float row_height = 0.0f;
      int row_item_count = 0;

      while (index < loaded_count) {
        ImVec2 display = base_sizes[index];
        if (row_item_count == 0 && available_width > 0.0f && display.x > available_width) {
          float scale = available_width / display.x;
          display.x *= scale;
          display.y *= scale;
        }

        float spacing = (row_item_count == 0) ? 0.0f : RESULTS_GRID_SPACING;
        if (row_item_count > 0 && row_width + spacing + display.x > available_width) {
          break;
        }

        float x_pos = grid_start_pos.x + row_width + spacing;
        item_layouts[index].position = ImVec2(x_pos, y_cursor);
        item_layouts[index].display_size = display;
        row_width += spacing + display.x;
        row_height = std::max(row_height, display.y);
        row_item_count++;
        index++;
      }

      if (row_item_count == 0 && index < loaded_count) {
        ImVec2 display = base_sizes[index];
        if (available_width > 0.0f && display.x > available_width) {
          float scale = available_width / display.x;
          display.x *= scale;
          display.y *= scale;
        }
        item_layouts[index].position = ImVec2(grid_start_pos.x, y_cursor);
        item_layouts[index].display_size = display;
        row_width = display.x;
        row_height = std::max(row_height, display.y);
        row_item_count = 1;
        index++;
      }

      row_height = std::max(row_height, 1.0f);

      RowInfo row;
      row.start_index = row_start;
      row.end_index = index;
      row.y = y_cursor;
      row.height = row_height;
      row_infos.push_back(row);

      for (int j = row_start; j < index; ++j) {
        item_layouts[j].row_height = row_height;
      }

      y_cursor += row_height + RESULTS_GRID_SPACING;
    }
  }

  float total_content_height = 0.0f;
  if (!row_infos.empty()) {
    const RowInfo& last_row = row_infos.back();
    total_content_height = (last_row.y + last_row.height) - grid_start_pos.y;
  }

  ImGui::Dummy(ImVec2(0, total_content_height));

  float current_scroll_y = ImGui::GetScrollY();
  float viewport_height = ImGui::GetWindowHeight();
  float view_bottom = current_scroll_y + viewport_height;

  int first_visible_row = 0;
  int last_visible_row = static_cast<int>(row_infos.size());

  if (!row_infos.empty()) {
    while (first_visible_row < static_cast<int>(row_infos.size()) &&
           row_infos[first_visible_row].y + row_infos[first_visible_row].height < current_scroll_y) {
      ++first_visible_row;
    }

    last_visible_row = first_visible_row;
    while (last_visible_row < static_cast<int>(row_infos.size()) &&
           row_infos[last_visible_row].y <= view_bottom) {
      ++last_visible_row;
    }

    first_visible_row = std::max(0, first_visible_row - 1);
    last_visible_row = std::min(static_cast<int>(row_infos.size()), last_visible_row + 1);

    if (last_visible_row > 0) {
      int last_visible_item = row_infos[last_visible_row - 1].end_index;
      int load_threshold = std::max(0, ui_state.loaded_end_index - UIState::LOAD_BATCH_SIZE / 2);
      if (last_visible_item >= load_threshold &&
          ui_state.loaded_end_index < static_cast<int>(ui_state.results.size())) {
        ui_state.loaded_end_index = std::min(
          ui_state.loaded_end_index + UIState::LOAD_BATCH_SIZE,
          static_cast<int>(ui_state.results.size())
        );
      }
    }
  }

  for (int row_index = first_visible_row; row_index < last_visible_row; ++row_index) {
    const RowInfo& row = row_infos[row_index];
    for (int i = row.start_index; i < row.end_index && i < loaded_count; ++i) {
      const ItemLayout& layout = item_layouts[i];

      ImGui::SetCursorPos(layout.position);

      ImGui::BeginGroup();

      const Asset& asset = ui_state.results[i];
      const TextureCacheEntry& texture_entry = texture_manager.get_asset_texture(asset);
      bool is_currently_selected = ui_state.selected_asset_ids.count(asset.id) > 0;

      float container_height = layout.row_height;
      ImVec2 container_size(layout.display_size.x, container_height);
      ImVec2 container_pos = ImGui::GetCursorScreenPos();
      ImVec2 container_max(container_pos.x + container_size.x, container_pos.y + container_height);

      bool is_drag_preview_target = false;
      if (drag_preview_active) {
        is_drag_preview_target = !(
          container_max.x < drag_preview_min.x ||
          container_pos.x > drag_preview_max.x ||
          container_max.y < drag_preview_min.y ||
          container_pos.y > drag_preview_max.y
        );
      }

      bool show_selected = is_currently_selected;
      if (drag_preview_active) {
        show_selected = selection_modifier_pressed
          ? (show_selected || is_drag_preview_target)
          : is_drag_preview_target;
      }

      if (show_selected) {
        ImU32 container_bg_color = Theme::ToImU32(Theme::ACCENT_BLUE_1_ALPHA_35);
        grid_draw_list->AddRectFilled(
          container_pos,
          container_max,
          container_bg_color,
          RESULTS_THUMBNAIL_CORNER_RADIUS);
      }

      if (show_selected) {
        grid_draw_list->AddRect(
          container_pos,
          container_max,
          Theme::ToImU32(Theme::ACCENT_BLUE_1),
          RESULTS_THUMBNAIL_CORNER_RADIUS,
          0,
          2.0f);
      }

      float image_x_offset = std::max(0.0f, (container_size.x - layout.display_size.x) * 0.5f);
      float image_y_offset = 0.0f;
      if (container_height > 0.0f) {
        image_y_offset = std::max(0.0f, (container_height - layout.display_size.y) * 0.5f);
      }
      ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);

      ImGui::SetCursorScreenPos(image_pos);
      unsigned int display_texture_id = texture_entry.get_texture_id();

      if (asset.extension == ".gif") {
        auto animation = texture_manager.get_or_load_animated_gif(asset.path);
        if (animation && !animation->empty()) {
          active_gif_paths.insert(asset.path);
          auto& playback = ui_state.grid_animation_states[asset.path];
          playback.set_animation(animation, animation_now);
          unsigned int animated_texture = playback.current_texture(animation_now);
          if (animated_texture != 0) {
            display_texture_id = animated_texture;
          } else if (!animation->frame_textures.empty()) {
            display_texture_id = animation->frame_textures.front();
          }
        } else {
          ui_state.grid_animation_states.erase(asset.path);
        }
      }

      std::string thumbnail_id = "Thumbnail##" + std::to_string(i);
      if (ImGui::InvisibleButton(thumbnail_id.c_str(), layout.display_size)) {

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

      bool is_container_hovered = ImGui::IsMouseHoveringRect(container_pos, container_max);

      // Draw the actual texture with rounded corners
      if (display_texture_id != 0) {
        ImVec2 image_max(image_pos.x + layout.display_size.x, image_pos.y + layout.display_size.y);
        grid_draw_list->AddImageRounded(
          (ImTextureID) (intptr_t) display_texture_id,
          image_pos,
          image_max,
          ImVec2(0.0f, 0.0f),
          ImVec2(1.0f, 1.0f),
          Theme::COLOR_WHITE_U32,
          RESULTS_THUMBNAIL_CORNER_RADIUS);

        if (is_container_hovered && !ImGui::IsItemActive()) {
          grid_draw_list->AddRectFilled(
            image_pos,
            image_max,
            Theme::ToImU32(Theme::IMAGE_HOVER_OVERLAY),
            RESULTS_THUMBNAIL_CORNER_RADIUS);
        }
      }

      // Handle drag-and-drop to external applications (Finder, Explorer, etc.)
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f)) {
        if (!ui_state.drag_initiated) {
          ImVec2 mouse_pos = ImGui::GetMousePos();

          std::vector<std::string> files_to_drag;

          if (ui_state.selected_asset_ids.size() > 1) {
            for (const auto& result : ui_state.results) {
              if (ui_state.selected_asset_ids.count(result.id)) {
                std::vector<std::string> related = find_related_files(result);
                files_to_drag.insert(files_to_drag.end(), related.begin(), related.end());
              }
            }
            LOG_DEBUG("Started drag for {} selected assets (with {} total file(s))",
                      ui_state.selected_asset_ids.size(), files_to_drag.size());
          } else {
            files_to_drag = find_related_files(ui_state.results[i]);
            LOG_DEBUG("Started drag for: {} (with {} related file(s))",
                      ui_state.results[i].name, files_to_drag.size());
          }

          if (Services::drag_drop_manager().is_supported()) {
            if (Services::drag_drop_manager().begin_file_drag(files_to_drag, mouse_pos)) {
              ui_state.drag_initiated = true;
            }
          }
        }
      }

      // Handle right-click context menu (no selection change)
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup(("AssetContextMenu##" + std::to_string(i)).c_str());
      }

      render_asset_context_menu(ui_state.results[i], "AssetContextMenu##" + std::to_string(i));

      int current_zoom_level = zoom_level_index(ui_state.grid_zoom_level);
      bool show_label_always = (asset.type != AssetType::_2D && asset.type != AssetType::_3D) &&
                               current_zoom_level >= zoom_level_index(ZoomLevel::Level2);

      bool can_show_label = show_selected ||
        (is_container_hovered && !ui_state.assets_directory_modal_open) ||
        show_label_always;

      if (can_show_label) {
        const std::string& full_name = ui_state.results[i].name;
        ImVec2 text_size = ImGui::CalcTextSize(full_name.c_str());
        grid_draw_list->ChannelsSetCurrent(1);

        float label_top = std::max(container_pos.y, container_pos.y + container_height - label_height);
        float label_bottom = container_pos.y + container_height;

        float available_label_height = std::max(0.0f, label_bottom - label_top);
        float text_y = label_top + std::max(0.0f, (available_label_height - text_size.y) * 0.5f);

        float text_width = text_size.x;
        float background_width = std::max(container_size.x, text_width);
        float background_x = container_pos.x + (container_size.x - background_width) * 0.5f;
        ImVec2 text_bg_min(background_x, label_top);
        ImVec2 text_bg_max(background_x + background_width, label_bottom);

        float text_x = background_x + (background_width - text_size.x) * 0.5f;
        ImVec2 text_pos(text_x, text_y);

        ImU32 background_color = Theme::ToImU32(
          show_selected ? Theme::ACCENT_BLUE_1_ALPHA_95 : Theme::FRAME_LIGHT_BLUE_5
        );
        ImU32 border_color = Theme::ToImU32(
          show_selected ? Theme::ACCENT_BLUE_1 : Theme::BORDER_LIGHT_BLUE_1
        );
        ImU32 text_color = show_selected ? Theme::COLOR_WHITE_U32 : Theme::ToImU32(Theme::TEXT_DARK);

        grid_draw_list->AddRectFilled(text_bg_min, text_bg_max, background_color, 3.0f);
        grid_draw_list->AddRect(text_bg_min, text_bg_max, border_color, 3.0f, 0, 1.0f);
        grid_draw_list->AddText(text_pos, text_color, full_name.c_str());
        grid_draw_list->ChannelsSetCurrent(0);
      }

      ImGui::EndGroup();
    }
  }

  if (!ui_state.grid_animation_states.empty()) {
    for (auto it = ui_state.grid_animation_states.begin(); it != ui_state.grid_animation_states.end();) {
      if (active_gif_paths.find(it->first) == active_gif_paths.end()) {
        it = ui_state.grid_animation_states.erase(it);
      } else {
        ++it;
      }
    }
  }
  
  // Handle area selection (rubber band selection)
  bool is_window_hovered = ImGui::IsWindowHovered();
  bool is_item_hovered = ImGui::IsAnyItemHovered();

  // Start drag selection when clicking on background
  if (is_window_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !is_item_hovered) {
    ui_state.drag_select_started = true;
    ui_state.drag_select_start = ImGui::GetMousePos();
    ui_state.drag_select_end = ui_state.drag_select_start;
    ui_state.drag_select_active = false; // Will activate after minimum drag distance
  }

  // Update drag selection while dragging
  if (ui_state.drag_select_started && ImGui::IsMouseDown(ImGuiMouseButton_Left) && is_window_hovered) {
    ImVec2 current_pos = ImGui::GetMousePos();
    float drag_distance = std::abs(current_pos.x - ui_state.drag_select_start.x) +
      std::abs(current_pos.y - ui_state.drag_select_start.y);

    // Activate drag selection after minimum distance (5 pixels) to show rectangle
    if (drag_distance > 5.0f && !ui_state.drag_select_active) {
      ui_state.drag_select_active = true;
    }
    if (ui_state.drag_select_active) {
      ui_state.drag_select_end = current_pos;
    }
  }

  // Draw selection rectangle
  if (ui_state.drag_select_active) {
    ImVec2 rect_min(
      std::min(ui_state.drag_select_start.x, ui_state.drag_select_end.x),
      std::min(ui_state.drag_select_start.y, ui_state.drag_select_end.y)
    );
    ImVec2 rect_max(
      std::max(ui_state.drag_select_start.x, ui_state.drag_select_end.x),
      std::max(ui_state.drag_select_start.y, ui_state.drag_select_end.y)
    );

    // Draw filled rectangle with transparency
    grid_draw_list->ChannelsSetCurrent(1);
    ImU32 selection_fill = Theme::ToImU32(Theme::ACCENT_BLUE_1_ALPHA_35);
    ImU32 selection_border = Theme::ToImU32(Theme::ACCENT_BLUE_1);
    grid_draw_list->AddRectFilled(rect_min, rect_max, selection_fill, 3.0f);

    // Draw border
    grid_draw_list->AddRect(rect_min, rect_max, selection_border, 3.0f, 0, 2.0f);
    grid_draw_list->ChannelsSetCurrent(0);
  }

  // Complete selection on mouse release (handles both clicks and drags)
  if (ui_state.drag_select_started && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    ImVec2 rect_min(
      std::min(ui_state.drag_select_start.x, ui_state.drag_select_end.x),
      std::min(ui_state.drag_select_start.y, ui_state.drag_select_end.y)
    );
    ImVec2 rect_max(
      std::max(ui_state.drag_select_start.x, ui_state.drag_select_end.x),
      std::max(ui_state.drag_select_start.y, ui_state.drag_select_end.y)
    );

    // Clear selection if not holding modifier
    if (!selection_modifier_pressed) {
      ui_state.selected_asset_ids.clear();
    }

    for (int i = 0; i < loaded_count; ++i) {
      const ItemLayout& layout = item_layouts[i];

      ImVec2 offset(layout.position.x - grid_start_pos.x, layout.position.y - grid_start_pos.y);
      ImVec2 item_min(grid_screen_start.x + offset.x, grid_screen_start.y + offset.y);
      ImVec2 item_max(item_min.x + layout.display_size.x, item_min.y + layout.row_height);

      bool intersects = !(item_max.x < rect_min.x || item_min.x > rect_max.x ||
                          item_max.y < rect_min.y || item_min.y > rect_max.y);
      if (!intersects) {
        continue;
      }

      if (i < static_cast<int>(ui_state.results.size())) {
          ui_state.selected_asset_ids.insert(ui_state.results[i].id);
        ui_state.selected_asset_index = i;
        ui_state.selected_asset = ui_state.results[i];
      }
    }

    // Reset flags
    ui_state.drag_select_started = false;
    ui_state.drag_select_active = false;
  }

  // End inner scrolling region (grid)
  grid_draw_list->ChannelsMerge();
  end_scrollbar_child(scrollbar_state);
  ImGui::PopStyleColor();

  // Overlay custom vertical scrollbar art while keeping ImGui hit-testing intact
  SpriteAtlas scrollbar_atlas = texture_manager.get_ui_elements_atlas();
  if (scrollbar_atlas.texture_id != 0) {
    SlicedSprite track_def = make_scrollbar_track_definition(0, scrollbar_style.pixel_scale);
    SlicedSprite thumb_def = make_scrollbar_thumb_definition(scrollbar_style.pixel_scale);
    draw_scrollbar_overlay(scrollbar_state, scrollbar_atlas, track_def, thumb_def);
  }

  // Reset drag state when mouse button is released
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    ui_state.drag_initiated = false;
  }

  // End outer container
  ImGui::EndChild();
}
