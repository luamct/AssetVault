#pragma once
#include "imgui.h"
#include "config.h"
#include <iostream>
#include "logger.h"
#include "builder/embedded_assets.h"

namespace Theme {

  // ================================
  // CENTRALIZED COLOR CONSTANTS
  // ================================
  // All colors used throughout the application should be defined here
  // Use numbered suffixes for similar colors (LIGHT_BLUE_1, LIGHT_BLUE_2, etc.)

  // === UTILITY FUNCTIONS ===
  // Convert ImVec4 (0.0-1.0 range) to ImU32 (0-255 range) for immediate drawing
  inline ImU32 ToImU32(const ImVec4& color) {
    return IM_COL32((int) (color.x * 255), (int) (color.y * 255), (int) (color.z * 255), (int) (color.w * 255));
  }

  // === TEXT COLORS ===
  constexpr ImVec4 TEXT_DARK = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);          // Primary dark text
  constexpr ImVec4 TEXT_SECONDARY = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);     // Disabled/secondary text (light gray)
  constexpr ImVec4 TEXT_DISABLED_DARK = ImVec4(0.50f, 0.50f, 0.50f, 1.00f); // Darker disabled text
  constexpr ImVec4 TEXT_HEADER = ImVec4(0.20f, 0.70f, 0.90f, 1.00f);        // Light blue for headers
  constexpr ImVec4 TEXT_LABEL = ImVec4(0.20f, 0.20f, 0.80f, 1.00f);         // Blue for labels
  constexpr ImVec4 TEXT_WARNING = ImVec4(0.90f, 0.70f, 0.20f, 1.00f);       // Yellow/orange for warnings

  // === BACKGROUND COLORS ===
  constexpr ImVec4 BACKGROUND_LIGHT_BLUE_1 = ImVec4(0.95f, 0.97f, 1.00f, 1.00f); // Main background
  constexpr ImVec4 BACKGROUND_LIGHT_GRAY = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);   // Light gray for 3D preview
  constexpr ImVec4 BACKGROUND_LIGHT_BLUE_2 = ImVec4(0.90f, 0.95f, 1.00f, 1.00f); // Slightly darker background
  constexpr ImVec4 BACKGROUND_WHITE = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);        // Pure white background

  // === FRAME/PANEL COLORS ===
  constexpr ImVec4 FRAME_LIGHT_BLUE_1 = ImVec4(0.85f, 0.90f, 0.95f, 1.00f); // Light frame
  constexpr ImVec4 FRAME_LIGHT_BLUE_2 = ImVec4(0.75f, 0.85f, 0.95f, 1.00f); // Medium frame
  constexpr ImVec4 FRAME_LIGHT_BLUE_3 = ImVec4(0.85f, 0.90f, 0.95f, 0.75f); // Light frame with transparency
  constexpr ImVec4 FRAME_LIGHT_BLUE_4 = ImVec4(0.85f, 0.90f, 0.95f, 0.80f); // Light frame with slight transparency
  constexpr ImVec4 FRAME_LIGHT_BLUE_5 = ImVec4(0.80f, 0.85f, 0.90f, 0.90f); // Medium-light frame
  constexpr ImVec4 FRAME_LIGHT_BLUE_6 = ImVec4(0.75f, 0.80f, 0.85f, 1.00f); // Darker frame

  // === BORDER/SEPARATOR COLORS ===
  constexpr ImVec4 BORDER_LIGHT_BLUE_1 = ImVec4(0.80f, 0.85f, 0.90f, 0.50f); // Standard border
  constexpr ImVec4 BORDER_LIGHT_BLUE_2 = ImVec4(0.70f, 0.80f, 0.90f, 0.75f); // Hovered border
  constexpr ImVec4 BORDER_GRAY = ImVec4(0.588f, 0.588f, 0.588f, 1.00f);     // Gray border (was IM_COL32(150,150,150,255))
  // === TRANSPARENCY COLORS ===
  constexpr ImVec4 COLOR_TRANSPARENT = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);      // Fully transparent / Invisible border
  constexpr ImVec4 COLOR_SEMI_TRANSPARENT = ImVec4(0.00f, 0.00f, 0.00f, 0.30f); // Semi-transparent black

  // === ImU32 COLORS (for immediate drawing) ===
  constexpr ImU32 COLOR_WHITE_U32 = IM_COL32(255, 255, 255, 255);        // Pure white
  constexpr ImU32 COLOR_TRANSPARENT_U32 = IM_COL32(0, 0, 0, 0);          // Fully transparent
  constexpr ImU32 COLOR_BORDER_GRAY_U32 = IM_COL32(150, 150, 150, 255);  // Gray border

  // === ACCENT COLORS (Blue Theme) ===
  constexpr ImVec4 ACCENT_BLUE_1 = ImVec4(0.15f, 0.55f, 0.75f, 1.00f); // Primary accent
  constexpr ImVec4 ACCENT_BLUE_2 = ImVec4(0.10f, 0.45f, 0.65f, 1.00f); // Darker accent
  constexpr ImVec4 ACCENT_BLUE_3 = ImVec4(0.05f, 0.35f, 0.55f, 1.00f); // Darkest accent

  // === ACCENT COLORS WITH TRANSPARENCY ===
  constexpr ImVec4 ACCENT_BLUE_1_ALPHA_80 = ImVec4(0.15f, 0.55f, 0.75f, 0.80f); // Primary accent 80% opacity
  constexpr ImVec4 ACCENT_BLUE_1_ALPHA_95 = ImVec4(0.15f, 0.55f, 0.75f, 0.95f); // Primary accent 95% opacity
  constexpr ImVec4 ACCENT_BLUE_1_ALPHA_35 = ImVec4(0.15f, 0.55f, 0.75f, 0.35f); // Primary accent 35% opacity
  constexpr ImVec4 ACCENT_BLUE_1_ALPHA_30 = ImVec4(0.15f, 0.55f, 0.75f, 0.30f); // Primary accent 30% opacity
  constexpr ImVec4 ACCENT_BLUE_1_ALPHA_20 = ImVec4(0.15f, 0.55f, 0.75f, 0.20f); // Primary accent 20% opacity
  constexpr ImVec4 ACCENT_BLUE_2_ALPHA_60 = ImVec4(0.10f, 0.45f, 0.65f, 0.60f); // Darker accent 60% opacity
  constexpr ImVec4 ACCENT_BLUE_2_ALPHA_95 = ImVec4(0.10f, 0.45f, 0.65f, 0.95f); // Darker accent 95% opacity
  constexpr ImVec4 ACCENT_BLUE_3_ALPHA_90 = ImVec4(0.05f, 0.35f, 0.55f, 0.90f); // Darkest accent 90% opacity

  // === 3D Preview Colors ===
  constexpr ImVec4 SKELETON_BONE = ImVec4(1.0f, 0.58f, 0.12f, 1.0f); // Warm orange bones

  // === LIVELY TOGGLE COLORS ===
  constexpr ImVec4 TOGGLE_OFF_BG = ImVec4(0.92f, 0.95f, 0.98f, 1.00f);        // Light off state background
  constexpr ImVec4 TOGGLE_OFF_BORDER = ImVec4(0.80f, 0.85f, 0.90f, 1.00f);    // Light off state border
  constexpr ImVec4 TOGGLE_OFF_TEXT = ImVec4(0.30f, 0.35f, 0.40f, 1.00f);      // Darker off state text
  constexpr ImVec4 TOGGLE_ON_BG = ImVec4(0.50f, 0.20f, 0.70f, 1.00f);         // Deep purple on state background
  constexpr ImVec4 TOGGLE_ON_BORDER = ImVec4(0.40f, 0.15f, 0.60f, 1.00f);     // Darker purple on state border
  constexpr ImVec4 TOGGLE_ON_TEXT = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);       // White on state text
  constexpr ImVec4 TOGGLE_HOVER_BG = ImVec4(0.88f, 0.92f, 0.96f, 1.00f);      // Hover background for off state

  // === SCROLLBAR COLORS ===
  constexpr ImVec4 SCROLLBAR_BG = ImVec4(0.90f, 0.95f, 1.00f, 0.60f);     // Scrollbar background
  constexpr ImVec4 SCROLLBAR_GRAB_1 = ImVec4(0.70f, 0.80f, 0.90f, 1.00f); // Scrollbar grab
  constexpr ImVec4 SCROLLBAR_GRAB_2 = ImVec4(0.60f, 0.75f, 0.85f, 1.00f); // Scrollbar grab hovered
  constexpr ImVec4 SCROLLBAR_GRAB_3 = ImVec4(0.50f, 0.65f, 0.80f, 1.00f); // Scrollbar grab active

  // === TAB COLORS ===
  constexpr ImVec4 TAB_UNFOCUSED_1 = ImVec4(0.90f, 0.95f, 1.00f, 0.80f); // Unfocused tab
  constexpr ImVec4 TAB_UNFOCUSED_2 = ImVec4(0.85f, 0.90f, 0.95f, 0.90f); // Unfocused active tab

  // === SELECTION RECTANGLE COLORS (Area/Rubber-band Selection) ===
  constexpr ImU32 SELECTION_FILL_U32 = IM_COL32(100, 150, 255, 50);   // Light blue fill with transparency
  constexpr ImU32 SELECTION_BORDER_U32 = IM_COL32(100, 150, 255, 200); // Darker blue border

  // Note: For OpenGL calls, access ImVec4 components directly:
  // Theme::BACKGROUND_LIGHT_BLUE_1.x, .y, .z, .w

  // Light and fun theme for game asset management
  inline void setup_light_fun_theme() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Apply centralized color scheme
    ImVec4* colors = style.Colors;

    // Text colors
    colors[ImGuiCol_Text] = TEXT_DARK;
    colors[ImGuiCol_TextDisabled] = TEXT_SECONDARY;
    colors[ImGuiCol_InputTextCursor] = TEXT_DARK;

    // Background colors
    colors[ImGuiCol_WindowBg] = BACKGROUND_LIGHT_BLUE_1;
    colors[ImGuiCol_ChildBg] = BACKGROUND_LIGHT_BLUE_1;
    colors[ImGuiCol_PopupBg] = BACKGROUND_LIGHT_BLUE_1;
    colors[ImGuiCol_MenuBarBg] = BACKGROUND_LIGHT_BLUE_2;
    colors[ImGuiCol_FrameBg] = BACKGROUND_WHITE;
    colors[ImGuiCol_FrameBgHovered] = BACKGROUND_LIGHT_BLUE_2;
    colors[ImGuiCol_FrameBgActive] = FRAME_LIGHT_BLUE_1;

    // Borders and separators
    colors[ImGuiCol_Border] = BORDER_LIGHT_BLUE_1;
    colors[ImGuiCol_BorderShadow] = COLOR_TRANSPARENT;
    colors[ImGuiCol_Separator] = BORDER_LIGHT_BLUE_1;
    colors[ImGuiCol_SeparatorHovered] = BORDER_LIGHT_BLUE_2;
    colors[ImGuiCol_SeparatorActive] = ACCENT_BLUE_1_ALPHA_95;

    // Title bars
    colors[ImGuiCol_TitleBg] = FRAME_LIGHT_BLUE_1;
    colors[ImGuiCol_TitleBgActive] = FRAME_LIGHT_BLUE_2;
    colors[ImGuiCol_TitleBgCollapsed] = FRAME_LIGHT_BLUE_3;

    // Scrollbars
    colors[ImGuiCol_ScrollbarBg] = SCROLLBAR_BG;
    colors[ImGuiCol_ScrollbarGrab] = SCROLLBAR_GRAB_1;
    colors[ImGuiCol_ScrollbarGrabHovered] = SCROLLBAR_GRAB_2;
    colors[ImGuiCol_ScrollbarGrabActive] = SCROLLBAR_GRAB_3;

    // Interactive elements
    colors[ImGuiCol_CheckMark] = ACCENT_BLUE_1;
    colors[ImGuiCol_SliderGrab] = ACCENT_BLUE_1;
    colors[ImGuiCol_SliderGrabActive] = ACCENT_BLUE_2;

    // Buttons
    colors[ImGuiCol_Button] = ACCENT_BLUE_1_ALPHA_80;
    colors[ImGuiCol_ButtonHovered] = ACCENT_BLUE_2_ALPHA_95;
    colors[ImGuiCol_ButtonActive] = ACCENT_BLUE_3;

    // Headers
    colors[ImGuiCol_Header] = FRAME_LIGHT_BLUE_4;
    colors[ImGuiCol_HeaderHovered] = FRAME_LIGHT_BLUE_5;
    colors[ImGuiCol_HeaderActive] = FRAME_LIGHT_BLUE_6;

    // Resize grips
    colors[ImGuiCol_ResizeGrip] = ACCENT_BLUE_1_ALPHA_30;
    colors[ImGuiCol_ResizeGripHovered] = ACCENT_BLUE_2_ALPHA_60;
    colors[ImGuiCol_ResizeGripActive] = ACCENT_BLUE_3_ALPHA_90;

    // Tabs
    colors[ImGuiCol_Tab] = FRAME_LIGHT_BLUE_4;
    colors[ImGuiCol_TabHovered] = FRAME_LIGHT_BLUE_5;
    colors[ImGuiCol_TabActive] = FRAME_LIGHT_BLUE_6;
    colors[ImGuiCol_TabUnfocused] = TAB_UNFOCUSED_1;
    colors[ImGuiCol_TabUnfocusedActive] = TAB_UNFOCUSED_2;

    // Plot colors
    colors[ImGuiCol_PlotLines] = ACCENT_BLUE_1;
    colors[ImGuiCol_PlotLinesHovered] = ACCENT_BLUE_2;
    colors[ImGuiCol_PlotHistogram] = ACCENT_BLUE_1;
    colors[ImGuiCol_PlotHistogramHovered] = ACCENT_BLUE_2;

    // Selection and highlighting
    colors[ImGuiCol_TextSelectedBg] = ACCENT_BLUE_1_ALPHA_35;
    colors[ImGuiCol_DragDropTarget] = ACCENT_BLUE_1_ALPHA_95;
    colors[ImGuiCol_NavHighlight] = ACCENT_BLUE_1;
    colors[ImGuiCol_NavWindowingHighlight] = ACCENT_BLUE_1;
    colors[ImGuiCol_NavWindowingDimBg] = ACCENT_BLUE_1_ALPHA_20;
    colors[ImGuiCol_ModalWindowDimBg] = ACCENT_BLUE_1;

    // Modern spacing and sizing
    style.WindowPadding = ImVec2(15, 15);
    style.WindowRounding = 8.0f;
    style.FramePadding = ImVec2(8, 6);
    style.FrameRounding = 6.0f;
    style.ItemSpacing = ImVec2(12, 8);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.IndentSpacing = 25.0f;
    style.ScrollbarSize = 16.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabMinSize = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowBorderSize = 1.0f;
  }

  // Font loading function
  inline bool load_roboto_font(ImGuiIO& io) {
    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;  // Embedded data is owned by the binary

    // Use default glyph ranges which include Extended Latin for Unicode characters like × (U+00D7)
    const ImWchar* glyph_ranges = io.Fonts->GetGlyphRangesDefault();

    auto font_asset = embedded_assets::get(Config::FONT_PATH);
    if (!font_asset.has_value()) {
      LOG_ERROR("Embedded font asset not found: {}", Config::FONT_PATH);
      return false;
    }

    ImFont* font = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(font_asset->data),
        static_cast<int>(font_asset->size),
        Config::FONT_SIZE,
        &font_config,
        glyph_ranges);

    if (!font) {
      LOG_ERROR("Failed to load Roboto font from embedded asset: {}", Config::FONT_PATH);
      return false;
    }

    LOG_INFO("Roboto font loaded successfully from embedded asset with Unicode support!");
    return true;
  }

} // namespace Theme
