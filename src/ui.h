#pragma once

#include "imgui.h"
#include "search.h"
#include "config.h"
#include <string>

// UI helper functions

// Renders a clickable path that allows filtering by directory
void render_clickable_path(const std::string& full_path, SearchState& search_state);

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
