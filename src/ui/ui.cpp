#include "ui/ui.h"
#include "theme.h"
#include "utils.h"
#include "config.h"
#include "imgui.h"
#include "texture_manager.h"
#include "ui/components.h"
#include "event_processor.h"
#include "audio_manager.h"
#include "asset.h"
#include "3d.h"
#include "search.h"
#include "logger.h"
#include "services.h"
#include "drag_drop.h"
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <shellscalingapi.h>
extern "C" HRESULT WINAPI SetProcessDpiAwareness(PROCESS_DPI_AWARENESS value);
#endif
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <limits>

namespace {
  bool render_assets_directory_modal(UIState& ui_state) {
    float ui_scale = ui_state.ui_scale;
    bool directory_changed = false;

    bool popup_style_pushed = false;
    bool popup_open = ImGui::IsPopupOpen("Select Assets Directory");
    if (popup_open) {
      ui_state.assets_directory_modal_open = true;
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

    bool popup_background_pushed = false;
    if (popup_open) {
      ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::COLOR_TRANSPARENT);
      ImGui::PushStyleColor(ImGuiCol_Border, Theme::COLOR_TRANSPARENT);
      popup_background_pushed = true;
    }

    constexpr ImGuiWindowFlags ASSETS_MODAL_FLAGS =
      ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoTitleBar;

    if (ImGui::BeginPopupModal("Select Assets Directory", nullptr, ASSETS_MODAL_FLAGS)) {
      ui_state.assets_directory_modal_open = true;
      if (ui_state.close_assets_directory_modal_requested) {
        ImGui::CloseCurrentPopup();
        ui_state.close_assets_directory_modal_requested = false;
      }

      SpriteAtlas modal_atlas = Services::texture_manager().get_ui_elements_atlas();
      ImDrawList* modal_draw_list = ImGui::GetWindowDrawList();
      if (modal_atlas.is_valid() && modal_draw_list != nullptr) {
        static const SlicedSprite modal_frame = make_modal_combined_frame(2.0f);
        ImVec2 window_pos = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();

        draw_nine_slice_image(modal_atlas, modal_frame, window_pos, window_size, ui_scale);

        float header_height = modal_frame.border.z * modal_frame.pixel_scale * ui_scale;
        const char* header_text = "Select Assets Directory";
        ImFont* header_font = Theme::get_primary_font_large();
        ImFont* draw_font = header_font ? header_font : ImGui::GetFont();
        float title_font_size = ImGui::GetFontSize() + 2.0f;
        ImVec2 text_size = draw_font->CalcTextSizeA(title_font_size, FLT_MAX, 0.0f, header_text);
        ImVec2 text_pos(
          window_pos.x + (window_size.x - text_size.x) * 0.5f,
          window_pos.y + (header_height - text_size.y) * 0.5f - 5.0f);
        modal_draw_list->AddText(draw_font, title_font_size, text_pos,
          Theme::ToImU32(Theme::TEXT_LABEL), header_text);

        float content_offset = header_height - ImGui::GetStyle().WindowPadding.y;
        if (content_offset > 0.0f) {
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + content_offset);
        }
      }

      namespace fs = std::filesystem;
      if (ui_state.assets_path_selected.empty()) {
        ui_state.assets_path_selected = get_home_directory();
      }

      fs::path current_path(ui_state.assets_path_selected);
      std::error_code fs_error;
      std::string selected_path = !ui_state.assets_path_selected.empty()
        ? ui_state.assets_path_selected
        : get_home_directory();
      ImVec2 action_button_size(200.0f, 40.0f);
      const float BUTTON_BOTTOM_MARGIN = 12.0f;

      ui_state.formatted_assets_path = add_spaces_around_path_separators(selected_path);
      ImGui::Dummy(ImVec2(0.0f, 12.0f));
      ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
      ImGui::TextColored(Theme::TEXT_LIGHTER, "%s", ui_state.formatted_assets_path.c_str());
      ImGui::PopTextWrapPos();
      ImGui::Dummy(ImVec2(0.0f, 12.0f));

      if (!fs::exists(current_path, fs_error) || !fs::is_directory(current_path, fs_error)) {
        ImGui::TextColored(Theme::TEXT_WARNING, "Directory unavailable");
      }
      else {
        float list_height = ImGui::GetContentRegionAvail().y - action_button_size.y - BUTTON_BOTTOM_MARGIN;
        list_height = std::max(list_height, 180.0f);
        const float LIST_PADDING = 14.0f;
        SpriteAtlas list_atlas = modal_atlas;
        ImVec2 list_pos = ImGui::GetCursorScreenPos();
        ImVec2 list_size(ImGui::GetContentRegionAvail().x, list_height);
        static const SlicedSprite list_frame = make_8px_frame(1, 2, 2.0f);
        if (list_atlas.is_valid() && list_size.x > 0.0f && list_size.y > 0.0f) {
          draw_nine_slice_image(list_atlas, list_frame, list_pos, list_size, ui_scale);
        }
        ImVec2 child_pos(
          list_pos.x + LIST_PADDING,
          list_pos.y + LIST_PADDING);
        ImVec2 child_size(
          std::max(0.0f, list_size.x - LIST_PADDING * 2.0f),
          std::max(0.0f, list_size.y - LIST_PADDING * 2.0f));
        ImGui::SetCursorScreenPos(child_pos);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::COLOR_TRANSPARENT);
        ScrollbarStyle list_scroll_style;
        list_scroll_style.pixel_scale = 2.0f;
        ScrollbarState list_scroll = begin_scrollbar_child(
          "AssetsDirectoryList",
          child_size,
          list_scroll_style,
          ImGuiWindowFlags_AlwaysVerticalScrollbar);

        // Navigate to parent directory
        fs::path parent_path = current_path.parent_path();
#ifdef _WIN32
        bool at_volume_root = current_path.has_root_path() && current_path == current_path.root_path();
#endif

        if (!parent_path.empty()) {
          if (ImGui::Selectable("..", false)) {
#ifdef _WIN32
            if (at_volume_root && !ui_state.show_drive_roots) {
              ui_state.show_drive_roots = true;
            }
            else {
              ui_state.show_drive_roots = false;
              ui_state.assets_path_selected = parent_path.generic_u8string();
            }
#else
            ui_state.assets_path_selected = parent_path.generic_u8string();
#endif
          }
        }

#ifdef _WIN32
        if (ui_state.show_drive_roots && at_volume_root) {
          std::vector<fs::path> drives = list_root_directories();
          if (!drives.empty()) {
            std::sort(drives.begin(), drives.end(), [](const fs::path& a, const fs::path& b) {
              return a.u8string() < b.u8string();
            });

            const fs::path current_root = current_path.root_path();
            for (const auto& drive : drives) {
              std::string label = "[Drive] " + drive.u8string();
              bool is_current_drive = drive == current_root;
              if (ImGui::Selectable(label.c_str(), is_current_drive)) {
                ui_state.assets_path_selected = drive.generic_u8string();
                ui_state.show_drive_roots = false;
              }
            }

            ImGui::Separator();
          }
          else {
            ImGui::TextColored(Theme::TEXT_WARNING, "No drives detected.");
          }
        }
#endif

        if (!ui_state.show_drive_roots) {
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
                ui_state.show_drive_roots = false;
              }

              if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                ui_state.assets_path_selected = entry.path().generic_u8string();
                ui_state.show_drive_roots = false;
                LOG_INFO("Assets directory selected: {}", ui_state.assets_path_selected);
                directory_changed = true;
                ImGui::CloseCurrentPopup();
              }
            }
          }
        }

        end_scrollbar_child(list_scroll);
        ImGui::PopStyleColor(4);

        SpriteAtlas scrollbar_atlas = Services::texture_manager().get_ui_elements_atlas();
        if (scrollbar_atlas.is_valid()) {
          SlicedSprite track_def = make_scrollbar_track_definition(0, list_scroll_style.pixel_scale);
          SlicedSprite thumb_def = make_scrollbar_thumb_definition(list_scroll_style.pixel_scale);
          draw_scrollbar_overlay(list_scroll, scrollbar_atlas, track_def, thumb_def, ui_scale);
        }

        ImGui::SetCursorScreenPos(ImVec2(list_pos.x, list_pos.y + list_size.y));
        ImGui::Dummy(ImVec2(0.0f, BUTTON_BOTTOM_MARGIN));
      }

      const float BUTTON_SPACING = 16.0f;
      float available_width = ImGui::GetContentRegionAvail().x;
      float total_width = action_button_size.x * 2.0f + BUTTON_SPACING;
      float center_offset = std::max(0.0f, (available_width - total_width) * 0.5f);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_offset);
      if (draw_small_frame_button("AssetsSelectButton", "Select", modal_atlas, action_button_size, ui_scale, 3.0f)) {
        if (!ui_state.assets_path_selected.empty() && !ui_state.show_drive_roots) {
          LOG_INFO("Assets directory selected: {}", ui_state.assets_path_selected);
          directory_changed = true;
        }
        ui_state.show_drive_roots = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine(0.0f, BUTTON_SPACING);
      if (draw_small_frame_button("AssetsCancelButton", "Cancel", modal_atlas, action_button_size, ui_scale, 3.0f)) {
        ui_state.show_drive_roots = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    } else {
      ui_state.assets_directory_modal_open = false;
    }
    if (!popup_open) {
      ui_state.assets_directory_modal_open = false;
    } else {
      ui_state.assets_directory_modal_open = ImGui::IsPopupOpen("Select Assets Directory");
    }
    if (popup_style_pushed) {
      ImGui::PopStyleColor();
    }
    if (popup_background_pushed) {
      ImGui::PopStyleColor(2);
    }

    return directory_changed;
  }
}

