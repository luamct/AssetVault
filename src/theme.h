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

  inline ImFont* g_primary_font = nullptr;
  inline ImFont* g_primary_font_large = nullptr;
  inline ImFont* g_tag_font = nullptr;

  inline ImFont* get_primary_font() {
    return g_primary_font;
  }

  inline ImFont* get_primary_font_large() {
    return g_primary_font_large ? g_primary_font_large : g_primary_font;
  }

  inline ImFont* get_tag_font() {
    return g_tag_font;
  }

  // === TEXT COLORS ===
  constexpr ImVec4 TEXT_DARK = ImVec4(0.29f, 0.21f, 0.14f, 1.00f);          // Primary text (deep brown)
  constexpr ImVec4 TEXT_SECONDARY = ImVec4(0.49f, 0.41f, 0.33f, 1.00f);     // Disabled/secondary text (warm gray)
  constexpr ImVec4 TEXT_DISABLED_DARK = ImVec4(0.63f, 0.53f, 0.45f, 1.00f); // Dimmer disabled text
  constexpr ImVec4 TEXT_HEADER = ImVec4(0.63f, 0.35f, 0.17f, 1.00f);        // Accent headers
  constexpr ImVec4 TEXT_LABEL = ImVec4(0.55f, 0.31f, 0.15f, 1.00f);         // Secondary label color
  constexpr ImVec4 TEXT_WARNING = ImVec4(0.75f, 0.40f, 0.17f, 1.00f);       // Warm warning tone

  // === BACKGROUND COLORS ===
  constexpr ImVec4 BACKGROUND_LIGHT_BLUE_1 = ImVec4(0.992f, 0.980f, 0.949f, 1.00f); // Main parchment background (#FDFaf2)
  constexpr ImVec4 BACKGROUND_LIGHT_GRAY = ImVec4(0.961f, 0.918f, 0.843f, 1.00f);   // Soft tan for preview panels
  constexpr ImVec4 BACKGROUND_LIGHT_BLUE_2 = ImVec4(0.969f, 0.945f, 0.894f, 1.00f); // Secondary parchment background
  constexpr ImVec4 BACKGROUND_WHITE = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);        // Pure white background

  // === FRAME/PANEL COLORS ===
  constexpr ImVec4 FRAME_LIGHT_BLUE_1 = ImVec4(0.957f, 0.878f, 0.784f, 1.00f); // Light panel fill
  constexpr ImVec4 FRAME_LIGHT_BLUE_2 = ImVec4(0.910f, 0.804f, 0.663f, 1.00f); // Medium panel fill
  constexpr ImVec4 FRAME_LIGHT_BLUE_3 = ImVec4(0.957f, 0.878f, 0.784f, 0.75f); // Light with transparency
  constexpr ImVec4 FRAME_LIGHT_BLUE_4 = ImVec4(0.933f, 0.839f, 0.718f, 0.82f); // Hovered header
  constexpr ImVec4 FRAME_LIGHT_BLUE_5 = ImVec4(0.890f, 0.780f, 0.639f, 0.90f); // Active header hover
  constexpr ImVec4 FRAME_LIGHT_BLUE_6 = ImVec4(0.835f, 0.706f, 0.549f, 1.00f); // Active header

  // === BORDER/SEPARATOR COLORS ===
  constexpr ImVec4 BORDER_LIGHT_BLUE_1 = ImVec4(0.827f, 0.706f, 0.541f, 0.65f); // Standard border
  constexpr ImVec4 BORDER_LIGHT_BLUE_2 = ImVec4(0.761f, 0.624f, 0.463f, 0.85f); // Hovered border
  constexpr ImVec4 BORDER_GRAY = ImVec4(0.616f, 0.478f, 0.322f, 1.00f);          // Neutral separator/border
  // === TRANSPARENCY COLORS ===
  constexpr ImVec4 COLOR_TRANSPARENT = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);      // Fully transparent / Invisible border
  constexpr ImVec4 COLOR_SEMI_TRANSPARENT = ImVec4(0.00f, 0.00f, 0.00f, 0.15f); // Semi-transparent black
  constexpr ImVec4 IMAGE_HOVER_OVERLAY = ImVec4(0.00f, 0.00f, 0.00f, 0.30f);    // Dark tint for hover states

  // === ImU32 COLORS (for immediate drawing) ===
  constexpr ImU32 COLOR_WHITE_U32 = IM_COL32(255, 255, 255, 255);        // Pure white
  constexpr ImU32 COLOR_TRANSPARENT_U32 = IM_COL32(0, 0, 0, 0);          // Fully transparent
  constexpr ImU32 COLOR_BORDER_GRAY_U32 = IM_COL32(157, 122, 82, 255);   // Warm gray border

  // === ACCENT COLORS (Earthy Theme) ===
  constexpr ImVec4 ACCENT_BLUE_1 = ImVec4(0.725f, 0.459f, 0.227f, 1.00f); // Primary accent (burnt orange)
  constexpr ImVec4 ACCENT_BLUE_2 = ImVec4(0.604f, 0.361f, 0.153f, 1.00f); // Darker accent
  constexpr ImVec4 ACCENT_BLUE_3 = ImVec4(0.494f, 0.278f, 0.110f, 1.00f); // Darkest accent

  // === TAG COLORS ===
  constexpr ImVec4 TAG_TYPE_2D = ImVec4(0.149f, 0.451f, 0.851f, 1.00f);      // Lively blue for 2D assets
  constexpr ImVec4 TAG_TYPE_3D = ImVec4(0.757f, 0.286f, 0.325f, 1.00f);      // Vibrant red for 3D assets
  constexpr ImVec4 TAG_TYPE_AUDIO = ImVec4(0.180f, 0.600f, 0.404f, 1.00f);   // Fresh green for audio
  constexpr ImVec4 TAG_TYPE_FONT = ImVec4(0.482f, 0.318f, 0.667f, 1.00f);    // Purple accent for fonts
  constexpr ImVec4 TAG_TYPE_SHADER = ImVec4(0.129f, 0.533f, 0.600f, 1.00f);  // Teal for shaders
  constexpr ImVec4 TAG_TYPE_DOCUMENT = ImVec4(0.620f, 0.439f, 0.188f, 1.00f); // Warm brown for documents
  constexpr ImVec4 TAG_TYPE_ARCHIVE = ImVec4(0.702f, 0.396f, 0.196f, 1.00f); // Orange for archives
  constexpr ImVec4 TAG_TYPE_DIRECTORY = ImVec4(0.192f, 0.451f, 0.741f, 1.00f); // Cool blue for folders
  constexpr ImVec4 TAG_TYPE_AUXILIARY = ImVec4(0.447f, 0.447f, 0.447f, 1.00f); // Neutral gray for aux files
  constexpr ImVec4 TAG_TYPE_UNKNOWN = ImVec4(0.424f, 0.392f, 0.357f, 1.00f);  // Default neutral tone
  constexpr ImVec4 TAG_TYPE_TEXT = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);        // White text on colored tags
  constexpr ImVec4 TAG_EXTENSION_FILL = ImVec4(0.176f, 0.176f, 0.165f, 1.00f); // Deep neutral for extensions
  constexpr ImVec4 TAG_EXTENSION_TEXT = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);    // White text on extensions
  constexpr ImVec4 TAG_PILL_BORDER = ImVec4(0.408f, 0.133f, 0.184f, 1.00f);   // Rich border for tag pills

  // === ACCENT COLORS WITH TRANSPARENCY ===
  constexpr ImVec4 ACCENT_BLUE_1_ALPHA_80 = ImVec4(0.725f, 0.459f, 0.227f, 0.80f); // Primary accent 80% opacity
  constexpr ImVec4 ACCENT_BLUE_1_ALPHA_95 = ImVec4(0.725f, 0.459f, 0.227f, 0.95f); // Primary accent 95% opacity
  constexpr ImVec4 ACCENT_BLUE_1_ALPHA_35 = ImVec4(0.725f, 0.459f, 0.227f, 0.35f); // Primary accent 35% opacity
  constexpr ImVec4 ACCENT_BLUE_1_ALPHA_30 = ImVec4(0.725f, 0.459f, 0.227f, 0.30f); // Primary accent 30% opacity
  constexpr ImVec4 ACCENT_BLUE_1_ALPHA_20 = ImVec4(0.725f, 0.459f, 0.227f, 0.20f); // Primary accent 20% opacity
  constexpr ImVec4 ACCENT_BLUE_2_ALPHA_60 = ImVec4(0.604f, 0.361f, 0.153f, 0.60f); // Darker accent 60% opacity
  constexpr ImVec4 ACCENT_BLUE_2_ALPHA_95 = ImVec4(0.604f, 0.361f, 0.153f, 0.95f); // Darker accent 95% opacity
  constexpr ImVec4 ACCENT_BLUE_3_ALPHA_90 = ImVec4(0.494f, 0.278f, 0.110f, 0.90f); // Darkest accent 90% opacity

  // === 3D Preview Colors ===
  constexpr ImVec4 SKELETON_BONE = ImVec4(1.0f, 0.58f, 0.12f, 1.0f); // Warm orange bones

  // === LIVELY TOGGLE COLORS ===
  constexpr ImVec4 TOGGLE_OFF_BG = ImVec4(0.965f, 0.922f, 0.839f, 1.00f);        // Light off state background
  constexpr ImVec4 TOGGLE_OFF_BORDER = ImVec4(0.863f, 0.769f, 0.643f, 1.00f);    // Light off state border
  constexpr ImVec4 TOGGLE_OFF_TEXT = ImVec4(0.439f, 0.314f, 0.200f, 1.00f);      // Darker off state text
  constexpr ImVec4 TOGGLE_ON_BG = ImVec4(0.725f, 0.459f, 0.227f, 1.00f);         // Rich terracotta on state background
  constexpr ImVec4 TOGGLE_ON_BORDER = ImVec4(0.604f, 0.361f, 0.153f, 1.00f);     // Darker terracotta border
  constexpr ImVec4 TOGGLE_ON_TEXT = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);       // White on state text
  constexpr ImVec4 TOGGLE_HOVER_BG = ImVec4(0.949f, 0.878f, 0.765f, 1.00f);      // Hover background for off state

  // === SCROLLBAR COLORS ===
  constexpr ImVec4 SCROLLBAR_BG = ImVec4(0.953f, 0.890f, 0.773f, 0.60f);     // Scrollbar background
  constexpr ImVec4 SCROLLBAR_GRAB_1 = ImVec4(0.867f, 0.718f, 0.541f, 1.00f); // Scrollbar grab
  constexpr ImVec4 SCROLLBAR_GRAB_2 = ImVec4(0.792f, 0.624f, 0.431f, 1.00f); // Scrollbar grab hovered
  constexpr ImVec4 SCROLLBAR_GRAB_3 = ImVec4(0.722f, 0.518f, 0.322f, 1.00f); // Scrollbar grab active

  // === TAB COLORS ===
  constexpr ImVec4 TAB_UNFOCUSED_1 = ImVec4(0.945f, 0.878f, 0.757f, 0.85f); // Unfocused tab
  constexpr ImVec4 TAB_UNFOCUSED_2 = ImVec4(0.925f, 0.839f, 0.714f, 0.95f); // Unfocused active tab

  // === SELECTION RECTANGLE COLORS (Area/Rubber-band Selection) ===
  constexpr ImU32 SELECTION_FILL_U32 = IM_COL32(199, 130, 76, 70);     // Warm clay fill with transparency
  constexpr ImU32 SELECTION_BORDER_U32 = IM_COL32(199, 130, 76, 200);  // Darker clay border

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
inline constexpr const char* PRIMARY_FONT_PATH = "external/fonts/Inter-Regular.ttf";
inline constexpr float PRIMARY_FONT_SIZE = 18.0f;
inline constexpr const char* TAG_FONT_PATH = "external/fonts/Inter_18pt-SemiBold.ttf";
inline constexpr float TAG_FONT_SIZE = 18.0f;

