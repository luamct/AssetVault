#include "ui/ui.h"
#include "theme.h"
#include "config.h"
#include "texture_manager.h"
#include "audio_manager.h"
#include "services.h"
#include "3d.h"
#include "logger.h"
#include "utils.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>

void render_clickable_path(const Asset& asset, UIState& ui_state) {
  const std::string& relative_path = asset.relative_path;

  // Split path into segments
  std::vector<std::string> segments;
  std::stringstream ss(relative_path);
  std::string segment;

  while (std::getline(ss, segment, '/')) {
    if (!segment.empty()) {
      segments.push_back(segment);
    }
  }

  // Render each segment as a clickable link (exclude the last segment if it's a file)
  // Only directory segments should be clickable, not the filename itself
  size_t clickable_segments = segments.size();
  if (clickable_segments > 0) {
    clickable_segments = segments.size() - 1; // Exclude the filename
  }

  // Get available width for wrapping
  float available_width = ImGui::GetContentRegionAvail().x;
  float current_line_width = 0.0f;

  for (size_t i = 0; i < segments.size(); ++i) {
    bool is_clickable = (i < clickable_segments);

    // Calculate the width this segment would take (including separator if not first)
    float separator_width = 0.0f;
    if (i > 0) {
      separator_width = ImGui::CalcTextSize(" / ").x + 4.0f; // Add spacing (2.0f before + 2.0f after)
    }

    // Text width calculation (same for both clickable and non-clickable now)
    float segment_width = ImGui::CalcTextSize(segments[i].c_str()).x;

    // Check if we need to wrap to next line
    if (i > 0) {
      if (current_line_width + separator_width + segment_width > available_width) {
        // Add separator at end of current line, then wrap to new line
        ImGui::SameLine(0, 2.0f);
        ImGui::TextColored(Theme::TEXT_SECONDARY, " /");
        current_line_width = segment_width;
      }
      else {
        // Continue on same line with separator
        current_line_width += separator_width + segment_width;
        ImGui::SameLine(0, 2.0f); // Small spacing between segments
        ImGui::TextColored(Theme::TEXT_SECONDARY, " / ");
        ImGui::SameLine(0, 2.0f);
      }
    }
    else {
      // First segment
      current_line_width = segment_width;
    }

    // Build the path up to this segment
    std::string path_to_segment;
    for (size_t j = 0; j <= i; ++j) {
      if (j > 0) path_to_segment += "/";
      path_to_segment += segments[j];
    }

    if (is_clickable) {
      // Check if this path is already in the filter
      bool is_active = std::find(ui_state.path_filters.begin(),
        ui_state.path_filters.end(),
        path_to_segment) != ui_state.path_filters.end();

      // Choose color based on active state
      ImVec4 link_color = is_active ? Theme::ACCENT_BLUE_2 : Theme::ACCENT_BLUE_1;

      // Render as clickable text link
      ImGui::PushStyleColor(ImGuiCol_Text, link_color);
      ImGui::Text("%s", segments[i].c_str());
      ImGui::PopStyleColor();

      // Get item rect for interaction and underline
      ImVec2 text_min = ImGui::GetItemRectMin();
      ImVec2 text_max = ImGui::GetItemRectMax();
      bool is_hovered = ImGui::IsItemHovered();

      // Draw underline on hover
      if (is_hovered) {
        ImGui::GetWindowDrawList()->AddLine(
          ImVec2(text_min.x, text_max.y - 1.0f),
          ImVec2(text_max.x, text_max.y - 1.0f),
          ImGui::GetColorU32(link_color),
          1.0f
        );

        // Change cursor to hand
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      }

      // Handle click
      if (is_hovered && ImGui::IsMouseClicked(0)) {
        // Single path filter mode - only one can be active at a time
        if (is_active) {
          // If this path is already active, deactivate it (clear all filters)
          ui_state.path_filters.clear();
          ui_state.path_filter_active = false;
        }
        else {
          // Clear all existing filters and add this one
          ui_state.path_filters.clear();
          ui_state.path_filters.push_back(path_to_segment);
          ui_state.path_filter_active = true;
        }

        // Trigger search update
        ui_state.update_needed = true;
      }
    }
    else {
      // Render non-clickable segment (filename) as regular text
      ImGui::TextColored(Theme::TEXT_DARK, "%s", segments[i].c_str());
    }
  }
}

