#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <optional>
#include <unordered_set>

// Forward declarations
struct Asset;

// UI state structure
struct UIState {
  std::atomic<bool> update_needed{ true };

  char buffer[256] = "";
  std::string last_buffer = "";
  std::string input_tracking = ""; // Track input to detect real changes

  // Debouncing state
  std::chrono::steady_clock::time_point last_keypress_time;
  bool pending_search = false;

  // UI state
  std::vector<Asset> results;
  int selected_asset_index = -1; // -1 means no selection
  std::optional<Asset> selected_asset; // Copy used for stable preview/audio

  // Asset path state
  std::string assets_path_selected;
  bool assets_directory_changed = false;
  std::string assets_root_directory;

  // Fast membership check for current results (IDs only)
  std::unordered_set<uint32_t> results_ids;

  // Infinite scroll state
  static constexpr int LOAD_BATCH_SIZE = 50;
  int loaded_start_index = 0;    // Always 0, never changes
  int loaded_end_index = 0;      // Grows as user scrolls down

  // Model preview state
  int model_preview_row = -1;    // Which row has the expanded preview

  // Audio playback settings
  bool auto_play_audio = true;

  // Type filter toggle states
  bool type_filter_2d = false;
  bool type_filter_3d = false;
  bool type_filter_audio = false;
  bool type_filter_shader = false;
  bool type_filter_font = false;

  // Path filter toggle state
  bool path_filter_active = false;

  // Path filters (set by clicking on path segments)
  std::vector<std::string> path_filters;
};