inline bool load_fonts(ImGuiIO& io) {
    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;  // Embedded data is owned by the binary

    // Use default glyph ranges which include Extended Latin for Unicode characters like × (U+00D7)
    const ImWchar* glyph_ranges = io.Fonts->GetGlyphRangesDefault();

    auto primary_asset = embedded_assets::get(PRIMARY_FONT_PATH);
    if (!primary_asset.has_value()) {
      LOG_ERROR("Embedded font asset not found: {}", PRIMARY_FONT_PATH);
      return false;
    }

    g_primary_font = io.Fonts->AddFontFromMemoryTTF(
      const_cast<unsigned char*>(primary_asset->data),
      static_cast<int>(primary_asset->size),
      PRIMARY_FONT_SIZE,
      &font_config,
      glyph_ranges);

    if (!g_primary_font) {
      LOG_ERROR("Failed to load primary font from embedded asset: {}", PRIMARY_FONT_PATH);
      return false;
    }

    ImFontConfig large_config = font_config;
    g_primary_font_large = io.Fonts->AddFontFromMemoryTTF(
      const_cast<unsigned char*>(primary_asset->data),
      static_cast<int>(primary_asset->size),
      PRIMARY_FONT_SIZE + 2.0f,
      &large_config,
      glyph_ranges);

    if (!g_primary_font_large) {
      g_primary_font_large = g_primary_font;
      LOG_WARN("Failed to load enlarged primary font. Falling back to default size.");
    }

    auto tag_asset = embedded_assets::get(TAG_FONT_PATH);
    if (!tag_asset.has_value()) {
      LOG_ERROR("Embedded tag font asset not found: {}", TAG_FONT_PATH);
    }
    else {
      ImFontConfig tag_config = font_config;
      tag_config.OversampleH = 2;
      tag_config.OversampleV = 2;
      tag_config.RasterizerMultiply = 0.9f;

      g_tag_font = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(tag_asset->data),
        static_cast<int>(tag_asset->size),
        TAG_FONT_SIZE,
        &tag_config,
        glyph_ranges);

      if (!g_tag_font) {
        LOG_ERROR("Failed to load tag font from embedded asset: {}", TAG_FONT_PATH);
      }
    }

    if (!g_tag_font) {
      g_tag_font = g_primary_font;
      LOG_WARN("Tag font unavailable. Falling back to primary font for pills.");
    }

    LOG_INFO("Fonts loaded successfully (primary={}, primary_large={}, tag={})",
      static_cast<void*>(g_primary_font), static_cast<void*>(g_primary_font_large), static_cast<void*>(g_tag_font));
    return g_primary_font != nullptr;
  }

} // namespace Theme
