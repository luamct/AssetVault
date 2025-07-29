#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include "asset.h"
#include "3d.h"

// Search state structure
struct SearchState {
  std::atomic<bool> update_needed{ true };

  char buffer[256] = "";
  std::string last_buffer = "";
  std::string input_tracking = ""; // Track input to detect real changes

  // Debouncing state
  std::chrono::steady_clock::time_point last_keypress_time;
  bool pending_search = false;

  // UI state
  std::vector<Asset> filtered_assets;
  int selected_asset_index = -1; // -1 means no selection

  // Infinite scroll state
  static constexpr int LOAD_BATCH_SIZE = 50;
  int loaded_start_index = 0;    // Always 0, never changes
  int loaded_end_index = 0;      // Grows as user scrolls down

  // Model preview state
  int model_preview_row = -1;    // Which row has the expanded preview
  Model current_model;

  // Audio playback settings
  bool auto_play_audio = true;
};

// Function to check if an asset matches the search query
bool asset_matches_search(const Asset& asset, const std::string& search_query);

// Function to filter assets based on search query
void filter_assets(SearchState& search_state, const std::vector<Asset>& assets);
