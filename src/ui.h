#pragma once

#include "imgui.h"
#include "config.h"
#include "ui_state.h"
#include <string>
#include <map>
#include <mutex>

// Forward declarations
class TextureManager;
class EventProcessor;
class AudioManager;
class SearchIndex;
struct Asset;
struct Model;
struct Camera3D;

// UI helper functions

// Clear all search and UI state when changing directories
void clear_ui_state(UIState& ui_state);

// Renders a clickable path that allows filtering by directory
void render_clickable_path(const Asset& asset, UIState& ui_state);

// Renders common asset information in standard order: Path, Extension, Type, Size, Modified
void render_common_asset_info(const Asset& asset, UIState& ui_state);

// Custom audio seek bar widget
bool audio_seek_bar(const char* id, float* value, float min_value, float max_value, float width, float height = 4.0f);

// Calculate thumbnail size based on asset dimensions
ImVec2 calculate_thumbnail_size(
    int original_width, int original_height,
    float max_width, float max_height,
    float max_upscale_factor
);

// Fancy styled text input with shadow effect
bool fancy_text_input(const char* label, char* buffer, size_t buffer_size, float width,
    float padding_x = 20.0f, float padding_y = 16.0f,
    float corner_radius = 25.0f, ImGuiInputTextFlags flags = 0);

// Draw type filter toggle button
bool draw_type_toggle_button(const char* label, bool& toggle_state, float x_pos, float y_pos,
    float button_width, float button_height);

// Main UI panel rendering functions
void render_search_panel(UIState& ui_state,
    std::map<std::string, Asset>& assets,
    std::mutex& assets_mutex, SearchIndex& search_index,
    float panel_width, float panel_height);

void render_progress_panel(UIState& ui_state, EventProcessor* processor,
    float panel_width, float panel_height);

void render_asset_grid(UIState& ui_state, TextureManager& texture_manager,
    std::map<std::string, Asset>& assets, float panel_width, float panel_height);

void render_preview_panel(UIState& ui_state, TextureManager& texture_manager,
    AudioManager& audio_manager, Model& current_model,
    Camera3D& camera, float panel_width, float panel_height);