// Renders common asset information in standard order: Path, Extension, Type, Size, Modified
void render_common_asset_info(const Asset& asset, UIState& ui_state) {
  // Path
  ImGui::TextColored(Theme::TEXT_LABEL, "Path: ");
  ImGui::SameLine();
  render_clickable_path(asset, ui_state);

  // Extension
  ImGui::TextColored(Theme::TEXT_LABEL, "Extension: ");
  ImGui::SameLine();
  ImGui::Text("%s", asset.extension.c_str());

  // Type
  ImGui::TextColored(Theme::TEXT_LABEL, "Type: ");
  ImGui::SameLine();
  ImGui::Text("%s", get_asset_type_string(asset.type).c_str());

  // Size
  ImGui::TextColored(Theme::TEXT_LABEL, "Size: ");
  ImGui::SameLine();
  ImGui::Text("%s", format_file_size(asset.size).c_str());

  // Modified
  auto time_t = std::chrono::system_clock::to_time_t(asset.last_modified);
  std::tm tm_buf;
  safe_localtime(&tm_buf, &time_t);
  std::stringstream ss;
  ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
  ImGui::TextColored(Theme::TEXT_LABEL, "Modified: ");
  ImGui::SameLine();
  ImGui::Text("%s", ss.str().c_str());
}

// Custom slider component for audio seek bar
bool audio_seek_bar(const char* id, float* value, float min_value, float max_value, float width, float height) {
  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

  // Calculate dimensions
  const float handle_radius = height * 2.0f; // Circle handle is ~4x the line height
  const ImVec2 size(width, handle_radius * 2.0f);

  // Create invisible button for interaction
  ImGui::InvisibleButton(id, size);
  bool hovered = ImGui::IsItemHovered();
  bool active = ImGui::IsItemActive();

  // Calculate value based on mouse position when dragging
  bool value_changed = false;
  if (active) {
    ImVec2 mouse_pos = ImGui::GetMousePos();
    float mouse_x = mouse_pos.x - cursor_pos.x;
    float new_value = (mouse_x / width) * (max_value - min_value) + min_value;
    if (new_value < min_value) new_value = min_value;
    if (new_value > max_value) new_value = max_value;
    if (*value != new_value) {
      *value = new_value;
      value_changed = true;
    }
  }

  // Calculate current position
  float position_ratio = (max_value > min_value) ? (*value - min_value) / (max_value - min_value) : 0.0f;
  if (position_ratio < 0.0f) position_ratio = 0.0f;
  if (position_ratio > 1.0f) position_ratio = 1.0f;
  float handle_x = cursor_pos.x + position_ratio * width;

  // Colors
  const ImU32 line_color_played = ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.3f, 1.0f));    // Darker line (played portion - before handle)
  const ImU32 line_color_unplayed = ImGui::GetColorU32(ImVec4(0.7f, 0.7f, 0.7f, 1.0f));  // Lighter line (unplayed portion - after handle)
  const ImU32 handle_color = hovered || active ?
    ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)) :    // White when hovered/active
    ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 1.0f));     // Light gray normally

  // Draw the seek bar
  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  // Line center Y position
  float line_y = cursor_pos.y + size.y * 0.5f;

  // Draw played portion (left of handle) - darker
  if (position_ratio > 0.0f) {
    draw_list->AddRectFilled(
      ImVec2(cursor_pos.x, line_y - height * 0.5f),
      ImVec2(handle_x, line_y + height * 0.5f),
      line_color_played,
      height * 0.5f
    );
  }

  // Draw unplayed portion (right of handle) - lighter
  if (position_ratio < 1.0f) {
    draw_list->AddRectFilled(
      ImVec2(handle_x, line_y - height * 0.5f),
      ImVec2(cursor_pos.x + width, line_y + height * 0.5f),
      line_color_unplayed,
      height * 0.5f
    );
  }

  // Draw circular handle
  draw_list->AddCircleFilled(
    ImVec2(handle_x, line_y),
    handle_radius,
    handle_color,
    16  // Number of segments for smooth circle
  );

  return value_changed;
}