void open_assets_directory_modal(UIState& ui_state) {
  ui_state.assets_directory_modal_open = true;
  ui_state.assets_directory_modal_requested = true;
  ui_state.show_drive_roots = false;
  if (!ui_state.assets_directory.empty()) {
    ui_state.assets_path_selected = ui_state.assets_directory;
  }
  else {
    ui_state.assets_path_selected = get_home_directory();
  }
}

// tree panel helpers moved to src/ui/tree_panel.cpp

void reset_folder_tree_state(UIState& ui_state) {
  ui_state.pending_tree_selection.reset();
  ui_state.tree_nodes_to_open.clear();
  ui_state.collapse_tree_requested = false;
  ui_state.folder_checkbox_states.clear();
  ui_state.folder_children_cache.clear();
  ui_state.folder_filters_dirty = true;
  ui_state.path_filters.clear();
  ui_state.path_filter_active = false;
  ui_state.folder_selection_covers_all = true;
  ui_state.folder_selection_empty = false;
}

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
  ui_state.filters_changed = true;
  ui_state.event_batch_finished = false;
  ui_state.assets_directory_modal_open = false;
  ui_state.assets_directory_modal_requested = false;
  ui_state.current_animation.reset();
  ui_state.current_animation_path.clear();
  ui_state.preview_animation_state.reset();
  ui_state.grid_animation_states.clear();
  reset_folder_tree_state(ui_state);
}

