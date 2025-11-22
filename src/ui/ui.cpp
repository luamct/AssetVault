#include "ui/ui.h"
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
#include <cmath>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <limits>


namespace {
  bool g_request_assets_path_popup = false;

  bool render_assets_directory_modal(UIState& ui_state) {
    bool directory_changed = false;
    if (g_request_assets_path_popup) {
      ImGui::OpenPopup("Select Assets Directory");
      g_request_assets_path_popup = false;
      ui_state.assets_directory_modal_open = true;

      // Initialize to current assets directory if set, otherwise home directory
      ui_state.show_drive_roots = false;
      if (!ui_state.assets_directory.empty()) {
        ui_state.assets_path_selected = ui_state.assets_directory;
      }
      else {
        ui_state.assets_path_selected = get_home_directory();
      }
    }

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

// tree panel helpers moved to src/ui/tree_panel.cpp

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
  ui_state.assets_directory_modal_open = false;
  ui_state.current_animation.reset();
  ui_state.current_animation_path.clear();
  ui_state.preview_animation_state.reset();
  ui_state.grid_animation_states.clear();
  ui_state.folder_checkbox_states.clear();
  ui_state.folder_children_cache.clear();
}

void render_progress_panel(UIState& ui_state, SafeAssets& safe_assets,
  float panel_width, float panel_height) {
  ImGui::BeginChild("ProgressRegion", ImVec2(panel_width, panel_height), false);

  // Unified progress bar for all asset processing
  bool show_progress = Services::event_processor().has_pending_work();

  if (show_progress) {
    // Progress bar data from event processor
    float progress = Services::event_processor().get_progress();
    size_t processed = Services::event_processor().get_total_processed();
    size_t total = Services::event_processor().get_total_queued();

    // Vertically center the progress bar within the panel child
    float bar_height = 35.0f;
    float target_y = (panel_height - bar_height) * 0.5f;
    if (target_y > ImGui::GetCursorPosY()) {
      ImGui::SetCursorPosY(target_y);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 18.0f);
    // Draw progress bar without text overlay
    ImGui::ProgressBar(progress, ImVec2(-1.0f, bar_height), "");
    ImGui::PopStyleVar();

    // Overlay centered text on the progress bar
    char progress_text[64];
    snprintf(progress_text, sizeof(progress_text), "Processing %zu out of %zu", processed, total);

    ImVec2 text_size = ImGui::CalcTextSize(progress_text);
    ImVec2 progress_bar_screen_pos = ImGui::GetItemRectMin();
    ImVec2 progress_bar_screen_size = ImGui::GetItemRectSize();

    // Center text on progress bar
    ImVec2 text_pos = ImVec2(
      progress_bar_screen_pos.x + (progress_bar_screen_size.x - text_size.x) * 0.5f,
      progress_bar_screen_pos.y + (progress_bar_screen_size.y - text_size.y) * 0.5f);

    ImGui::GetWindowDrawList()->AddText(text_pos, Theme::ToImU32(Theme::TEXT_DARK), progress_text);
  }

  const bool SHOW_ASSETS_PATH_BUTTON = false;
  if (SHOW_ASSETS_PATH_BUTTON) {
    float button_height = ImGui::GetFrameHeight();
    float bottom_margin = 12.0f;
    float left_margin = 12.0f;
    ImVec2 button_pos(left_margin, panel_height - button_height - bottom_margin);
    button_pos.y = std::max(button_pos.y, ImGui::GetCursorPosY());
    ImGui::SetCursorPos(button_pos);
    if (ImGui::Button("Assets Path", ImVec2(150.0f, 0.0f))) {
      g_request_assets_path_popup = true;
    }
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

