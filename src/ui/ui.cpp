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

    if (ImGui::BeginPopupModal("Select Assets Directory", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
      ui_state.assets_directory_modal_open = true;
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

        ImGui::EndChild();

        ImGui::Spacing();
      }

      if (ImGui::Button("Select", ImVec2(160.0f, 0.0f))) {
        if (!ui_state.assets_path_selected.empty() && !ui_state.show_drive_roots) {
          LOG_INFO("Assets directory selected: {}", ui_state.assets_path_selected);
          directory_changed = true;
        }
        ui_state.show_drive_roots = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
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

    if (progress_frame_atlas.texture_id != 0) {
      draw_nine_slice_image(progress_frame_atlas, progress_frame_definition,
        bar_pos, bar_size, Theme::COLOR_WHITE_U32);

      float frame_border = progress_frame_definition.border.x * progress_frame_definition.pixel_scale;
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
    float frame_border = progress_frame_definition.border.x * progress_frame_definition.pixel_scale;
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
