#include "ui/ui.h"
#include "theme.h"
#include "config.h"
#include "search.h"
#include "utils.h"
#include "imgui.h"
#include "texture_manager.h"
#include "ui/components.h"

#include <algorithm>
#include <chrono>
#include <cfloat>

namespace {
constexpr float SEARCH_BOX_WIDTH = 375.0f;
constexpr float SEARCH_BOX_HEIGHT = 60.0f;
}

void render_search_panel(UIState& ui_state,
  const SafeAssets& safe_assets,
  TextureManager& texture_manager,
  float panel_width, float panel_height) {
  ImGui::BeginChild("SearchRegion", ImVec2(panel_width, panel_height), false);

  const float top_padding = 10.0f;
  const float bottom_padding = 5.0f;
  const float toggle_gap = 10.0f;
  const float toggle_button_height = 35.0f;

  bool open_settings_modal = false;
  bool request_assets_directory_modal = false;

  ImVec2 content_origin = ImGui::GetCursorScreenPos();
  float content_width = ImGui::GetContentRegionAvail().x;
  float settings_button_size = ImGui::GetFrameHeight() * 2.0f;
  float settings_button_padding = 8.0f;
  unsigned int settings_icon = texture_manager.get_settings_icon();

  if (settings_icon != 0) {
    ImVec2 original_cursor = ImGui::GetCursorPos();
    float button_x = std::max(0.0f, content_width - settings_button_size - settings_button_padding);
    ImVec2 button_pos(button_x, top_padding);
    IconButtonParams settings_button;
    settings_button.id = "SettingsButton";
    settings_button.cursor_pos = button_pos;
    settings_button.size = settings_button_size;
    settings_button.icon_texture = settings_icon;
    open_settings_modal = draw_icon_button(settings_button);
    ImGui::SetCursorPos(original_cursor);
  }

  ImGuiIO& search_io = ImGui::GetIO();
  char search_fps_buf[32];
  snprintf(search_fps_buf, sizeof(search_fps_buf), "%.1f FPS", search_io.Framerate);
  ImGui::TextColored(Theme::TEXT_SECONDARY, "%s", search_fps_buf);

  float local_search_x = (content_width - SEARCH_BOX_WIDTH) * 0.5f;
  local_search_x = std::max(0.0f, local_search_x);
  float content_search_y = top_padding;

  ImGui::SetCursorPos(ImVec2(local_search_x, content_search_y));

  bool enter_pressed = fancy_text_input("##Search", ui_state.buffer, sizeof(ui_state.buffer),
    SEARCH_BOX_WIDTH, 20.0f, 16.0f, 25.0f);

  float search_bottom_y = content_search_y + SEARCH_BOX_HEIGHT;

  std::string current_input(ui_state.buffer);

  if (enter_pressed) {
    filter_assets(ui_state, safe_assets);
    ui_state.last_buffer = current_input;
    ui_state.input_tracking = current_input;
    ui_state.pending_search = false;
  }
  else if (current_input != ui_state.input_tracking) {
    ui_state.input_tracking = current_input;
    ui_state.pending_search = true;
    ui_state.last_keypress_time = std::chrono::steady_clock::now();
  }

  if (ui_state.pending_search) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - ui_state.last_keypress_time);
    const int DEBOUNCE_DELAY_MS = 350;
    if (elapsed.count() >= DEBOUNCE_DELAY_MS) {
      filter_assets(ui_state, safe_assets);
      ui_state.pending_search = false;
      ui_state.last_buffer = current_input;
    }
  }

  float toggles_y = search_bottom_y + toggle_gap;
  float toggle_spacing = 10.0f;

  float button_width_2d = 70.0f;
  float button_width_3d = 70.0f;
  float button_width_audio = 84.0f;
  float button_width_font = 72.0f;

  float total_toggle_width = button_width_2d + button_width_3d + button_width_audio +
    button_width_font + (toggle_spacing * 3);

  float toggles_start_x = (content_width - total_toggle_width) * 0.5f;
  toggles_start_x = std::max(0.0f, toggles_start_x);

  float current_x = toggles_start_x;
  bool any_toggle_changed = false;

  any_toggle_changed |= draw_type_toggle_button("2D", ui_state.type_filter_2d,
    content_origin.x + current_x, content_origin.y + toggles_y,
    button_width_2d, toggle_button_height, Theme::TAG_TYPE_2D);
  current_x += button_width_2d + toggle_spacing;

  any_toggle_changed |= draw_type_toggle_button("3D", ui_state.type_filter_3d,
    content_origin.x + current_x, content_origin.y + toggles_y,
    button_width_3d, toggle_button_height, Theme::TAG_TYPE_3D);
  current_x += button_width_3d + toggle_spacing;

  any_toggle_changed |= draw_type_toggle_button("Audio", ui_state.type_filter_audio,
    content_origin.x + current_x, content_origin.y + toggles_y,
    button_width_audio, toggle_button_height, Theme::TAG_TYPE_AUDIO);
  current_x += button_width_audio + toggle_spacing;

  any_toggle_changed |= draw_type_toggle_button("Font", ui_state.type_filter_font,
    content_origin.x + current_x, content_origin.y + toggles_y,
    button_width_font, toggle_button_height, Theme::TAG_TYPE_FONT);
  current_x += button_width_font + toggle_spacing;

  if (any_toggle_changed) {
    filter_assets(ui_state, safe_assets);
    ui_state.pending_search = false;
  }

  ImGui::Dummy(ImVec2(0.0f, bottom_padding));
  ImGui::EndChild();

  const char* SETTINGS_MODAL_ID = "Settings";
  if (open_settings_modal) {
    ImGui::OpenPopup(SETTINGS_MODAL_ID);
  }

  bool settings_popup_active = ImGui::IsPopupOpen(SETTINGS_MODAL_ID);
  bool prepare_modal = open_settings_modal || settings_popup_active;
  if (prepare_modal) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float modal_width = viewport ? viewport->Size.x * 0.5f : 400.0f;
    ImVec2 modal_size(modal_width, 0.0f);
    if (viewport) {
      ImVec2 modal_center(
        viewport->Pos.x + viewport->Size.x * 0.5f,
        viewport->Pos.y + viewport->Size.y * 0.5f);
      ImGui::SetNextWindowPos(modal_center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }
    ImGui::SetNextWindowSize(modal_size, ImGuiCond_Always);
  }

  bool dim_color_pushed = false;
  if (settings_popup_active) {
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.3f));
    dim_color_pushed = true;
  }

  if (ImGui::BeginPopupModal(SETTINGS_MODAL_ID, nullptr,
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    constexpr float SETTINGS_LABEL_Y_OFFSET = 4.0f;
    if (ImGui::BeginTable("SettingsTable", 2, ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.0f);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImVec2 label_pos = ImGui::GetCursorPos();
      ImGui::SetCursorPos(ImVec2(label_pos.x, label_pos.y + SETTINGS_LABEL_Y_OFFSET));
      ImGui::TextColored(Theme::TEXT_SECONDARY, "Assets directory");

      ImGui::TableSetColumnIndex(1);
      if (!ui_state.assets_directory.empty()) {
        const std::string display_path = ui_state.assets_directory;
        if (draw_wrapped_settings_entry("AssetsDirectoryButton", display_path,
            Theme::TEXT_SECONDARY)) {
          request_assets_directory_modal = true;
          ImGui::CloseCurrentPopup();
        }
      }
      else {
        if (draw_wrapped_settings_entry("AssetsDirectoryPlaceholder",
            "Select Assets Folder", Theme::TEXT_DISABLED_DARK)) {
          request_assets_directory_modal = true;
          ImGui::CloseCurrentPopup();
        }
      }

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImVec2 axes_label_pos = ImGui::GetCursorPos();
      ImGui::SetCursorPos(ImVec2(axes_label_pos.x, axes_label_pos.y + SETTINGS_LABEL_Y_OFFSET));
      ImGui::TextColored(Theme::TEXT_SECONDARY, "Draw debug axes");

      ImGui::TableSetColumnIndex(1);
      ImGuiStyle& settings_style = ImGui::GetStyle();
      ImVec2 compact_padding(settings_style.FramePadding.x * 0.7f,
        settings_style.FramePadding.y * 0.7f);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, compact_padding);
      bool draw_axes = Config::draw_debug_axes();
      ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::COLOR_TRANSPARENT);
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::COLOR_TRANSPARENT);
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::COLOR_TRANSPARENT);
      if (ImGui::Checkbox("##DrawDebugAxes", &draw_axes)) {
        Config::set_draw_debug_axes(draw_axes);
      }
      ImGui::PopStyleColor(3);
      ImGui::PopStyleVar();

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImVec2 projection_label_pos = ImGui::GetCursorPos();
      ImGui::SetCursorPos(ImVec2(projection_label_pos.x,
        projection_label_pos.y + SETTINGS_LABEL_Y_OFFSET));
      ImGui::TextColored(Theme::TEXT_SECONDARY, "3D projection");

      ImGui::TableSetColumnIndex(1);
      std::string projection_pref = ui_state.preview_projection;
      bool ortho_selected = projection_pref != Config::CONFIG_VALUE_PROJECTION_PERSPECTIVE;
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, compact_padding);
      if (ImGui::RadioButton("Orthographic##PreviewProjection", ortho_selected)) {
        projection_pref = Config::CONFIG_VALUE_PROJECTION_ORTHOGRAPHIC;
        ui_state.preview_projection = projection_pref;
        Config::set_preview_projection(projection_pref);
      }
      ImGui::SameLine();
      bool perspective_selected = projection_pref == Config::CONFIG_VALUE_PROJECTION_PERSPECTIVE;
      if (ImGui::RadioButton("Perspective##PreviewProjection", perspective_selected)) {
        projection_pref = Config::CONFIG_VALUE_PROJECTION_PERSPECTIVE;
        ui_state.preview_projection = projection_pref;
        Config::set_preview_projection(projection_pref);
      }
      ImGui::PopStyleVar();

      ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  if (dim_color_pushed) {
    ImGui::PopStyleColor();
  }

  if (request_assets_directory_modal) {
    open_assets_directory_modal(ui_state);
  }
}
