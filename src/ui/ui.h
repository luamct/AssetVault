#pragma once

#include "config.h"
#include "imgui.h"
#include "animation.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <filesystem>

// Forward declarations
class TextureManager;
class EventProcessor;
class AudioManager;
class SearchIndex;
struct Asset;
struct Model;
struct Camera3D;
struct Animation2D;

// Grid zoom levels for the results pane
enum class ZoomLevel : int {
  Level0 = 0,
  Level1 = 1,
  Level2 = 2,
  Level3 = 3,
  Level4 = 4,
  Level5 = 5
};

// UI state structure
struct UIState {
  std::atomic<bool> filters_changed{ true };
  std::atomic<bool> event_batch_finished{ false };

  char buffer[256] = "";
  std::string last_buffer = "";
  std::string input_tracking = ""; // Track input to detect real changes

  // Debouncing state
  TimePoint last_keypress_time;
  bool pending_search = false;

  // UI state
  std::vector<Asset> results;

  // Multiple selection support
  std::unordered_set<uint32_t> selected_asset_ids;  // IDs of all selected assets (for fast lookup)
  int selected_asset_index = -1; // Index of most recently selected asset (-1 means no selection)
  std::optional<Asset> selected_asset; // Most recently selected asset (for preview/audio)

  // Asset path state
  std::string assets_path_selected;
  std::string assets_directory;
  bool show_drive_roots = false;
  bool assets_directory_modal_open = false;

  // Fast membership check for current results (IDs only)
  std::unordered_set<uint32_t> results_ids;

  // Infinite scroll state
  static constexpr int LOAD_BATCH_SIZE = 50;
  int loaded_start_index = 0;    // Always 0, never changes
  int loaded_end_index = 0;      // Grows as user scrolls down

  // Thumbnail zoom level for results grid
  ZoomLevel grid_zoom_level = ZoomLevel::Level3;

  // Model preview state
  int model_preview_row = -1;    // Which row has the expanded preview

  // Animation preview state (loaded on-demand, similar to 3D models)
  std::shared_ptr<Animation2D> current_animation;
  std::string current_animation_path;  // Track which animation is loaded to detect asset changes
  Animation2DPlaybackState preview_animation_state;
  std::unordered_map<std::string, Animation2DPlaybackState> grid_animation_states;

  // Folder tree checkbox states
  std::unordered_map<std::string, bool> folder_checkbox_states;
  std::unordered_map<std::string, std::vector<std::string>> folder_children_cache;

  // Audio playback settings
  bool auto_play_audio = true;

  // Drag-and-drop state (track if drag is in progress to prevent multiple initiations)
  bool drag_initiated = false;

  // Area selection state (rubber band selection)
  bool drag_select_started = false;  // True if clicked on background (initiates selection)
  bool drag_select_active = false;   // True after minimum drag distance (shows rectangle)
  ImVec2 drag_select_start;
  ImVec2 drag_select_end;

  // Type filter toggle states
  bool type_filter_2d = false;
  bool type_filter_3d = false;
  bool type_filter_audio = false;
  bool type_filter_shader = false;
  bool type_filter_font = false;

  // Path filter toggle state
  bool path_filter_active = false;
  bool folder_selection_covers_all = true;
  bool folder_selection_empty = false;

  // Path filters (set by clicking on path segments)
  std::vector<std::string> path_filters;
  std::optional<std::string> pending_tree_selection;
  std::unordered_set<std::string> tree_nodes_to_open;
  std::unordered_set<std::string> disable_child_propagation;
};

// UI helper functions

// Clear all search and UI state when changing directories
void clear_ui_state(UIState& ui_state);

// Reset folder tree state and filters
void reset_folder_tree_state(UIState& ui_state);

// Renders a clickable path that allows filtering by directory
void render_clickable_path(const Asset& asset, UIState& ui_state);

// Renders common asset information in standard order: Path, Extension, Type, Size, Modified
void render_common_asset_info(const Asset& asset, UIState& ui_state);

// Custom audio seek bar widget
bool audio_seek_bar(const char* id, float* value, float min_value, float max_value, float width, float height = 4.0f);


// Main UI panel rendering functions
void render_search_panel(UIState& ui_state,
    const SafeAssets& safe_assets,
    TextureManager& texture_manager,
    float panel_width, float panel_height);

void render_progress_panel(UIState& ui_state, SafeAssets& safe_assets,
    float panel_width, float panel_height);

void render_asset_grid(UIState& ui_state, TextureManager& texture_manager,
    SafeAssets& safe_assets, float panel_width, float panel_height);

void render_preview_panel(UIState& ui_state, TextureManager& texture_manager,
    Model& current_model, Camera3D& camera, float panel_width, float panel_height);

void render_folder_tree_panel(UIState& ui_state, float panel_width, float panel_height);

// Shared modal helpers
void open_assets_directory_modal(UIState& ui_state);

namespace folder_tree_utils {
  struct FilterComputationResult {
    std::vector<std::string> filters;
    bool all_selected = false;
    bool any_selected = false;
  };

  const std::vector<std::string>& ensure_children_loaded(UIState& ui_state,
    const std::filesystem::path& dir_path, bool propagate_parent_state = true);
  FilterComputationResult collect_active_filters(UIState& ui_state,
    const std::filesystem::path& root_path);
}