// Function to calculate aspect-ratio-preserving dimensions with upscaling limit
static ImVec2 calculate_thumbnail_size(
  int original_width, int original_height, float max_width, float max_height, float max_upscale_factor) {
  float aspect_ratio = static_cast<float>(original_width) / static_cast<float>(original_height);

  float calculated_width = max_width;
  float calculated_height = max_width / aspect_ratio;
  if (calculated_height > max_height) {
    calculated_height = max_height;
    calculated_width = max_height * aspect_ratio;
  }

  // Limit upscaling to the specified factor
  float width_scale = calculated_width / original_width;
  float height_scale = calculated_height / original_height;
  if (width_scale > max_upscale_factor || height_scale > max_upscale_factor) {
    float scale_factor = std::min(max_upscale_factor, std::min(width_scale, height_scale));
    calculated_width = original_width * scale_factor;
    calculated_height = original_height * scale_factor;
  }

  return ImVec2(calculated_width, calculated_height);
}

void render_preview_panel(UIState& ui_state, TextureManager& texture_manager,
  Model& current_model, Camera3D& camera, float panel_width, float panel_height) {
  ImGui::BeginChild("AssetPreview", ImVec2(panel_width, panel_height), true);

  // Use fixed panel dimensions for stable calculations
  float avail_width = panel_width - Config::PREVIEW_INTERNAL_PADDING; // Account for ImGui padding and margins
  float avail_height = avail_width;                           // Square aspect ratio for preview area

  // Track previously selected asset for cleanup
  static uint32_t prev_selected_id = 0;
  static AssetType prev_selected_type = AssetType::Unknown;

  // Handle asset selection changes (by id)
  if ((ui_state.selected_asset ? ui_state.selected_asset->id : 0) != prev_selected_id) {
    if (prev_selected_type == AssetType::Audio && Services::audio_manager().has_audio_loaded()) {
      Services::audio_manager().unload_audio();
    }
    prev_selected_id = ui_state.selected_asset ? ui_state.selected_asset->id : 0;
    prev_selected_type = (ui_state.selected_asset.has_value()) ? ui_state.selected_asset->type : AssetType::Unknown;
  }

  // Validate current selection against filtered results (no disk access)
  if (ui_state.selected_asset_index >= 0) {
    // Bounds check for highlight index only
    if (ui_state.selected_asset_index >= static_cast<int>(ui_state.results.size())) {
      ui_state.selected_asset_index = -1;
    }
  }

  // Clean up selected_asset_ids: remove any IDs that are no longer in results
  std::vector<uint32_t> ids_to_remove;
  for (uint32_t id : ui_state.selected_asset_ids) {
    if (ui_state.results_ids.find(id) == ui_state.results_ids.end()) {
      ids_to_remove.push_back(id);
    }
  }
  for (uint32_t id : ids_to_remove) {
    ui_state.selected_asset_ids.erase(id);
  }

  // If the preview asset is no longer in results, clear it
  if (ui_state.selected_asset &&
      ui_state.results_ids.find(ui_state.selected_asset->id) == ui_state.results_ids.end()) {
    ui_state.selected_asset_index = -1;
    ui_state.selected_asset.reset();
  }

  if (ui_state.selected_asset.has_value()) {
    const Asset& selected_asset = *ui_state.selected_asset;

    // Check if selected asset is a model
    if (selected_asset.type == AssetType::_3D && texture_manager.is_preview_initialized()) {
      // Load the model if it's different from the currently loaded one
      if (selected_asset.path != current_model.path) {
        LOG_DEBUG("=== Loading Model in Main ===");
        LOG_DEBUG("Selected asset: {}", selected_asset.path);
        Model model;
        bool load_success = load_model(selected_asset.path, model, texture_manager);
        if (load_success) {
          set_current_model(current_model, model);
          camera.reset(); // Reset camera to default view for new model
          LOG_DEBUG("Model loaded successfully in main");
        }
        else {
          LOG_DEBUG("Failed to load model in main");
        }
        LOG_DEBUG("===========================");
      }

      // Get the current model for displaying info
      const Model& current_model_ref = get_current_model(current_model);

      // 3D Preview Viewport for models
      ImVec2 viewport_size(avail_width, avail_height);

      // Render the 3D preview to framebuffer texture
      int fb_width = static_cast<int>(avail_width);
      int fb_height = static_cast<int>(avail_height);
      render_3d_preview(fb_width, fb_height, current_model, texture_manager, camera, ImGui::GetIO().DeltaTime);

      // Center the viewport in the panel (same logic as 2D previews)
      ImVec2 container_pos = ImGui::GetCursorScreenPos();
      float image_x_offset = (avail_width - viewport_size.x) * 0.5f;
      float image_y_offset = (avail_height - viewport_size.y) * 0.5f;
      ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);
      ImGui::SetCursorScreenPos(image_pos);

      // Draw border around the viewport
      ImVec2 border_min = image_pos;
      ImVec2 border_max(border_min.x + viewport_size.x, border_min.y + viewport_size.y);
      ImGui::GetWindowDrawList()->AddRect(border_min, border_max, Theme::COLOR_BORDER_GRAY_U32, 8.0f, 0, 1.0f);

      // Display the 3D viewport
      ImGui::Image(
        (ImTextureID) (intptr_t) texture_manager.get_preview_texture(), 
        viewport_size,
        ImVec2(0.0f, 1.0f),   // bottom-left
        ImVec2(1.0f, 0.0f)    // top-right
      );

      // Handle mouse interactions for 3D camera control
      bool is_image_hovered = ImGui::IsItemHovered();

      if (is_image_hovered) {
        ImGuiIO& io = ImGui::GetIO();

        // Handle mouse wheel for zoom
        if (io.MouseWheel != 0.0f) {
          if (io.MouseWheel > 0.0f) {
            camera.zoom *= Config::PREVIEW_3D_ZOOM_FACTOR; // Zoom in
          }
          else {
            camera.zoom /= Config::PREVIEW_3D_ZOOM_FACTOR; // Zoom out
          }
          // Clamp zoom to reasonable bounds
          camera.zoom = std::max(0.1f, std::min(camera.zoom, 10.0f));
        }

        // Handle double-click for reset (check this first)
        if (ImGui::IsMouseDoubleClicked(0)) {
          camera.reset();
          camera.is_dragging = false; // Cancel any dragging
        }
        // Handle single click to start dragging (only if not double-click)
        else if (ImGui::IsMouseClicked(0)) {
          camera.is_dragging = true;
          camera.last_mouse_x = io.MousePos.x;
          camera.last_mouse_y = io.MousePos.y;
        }
      }

      // Handle dragging (can continue even if mouse leaves image area)
      if (camera.is_dragging) {
        ImGuiIO& io = ImGui::GetIO();

        // Check if mouse button is still held down
        if (io.MouseDown[0]) {
          float delta_x = io.MousePos.x - camera.last_mouse_x;
          float delta_y = io.MousePos.y - camera.last_mouse_y;

          // Only update if there's actual movement
          if (delta_x != 0.0f || delta_y != 0.0f) {
            // Update rotation based on mouse movement using config sensitivity
            camera.rotation_y -= delta_x * Config::PREVIEW_3D_ROTATION_SENSITIVITY; // Horizontal rotation (left/right)
            camera.rotation_x += delta_y * Config::PREVIEW_3D_ROTATION_SENSITIVITY; // Vertical rotation (up/down)

            // Clamp vertical rotation to avoid flipping
            camera.rotation_x = std::max(-89.0f, std::min(camera.rotation_x, 89.0f));

            camera.last_mouse_x = io.MousePos.x;
            camera.last_mouse_y = io.MousePos.y;
          }
        }
        else {
          // Mouse button released, stop dragging
          camera.is_dragging = false;
        }
      }

      // Restore cursor for info below
      ImGui::SetCursorScreenPos(container_pos);
      ImGui::Dummy(ImVec2(0, avail_height + 10));

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Common asset information
      render_common_asset_info(selected_asset, ui_state);

      // 3D-specific information
      if (current_model_ref.loaded) {
        int vertex_count =
          static_cast<int>(current_model_ref.vertices.size() / MODEL_VERTEX_FLOAT_STRIDE);
        int face_count = static_cast<int>(current_model_ref.indices.size() / 3); // 3 indices per triangle

        ImGui::TextColored(Theme::TEXT_LABEL, "Vertices: ");
        ImGui::SameLine();
        ImGui::Text("%d", vertex_count);

        ImGui::TextColored(Theme::TEXT_LABEL, "Faces: ");
        ImGui::SameLine();
        ImGui::Text("%d", face_count);
      }
    }
    else if (selected_asset.type == AssetType::Audio && Services::audio_manager().is_initialized()) {
      // Audio handling for sound assets

      // Load the audio file if it's different from the currently loaded one
      const std::string asset_path = selected_asset.path;
      const std::string current_file = Services::audio_manager().get_current_file();

      if (asset_path != current_file) {
        LOG_DEBUG("Main: Audio file changed from '{}' to '{}'", current_file, asset_path);
        bool loaded = Services::audio_manager().load_audio(asset_path);
        if (loaded) {
          // Set initial volume to match our slider default
          Services::audio_manager().set_volume(0.5f);
          // Auto-play if enabled
          if (ui_state.auto_play_audio) {
            Services::audio_manager().play();
          }
        }
        else {
          LOG_DEBUG("Main: Failed to load audio, current_file is now '{}'", Services::audio_manager().get_current_file());
        }
      }

      // Display audio icon in preview area
      const TextureCacheEntry& audio_entry = texture_manager.get_asset_texture(selected_asset);
      if (audio_entry.get_texture_id() != 0) {
        float icon_dim = Config::ICON_SCALE * std::min(avail_width, avail_height);
        ImVec2 icon_size(icon_dim, icon_dim);

        // Center the icon
        ImVec2 container_pos = ImGui::GetCursorScreenPos();
        float image_x_offset = (avail_width - icon_size.x) * 0.5f;
        float image_y_offset = (avail_height - icon_size.y) * 0.5f;
        ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);
        ImGui::SetCursorScreenPos(image_pos);

        ImGui::Image((ImTextureID) (intptr_t) audio_entry.get_texture_id(), icon_size);

        // Restore cursor for controls below
        ImGui::SetCursorScreenPos(container_pos);
        ImGui::Dummy(ImVec2(0, avail_height + 10));
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Audio controls - single row layout
      if (Services::audio_manager().has_audio_loaded()) {
        float duration = Services::audio_manager().get_duration();
        float position = Services::audio_manager().get_position();
        bool is_playing = Services::audio_manager().is_playing();

        // Format time helper lambda
        auto format_time = [](float seconds) -> std::string {
          int mins = static_cast<int>(seconds) / 60;
          int secs = static_cast<int>(seconds) % 60;
          char buffer[16];
          snprintf(buffer, sizeof(buffer), "%02d:%02d", mins, secs);
          return std::string(buffer);
          };

        // Create a single row with all controls
        ImGui::BeginGroup();

        // 1. Square Play/Pause button with transparent background
        const float button_size = 32.0f;

        // Store the baseline Y position BEFORE drawing the button for proper alignment
        float baseline_y = ImGui::GetCursorPosY();

        unsigned int icon_texture = is_playing ? texture_manager.get_pause_icon() : texture_manager.get_play_icon();

        // Make button background transparent
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 0.1f)); // Very light hover
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.7f, 0.7f, 0.2f));  // Light press

        if (ImGui::ImageButton("##PlayPause", (ImTextureID) (intptr_t) icon_texture, ImVec2(button_size, button_size))) {
          if (is_playing) {
            Services::audio_manager().pause();
          }
          else {
            Services::audio_manager().play();
          }
        }

        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 8); // 8px spacing

        // 2. Current timestamp
        ImGui::SetCursorPosY(baseline_y + button_size * 0.5f - 6.0);
        ImGui::Text("%s", format_time(position).c_str());

        ImGui::SameLine(0, 16);

        // 3. Custom seek bar - thin line with circle handle
        static bool seeking = false;
        static float seek_position = 0.0f;

        if (!seeking) {
          seek_position = position;
        }

        const float seek_bar_width = 120.0f;
        const float seek_bar_height = 4.0f; // Thin line height

        // Use our custom seek bar - vertically centered
        ImGui::SetCursorPosY(baseline_y + button_size * 0.5f - seek_bar_height); // Center based on handle radius
        bool seek_changed = audio_seek_bar("##CustomSeek", &seek_position, 0.0f, duration, seek_bar_width, seek_bar_height);

        if (seek_changed) {
          seeking = true;
          Services::audio_manager().set_position(seek_position);
        }

        // Reset seeking flag when not actively dragging
        if (seeking && !ImGui::IsItemActive()) {
          seeking = false;
        }

        ImGui::SameLine(0, 12);

        // 4. Total duration
        ImGui::SetCursorPosY(baseline_y + button_size * 0.5f - 6.0f);
        ImGui::Text("%s", format_time(duration).c_str());

        ImGui::SameLine(0, 12);

        // 5. Speaker icon - vertically centered
        const float icon_size = 24.0f;
        ImGui::SetCursorPosY(baseline_y + (button_size - 0.5 * icon_size) * 0.5f);
        ImGui::Image((ImTextureID) (intptr_t) texture_manager.get_speaker_icon(), ImVec2(icon_size, icon_size));

        ImGui::SameLine(0, 6);

        // 6. Volume slider - custom horizontal seek bar style
        static float audio_volume = 0.5f; // Start at 50%
        const float volume_width = 60.0f;  // Small horizontal slider
        const float volume_height = 3.0f;   // Thinner than seek bar

        ImGui::SetCursorPosY(baseline_y + button_size * 0.5f);

        if (audio_seek_bar("##VolumeBar", &audio_volume, 0.0f, 1.0f, volume_width, volume_height)) {
          Services::audio_manager().set_volume(audio_volume);
        }

        // Show percentage on hover
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Volume: %d%%", (int) (audio_volume * 100));
        }

        ImGui::EndGroup();

        // Auto-play checkbox below the player
        ImGui::Spacing();
        ImGui::Checkbox("Auto-play", &ui_state.auto_play_audio);
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Common asset information
      render_common_asset_info(selected_asset, ui_state);
    }
    else if (selected_asset.extension == ".gif") {
      auto now = std::chrono::steady_clock::now();

      if (ui_state.current_animation_path != selected_asset.path || !ui_state.current_animation) {
        LOG_DEBUG("[UI] Loading animated GIF on-demand: {}", selected_asset.path);
        ui_state.current_animation = texture_manager.get_or_load_animated_gif(selected_asset.path);
        ui_state.current_animation_path = selected_asset.path;
        ui_state.preview_animation_state.reset();
      }

      if (!ui_state.current_animation) {
        ui_state.preview_animation_state.reset();
      } else {
        ui_state.preview_animation_state.set_animation(ui_state.current_animation, now);
      }

      const auto& animation = ui_state.current_animation;

      if (animation && !animation->empty()) {
        ImVec2 preview_size(avail_width, avail_height);
        if (animation->width > 0 && animation->height > 0) {
          preview_size = calculate_thumbnail_size(
            animation->width,
            animation->height,
            avail_width, avail_height,
            Config::MAX_PREVIEW_UPSCALE_FACTOR
          );
        }

        ImVec2 container_pos = ImGui::GetCursorScreenPos();
        float image_x_offset = (avail_width - preview_size.x) * 0.5f;
        float image_y_offset = (avail_height - preview_size.y) * 0.5f;
        ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);
        ImGui::SetCursorScreenPos(image_pos);

        ImVec2 border_min = image_pos;
        ImVec2 border_max(border_min.x + preview_size.x, border_min.y + preview_size.y);
        ImGui::GetWindowDrawList()->AddRect(border_min, border_max, Theme::COLOR_BORDER_GRAY_U32, 8.0f, 0, 1.0f);

        unsigned int frame_texture = ui_state.preview_animation_state.current_texture(now);
        if (frame_texture == 0 && !animation->frame_textures.empty()) {
          frame_texture = animation->frame_textures.front();
        }
        ImGui::Image((ImTextureID) (intptr_t) frame_texture, preview_size);

        ImGui::SetCursorScreenPos(container_pos);
        ImGui::Dummy(ImVec2(0, avail_height + 10));
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Common asset information
      render_common_asset_info(selected_asset, ui_state);

      // GIF-specific information (dimensions)
      if (animation) {
        ImGui::TextColored(Theme::TEXT_LABEL, "Dimensions: ");
        ImGui::SameLine();
        ImGui::Text("%dx%d", animation->width, animation->height);

        ImGui::TextColored(Theme::TEXT_LABEL, "Frames: ");
        ImGui::SameLine();
        ImGui::Text("%d", animation->frame_count());
      }
    }
    else {
      // 2D Preview for non-GIF assets
      const TextureCacheEntry& preview_entry = texture_manager.get_asset_texture(selected_asset);
      if (preview_entry.get_texture_id() != 0) {
        ImVec2 preview_size(avail_width, avail_height);

        if (selected_asset.type == AssetType::_2D || selected_asset.type == AssetType::Font) {
          // TextureCacheEntry already contains dimensions
          if (preview_entry.width > 0 && preview_entry.height > 0) {
            preview_size = calculate_thumbnail_size(preview_entry.width, preview_entry.height, avail_width, avail_height, Config::MAX_PREVIEW_UPSCALE_FACTOR);
          }
        }
        else {
          // For type icons, use ICON_SCALE * min(available_width, available_height)
          float icon_dim = Config::ICON_SCALE * std::min(avail_width, avail_height);
          preview_size = ImVec2(icon_dim, icon_dim);
        }

        // Center the preview image in the panel (same logic as grid)
        ImVec2 container_pos = ImGui::GetCursorScreenPos();
        float image_x_offset = (avail_width - preview_size.x) * 0.5f;
        float image_y_offset = (avail_height - preview_size.y) * 0.5f;
        ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);
        ImGui::SetCursorScreenPos(image_pos);

        // Draw border around the image
        ImVec2 border_min = image_pos;
        ImVec2 border_max(border_min.x + preview_size.x, border_min.y + preview_size.y);
        ImGui::GetWindowDrawList()->AddRect(border_min, border_max, Theme::COLOR_BORDER_GRAY_U32, 8.0f, 0, 1.0f);

        // Display static image
        ImGui::Image((ImTextureID) (intptr_t) preview_entry.get_texture_id(), preview_size);

        // Restore cursor for info below
        ImGui::SetCursorScreenPos(container_pos);
        ImGui::Dummy(ImVec2(0, avail_height + 10));
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Common asset information
      render_common_asset_info(selected_asset, ui_state);

      // 2D-specific information
      if (selected_asset.type == AssetType::_2D) {
        int width, height;
        if (texture_manager.get_texture_dimensions(selected_asset.path, width, height)) {
          ImGui::TextColored(Theme::TEXT_LABEL, "Dimensions: ");
          ImGui::SameLine();
          ImGui::Text("%dx%d", width, height);
        }
      }
    }
  }
  else {
    ImGui::TextColored(Theme::TEXT_DISABLED_DARK, "No asset selected");
    ImGui::TextColored(Theme::TEXT_DISABLED_DARK, "Click on an asset to preview");
  }

  ImGui::EndChild();
}
