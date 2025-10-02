#pragma once

#include "config.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <optional>
#include <unordered_set>

// Forward declarations
class TextureManager;
class EventProcessor;
class AudioManager;
class SearchIndex;
struct Asset;
struct Model;
struct Camera3D;

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
  std::string assets_directory;

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

// UI helper functions

// Clear all search and UI state when changing directories
void clear_ui_state(UIState& ui_state);

// Renders a clickable path that allows filtering by directory
void render_clickable_path(const Asset& asset, UIState& ui_state);

// Renders common asset information in standard order: Path, Extension, Type, Size, Modified
void render_common_asset_info(const Asset& asset, UIState& ui_state);

// Custom audio seek bar widget
bool audio_seek_bar(const char* id, float* value, float min_value, float max_value, float width, float height = 4.0f);


// Fancy styled text input with shadow effect
bool fancy_text_input(const char* label, char* buffer, size_t buffer_size, float width,
    float padding_x = 20.0f, float padding_y = 16.0f, float corner_radius = 25.0f);

// Draw type filter toggle button
bool draw_type_toggle_button(const char* label, bool& toggle_state, float x_pos, float y_pos,
    float button_width, float button_height);

// Main UI panel rendering functions
void render_search_panel(UIState& ui_state,
    const SafeAssets& safe_assets,
    float panel_width, float panel_height);

void render_progress_panel(UIState& ui_state,
    float panel_width, float panel_height);

void render_asset_grid(UIState& ui_state, TextureManager& texture_manager,
    SafeAssets& safe_assets, float panel_width, float panel_height);

void render_preview_panel(UIState& ui_state, TextureManager& texture_manager,
    AudioManager& audio_manager, Model& current_model,
    Camera3D& camera, float panel_width, float panel_height);
