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
constexpr float SEARCH_BOX_WIDTH = 750.0f;
}

void render_search_panel(UIState& ui_state,
  const SafeAssets& safe_assets,
  TextureManager& texture_manager,
  float panel_width, float panel_height) {
  SpriteAtlas search_frame_atlas = texture_manager.get_ui_elements_atlas();
  const SlicedSprite search_frame_definition = make_16px_frame(0, 3.0f);
  const SlicedSprite toggle_frame_definition = make_8px_frame(2, 3, 3.0f);
  const SlicedSprite toggle_frame_definition_selected = make_8px_frame(2, 2, 3.0f);
  const SlicedSprite& search_frame_definition_selected = search_frame_definition;

  // Temp: tint the search panel background to visualize its bounds.
  ImGui::PushStyleColor(ImGuiCol_ChildBg,
    ImVec4(Theme::ACCENT_BLUE_1.x, Theme::ACCENT_BLUE_1.y, Theme::ACCENT_BLUE_1.z, 0.35f));
  ImGui::BeginChild("SearchRegion", ImVec2(panel_width, panel_height), false);

  const float top_padding = 8.0f;
  const float bottom_padding = 4.0f;
  const float control_spacing_x = 12.0f;
  const float toggle_spacing = 10.0f;
  const float toggle_button_height = 35.0f;

  bool open_settings_modal = false;
  bool request_assets_directory_modal = false;

  ImVec2 content_origin = ImGui::GetCursorScreenPos();
  float content_width = ImGui::GetContentRegionAvail().x;
  float settings_button_size = ImGui::GetFrameHeight() * 2.0f;
  unsigned int settings_icon = texture_manager.get_settings_icon();
  const float frame_padding_y = 16.0f;
  float frame_height = ImGui::GetFontSize() + frame_padding_y * 2.0f;

  if (settings_icon != 0) {
    ImVec2 original_cursor = ImGui::GetCursorPos();
    float button_x = std::max(0.0f, content_width - settings_button_size);
    float button_y = top_padding + std::max(0.0f, (frame_height - settings_button_size) * 0.5f);
    ImVec2 button_pos(button_x, button_y);
    IconButtonParams settings_button;
    settings_button.id = "SettingsButton";
    settings_button.cursor_pos = button_pos;
    settings_button.size = settings_button_size;
    settings_button.icon_texture = settings_icon;
    settings_button.highlight_color = Theme::ACCENT_BLUE_1_ALPHA_80;
    open_settings_modal = draw_icon_button(settings_button);
    ImGui::SetCursorPos(original_cursor);
  }

  float search_x = 0.0f;
  float search_y = top_padding;

  ImGui::SetCursorPos(ImVec2(search_x, search_y));

  ImVec2 frame_pos = ImGui::GetCursorScreenPos();
  ImVec2 frame_size(SEARCH_BOX_WIDTH, frame_height);

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  bool can_layer_frame = (search_frame_atlas.texture_id != 0) && (draw_list != nullptr);
  if (can_layer_frame) {
    draw_list->ChannelsSplit(2);
    draw_list->ChannelsSetCurrent(1);
  }

  bool enter_pressed = fancy_text_input("##Search", ui_state.buffer, sizeof(ui_state.buffer),
    SEARCH_BOX_WIDTH, 20.0f, frame_padding_y, 0.0f);

  if (can_layer_frame) {
    ImVec2 input_min = ImGui::GetItemRectMin();
    ImVec2 input_max = ImGui::GetItemRectMax();
    ImVec2 input_size(input_max.x - input_min.x, input_max.y - input_min.y);
    bool input_hovered = ImGui::IsItemHovered();
    bool input_active = ImGui::IsItemActive();

    const SlicedSprite& frame_def = (input_hovered || input_active)
      ? search_frame_definition_selected
      : search_frame_definition;

    draw_list->ChannelsSetCurrent(0);
    draw_nine_slice_image(search_frame_atlas, frame_def, input_min, input_size);
    draw_list->ChannelsMerge();
  }
  else if (search_frame_atlas.texture_id != 0) {
    draw_nine_slice_image(search_frame_atlas, search_frame_definition, frame_pos, frame_size);
  }

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

  float toggles_y = search_y + std::max(0.0f, (frame_height - toggle_button_height) * 0.5f);

  float button_width_2d = 70.0f;
  float button_width_3d = 70.0f;
  float button_width_audio = 84.0f;
  float button_width_font = 72.0f;

  float toggles_start_x = SEARCH_BOX_WIDTH + control_spacing_x;

  float current_x = toggles_start_x;
  bool any_toggle_changed = false;

  any_toggle_changed |= draw_type_toggle_button("2D", ui_state.type_filter_2d,
    content_origin.x + current_x, content_origin.y + toggles_y,
    button_width_2d, toggle_button_height, Theme::TAG_TYPE_2D,
    search_frame_atlas, toggle_frame_definition, toggle_frame_definition_selected);
  current_x += button_width_2d + toggle_spacing;

  any_toggle_changed |= draw_type_toggle_button("3D", ui_state.type_filter_3d,
    content_origin.x + current_x, content_origin.y + toggles_y,
    button_width_3d, toggle_button_height, Theme::TAG_TYPE_3D,
    search_frame_atlas, toggle_frame_definition, toggle_frame_definition_selected);
  current_x += button_width_3d + toggle_spacing;

  any_toggle_changed |= draw_type_toggle_button("Audio", ui_state.type_filter_audio,
    content_origin.x + current_x, content_origin.y + toggles_y,
    button_width_audio, toggle_button_height, Theme::TAG_TYPE_AUDIO,
    search_frame_atlas, toggle_frame_definition, toggle_frame_definition_selected);
  current_x += button_width_audio + toggle_spacing;

  any_toggle_changed |= draw_type_toggle_button("Font", ui_state.type_filter_font,
    content_origin.x + current_x, content_origin.y + toggles_y,
    button_width_font, toggle_button_height, Theme::TAG_TYPE_FONT,
    search_frame_atlas, toggle_frame_definition, toggle_frame_definition_selected);
  current_x += button_width_font + toggle_spacing;

  if (any_toggle_changed) {
    filter_assets(ui_state, safe_assets);
    ui_state.pending_search = false;
  }

  ImGui::Dummy(ImVec2(0.0f, bottom_padding));
  ImGui::EndChild();
  ImGui::PopStyleColor();

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