void render_progress_panel(UIState& ui_state, SafeAssets& safe_assets,
  TextureManager& texture_manager, float panel_width, float panel_height) {
  float ui_scale = ui_state.ui_scale;
  SpriteAtlas progress_frame_atlas = texture_manager.get_ui_elements_atlas();
  const SlicedSprite progress_frame_definition = make_16px_frame(1, 3.0f);
  ImVec2 panel_pos = ImGui::GetCursorScreenPos();
  ImVec2 panel_size(panel_width, panel_height);

  // Unified progress bar for all asset processing
  bool show_progress = Services::event_processor().has_pending_work();

  if (show_progress) {
    float progress = Services::event_processor().get_progress();
    size_t processed = Services::event_processor().get_total_processed();
    size_t total = Services::event_processor().get_total_queued();

    float bar_height = std::max(20.0f, panel_height - 8.0f);
    bar_height = std::min(bar_height, panel_height);
    float bar_y = panel_pos.y + std::max(0.0f, (panel_height - bar_height) * 0.5f);
    float clamped_progress = std::clamp(progress, 0.0f, 1.0f);
    ImVec2 bar_pos(panel_pos.x, bar_y);
    ImVec2 bar_size(panel_width, bar_height);
    ImVec2 bar_end(bar_pos.x, bar_pos.y + bar_height);

    if (progress_frame_atlas.is_valid()) {
      draw_nine_slice_image(progress_frame_atlas, progress_frame_definition,
        bar_pos, bar_size, ui_scale, Theme::COLOR_WHITE_U32);

      float frame_border = progress_frame_definition.border.x * progress_frame_definition.pixel_scale * ui_scale;
      float fill_inset = std::max(2.0f, frame_border * 0.5f);
      ImVec2 fill_pos(
        bar_pos.x + fill_inset,
        bar_pos.y + fill_inset);
      ImVec2 fill_size(
        std::max(0.0f, bar_size.x - fill_inset * 2.0f),
        std::max(0.0f, bar_size.y - fill_inset * 2.0f));

      if (clamped_progress > 0.0f && fill_size.x > 0.0f && fill_size.y > 0.0f) {
        ImVec2 fill_end(
          fill_pos.x + fill_size.x * clamped_progress,
          fill_pos.y + fill_size.y);
        ImGui::GetWindowDrawList()->AddRectFilled(fill_pos, fill_end,
          Theme::ToImU32(Theme::TAG_TYPE_AUDIO));
      }
    }

    char progress_text[64];
    snprintf(progress_text, sizeof(progress_text), "Processing %zu out of %zu", processed, total);
    ImVec2 text_size = ImGui::CalcTextSize(progress_text);
    float frame_border = progress_frame_definition.border.x * progress_frame_definition.pixel_scale * ui_scale;
    float fill_inset = std::max(2.0f, frame_border * 0.5f);
    ImVec2 inner_pos(
      bar_pos.x + fill_inset,
      bar_pos.y + fill_inset);
    ImVec2 inner_size(
      std::max(0.0f, bar_size.x - fill_inset * 2.0f),
      std::max(0.0f, bar_size.y - fill_inset * 2.0f));
    ImVec2 text_pos = ImVec2(
      inner_pos.x + (inner_size.x - text_size.x) * 0.5f,
      bar_pos.y + (bar_size.y - text_size.y) * 0.5f);
    ImGui::GetWindowDrawList()->AddText(text_pos, Theme::ToImU32(Theme::TEXT_DARK), progress_text);

    ImGui::SetCursorScreenPos(bar_end);
  }

  ImGui::SetCursorScreenPos(ImVec2(panel_pos.x, panel_pos.y + panel_height));

  if (ui_state.assets_directory_modal_requested) {
    ImGui::OpenPopup("Select Assets Directory");
    ui_state.assets_directory_modal_requested = false;
  }

  // Handle assets directory change
  if (render_assets_directory_modal(ui_state)) {
    const std::string new_path = ui_state.assets_path_selected;
    ui_state.assets_directory = new_path;

    // Stop all services and clear all data
    Services::stop(&safe_assets);

    clear_ui_state(ui_state);

    if (!Config::set_assets_directory(new_path)) {
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
