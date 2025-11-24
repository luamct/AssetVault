#include "ui/ui.h"
#include "theme.h"
#include "config.h"
#include "search.h"
#include "utils.h"
#include "imgui.h"
#include "texture_manager.h"

#include <algorithm>
#include <chrono>
#include <cfloat>

namespace {
bool draw_settings_icon_button(const char* id, unsigned int icon_texture,
    const ImVec2& cursor_pos, float button_size) {
  ImGui::SetCursorPos(cursor_pos);
  ImGui::PushID(id);

  ImVec2 size(button_size, button_size);
  bool clicked = ImGui::InvisibleButton("Button", size);
  bool hovered = ImGui::IsItemHovered();
  bool active = ImGui::IsItemActive();

  ImVec2 min = ImGui::GetItemRectMin();
  ImVec2 max = ImGui::GetItemRectMax();

  if (hovered || active) {
    ImVec4 highlight = Theme::COLOR_SEMI_TRANSPARENT;
    if (active) {
      highlight.w = std::min(1.0f, highlight.w + 0.2f);
    }
    ImGui::GetWindowDrawList()->AddRectFilled(min, max,
      Theme::ToImU32(highlight), 8.0f);
  }

  ImVec4 icon_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  float icon_padding = std::max(2.0f, button_size * 0.15f);
  if (icon_texture != 0) {
    ImVec2 icon_min(min.x + icon_padding, min.y + icon_padding);
    ImVec2 icon_max(max.x - icon_padding, max.y - icon_padding);
    ImGui::GetWindowDrawList()->AddImage(
      (ImTextureID) (intptr_t) icon_texture,
      icon_min,
      icon_max,
      ImVec2(0.0f, 0.0f),
      ImVec2(1.0f, 1.0f),
      Theme::ToImU32(icon_color));
  }

  ImGui::PopID();
  return clicked;
}
}

bool fancy_text_input(const char* label, char* buffer, size_t buffer_size, float width,
    float padding_x, float padding_y, float corner_radius) {
  ImGui::PushItemWidth(width);

  float font_height = ImGui::GetFontSize();
  float actual_input_height = font_height + (padding_y * 2.0f);

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, corner_radius);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding_x, padding_y));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.98f, 0.98f, 0.98f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));

  ImVec2 shadow_offset(2.0f, 2.0f);
  ImVec2 input_pos = ImGui::GetCursorScreenPos();
  ImVec2 shadow_min(input_pos.x + shadow_offset.x, input_pos.y + shadow_offset.y);
  ImVec2 shadow_max(shadow_min.x + width, shadow_min.y + actual_input_height);

  ImGui::GetWindowDrawList()->AddRectFilled(
    shadow_min, shadow_max,
    ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.12f)),
    corner_radius);

  bool result = ImGui::InputText(label, buffer, buffer_size, ImGuiInputTextFlags_EnterReturnsTrue);

  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(2);
  ImGui::PopItemWidth();
  return result;
}

bool draw_type_toggle_button(const char* label, bool& toggle_state, float x_pos, float y_pos,
    float button_width, float button_height) {
  ImVec2 button_min(x_pos, y_pos);
  ImVec2 button_max(button_min.x + button_width, button_min.y + button_height);

  ImVec2 mouse_pos = ImGui::GetMousePos();
  bool is_hovered = (mouse_pos.x >= button_min.x && mouse_pos.x <= button_max.x &&
    mouse_pos.y >= button_min.y && mouse_pos.y <= button_max.y);

  ImVec4 bg_color = Theme::BACKGROUND_WHITE;
  if (toggle_state) {
    bg_color = Theme::TOGGLE_ON_BG;
  }
  else if (is_hovered) {
    bg_color = Theme::TOGGLE_HOVER_BG;
  }

  ImVec4 border_color = toggle_state ? Theme::TOGGLE_ON_BORDER : Theme::TOGGLE_OFF_BORDER;
  ImVec4 text_color = toggle_state ? Theme::TOGGLE_ON_TEXT : Theme::TOGGLE_OFF_TEXT;

  float button_rounding = button_height * 0.5f;
  float border_thickness = 1.0f;

  ImGui::GetWindowDrawList()->AddRectFilled(button_min, button_max, Theme::ToImU32(bg_color), button_rounding);
  ImGui::GetWindowDrawList()->AddRect(button_min, button_max, Theme::ToImU32(border_color), button_rounding, 0, border_thickness);

  ImVec2 text_size = ImGui::CalcTextSize(label);
  ImVec2 text_pos(
    button_min.x + (button_width - text_size.x) * 0.5f,
    button_min.y + (button_height - text_size.y) * 0.5f);
  ImGui::GetWindowDrawList()->AddText(text_pos, Theme::ToImU32(text_color), label);

  bool clicked = false;
  if (is_hovered && ImGui::IsMouseClicked(0)) {
    toggle_state = !toggle_state;
    clicked = true;
  }

  return clicked;
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
    open_settings_modal = draw_settings_icon_button("SettingsButton", settings_icon,
      button_pos, settings_button_size);
    ImGui::SetCursorPos(original_cursor);
  }

  ImGuiIO& search_io = ImGui::GetIO();
  char search_fps_buf[32];
  snprintf(search_fps_buf, sizeof(search_fps_buf), "%.1f FPS", search_io.Framerate);
  ImGui::TextColored(Theme::TEXT_SECONDARY, "%s", search_fps_buf);

  float local_search_x = (content_width - Config::SEARCH_BOX_WIDTH) * 0.5f;
  local_search_x = std::max(0.0f, local_search_x);
  float content_search_y = top_padding;

  ImGui::SetCursorPos(ImVec2(local_search_x, content_search_y));

  bool enter_pressed = fancy_text_input("##Search", ui_state.buffer, sizeof(ui_state.buffer),
    Config::SEARCH_BOX_WIDTH, 20.0f, 16.0f, 25.0f);

  float search_bottom_y = content_search_y + Config::SEARCH_BOX_HEIGHT;

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
  float button_width_shader = 96.0f;
  float button_width_font = 72.0f;

  float total_toggle_width = button_width_2d + button_width_3d + button_width_audio +
    button_width_shader + button_width_font + (toggle_spacing * 4);

  float toggles_start_x = (content_width - total_toggle_width) * 0.5f;
  toggles_start_x = std::max(0.0f, toggles_start_x);

  float current_x = toggles_start_x;
  bool any_toggle_changed = false;

  any_toggle_changed |= draw_type_toggle_button("2D", ui_state.type_filter_2d,
    content_origin.x + current_x, content_origin.y + toggles_y,
    button_width_2d, toggle_button_height);
  current_x += button_width_2d + toggle_spacing;

  any_toggle_changed |= draw_type_toggle_button("3D", ui_state.type_filter_3d,
    content_origin.x + current_x, content_origin.y + toggles_y,
    button_width_3d, toggle_button_height);
  current_x += button_width_3d + toggle_spacing;

  any_toggle_changed |= draw_type_toggle_button("Audio", ui_state.type_filter_audio,
    content_origin.x + current_x, content_origin.y + toggles_y,
    button_width_audio, toggle_button_height);
  current_x += button_width_audio + toggle_spacing;

  any_toggle_changed |= draw_type_toggle_button("Shader", ui_state.type_filter_shader,
    content_origin.x + current_x, content_origin.y + toggles_y,
    button_width_shader, toggle_button_height);
  current_x += button_width_shader + toggle_spacing;

  any_toggle_changed |= draw_type_toggle_button("Font", ui_state.type_filter_font,
    content_origin.x + current_x, content_origin.y + toggles_y,
    button_width_font, toggle_button_height);
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
    if (ImGui::BeginTable("SettingsTable", 2, ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.0f);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextColored(Theme::TEXT_SECONDARY, "Assets directory");

      ImGui::TableSetColumnIndex(1);
      if (!ui_state.assets_directory.empty()) {
        const std::string display_path = ui_state.assets_directory;
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COLOR_SEMI_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::COLOR_SEMI_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TEXT_SECONDARY);
        ImGui::PushID("AssetsDirectoryButton");
        if (ImGui::Button(display_path.c_str(), ImVec2(0.0f, 0.0f))) {
          request_assets_directory_modal = true;
          ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
        ImGui::PopStyleColor(4);
      }
      else {
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COLOR_SEMI_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::COLOR_SEMI_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TEXT_DISABLED_DARK);
        ImGui::PushID("AssetsDirectoryPlaceholder");
        if (ImGui::Button("Select Assets Folder", ImVec2(0.0f, 0.0f))) {
          request_assets_directory_modal = true;
          ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
        ImGui::PopStyleColor(4);
      }

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
