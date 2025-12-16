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

  // === CORE TEXT COLORS (actively used) ===
  constexpr ImVec4 TEXT_DARK = ImVec4(0.98f, 0.98f, 0.99f, 1.00f);          // Primary text (bright white)
  constexpr ImVec4 TEXT_LIGHTER = ImVec4(0.8f, 0.8f, 0.8f, 1.00f);          // Secondary text (a bit darker)
  constexpr ImVec4 TEXT_SECONDARY = ImVec4(0.63f, 0.68f, 0.75f, 1.00f);     // Disabled/secondary text
  constexpr ImVec4 TEXT_DISABLED_DARK = ImVec4(0.42f, 0.46f, 0.52f, 1.00f); // Muted UI chrome
  constexpr ImVec4 TEXT_LABEL = ImVec4(0.58f, 0.75f, 0.92f, 1.00f);         // Small labels and pills
  constexpr ImVec4 TEXT_WARNING = ImVec4(0.98f, 0.65f, 0.32f, 1.00f);       // Inline warnings

  // === CORE SURFACE COLORS ===
  constexpr ImVec4 BACKGROUND_LIGHT_BLUE_1 = ImVec4(0.125f, 0.125f, 0.125f, 1.00f);     // Main background #202020
  constexpr ImVec4 BACKGROUND_LIGHT_GRAY = ImVec4(0.212f, 0.239f, 0.290f, 1.00f);      // Lighter panels #363D4A
  constexpr ImVec4 BACKGROUND_WHITE = ImVec4(0.263f, 0.305f, 0.373f, 1.00f);           // Raised cards #43506a
  constexpr ImVec4 FRAME_LIGHT_BLUE_3 = ImVec4(0.212f, 0.239f, 0.290f, 0.95f);         // Subtle frame fill
  constexpr ImVec4 FRAME_LIGHT_BLUE_4 = ImVec4(0.263f, 0.305f, 0.373f, 0.95f);         // Header default / hovered widget fill
  constexpr ImVec4 FRAME_LIGHT_BLUE_5 = ImVec4(0.318f, 0.365f, 0.443f, 1.00f);         // Header hover / tiles
  constexpr ImVec4 FRAME_LIGHT_BLUE_6 = ImVec4(0.408f, 0.455f, 0.533f, 1.00f);         // Header active highlight

  // === SPECIAL SURFACE COLORS ===
  constexpr ImVec4 SEARCH_BOX_BG = ImVec4(0.922f, 0.922f, 0.922f, 1.00f);              // #EBEBEB search field
  constexpr ImVec4 SEARCH_BOX_BG_HOVERED = ImVec4(0.980f, 0.980f, 0.980f, 1.00f);      // #FAFAFA hover
  constexpr ImVec4 SEARCH_BOX_BG_ACTIVE = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);       // #FFFFFF active
  constexpr ImVec4 SEARCH_BOX_TEXT = ImVec4(0.078f, 0.098f, 0.125f, 1.00f);            // Dark text for light search box
  constexpr ImVec4 SEARCH_BOX_CURSOR = ImVec4(0.078f, 0.098f, 0.125f, 1.00f);          // Matching caret color
  constexpr ImVec4 VIEWPORT_CANVAS = ImVec4(0.450f, 0.450f, 0.450f, 1.00f);            // Shared grid/viewport canvas (lighter slate #5C6678)

  // === BORDERS & OVERLAYS ===
  constexpr ImVec4 BORDER_LIGHT_BLUE_1 = ImVec4(0.247f, 0.278f, 0.329f, 1.00f); // Soft steel borders #3F4754
  constexpr ImVec4 COLOR_TRANSPARENT = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);      // Invisible border
  constexpr ImVec4 COLOR_SEMI_TRANSPARENT = ImVec4(0.145f, 0.169f, 0.204f, 0.35f); // Hover overlay
  constexpr ImVec4 IMAGE_HOVER_OVERLAY = ImVec4(0.212f, 0.239f, 0.290f, 0.40f);    // Dark tint for thumbnails

  // === ACCENT COLORS & TINTS ===
  constexpr ImVec4 ACCENT_BLUE_1 = ImVec4(0.345f, 0.392f, 0.467f, 1.00f); // Elevated panels (#586475)
  constexpr ImVec4 ACCENT_BLUE_2 = ImVec4(0.408f, 0.455f, 0.533f, 1.00f); // Hovered accent state (#687482)
  constexpr ImVec4 ACCENT_BLUE_1_ALPHA_80 = ImVec4(0.345f, 0.392f, 0.467f, 0.80f); // Button fill
  constexpr ImVec4 ACCENT_BLUE_1_ALPHA_95 = ImVec4(0.345f, 0.392f, 0.467f, 0.95f); // Solid selection border
  constexpr ImVec4 ACCENT_BLUE_1_ALPHA_35 = ImVec4(0.345f, 0.392f, 0.467f, 0.35f); // Selection fill / scrollbar grab

  // === SCROLLBAR COLORS ===
  constexpr ImVec4 SCROLLBAR_BG = BACKGROUND_LIGHT_BLUE_1;                        // Track background
  constexpr ImVec4 SCROLLBAR_GRAB = ImVec4(0.145f, 0.169f, 0.204f, 1.00f);        // Idle thumb (soft navy)
  constexpr ImVec4 SCROLLBAR_GRAB_HOVERED = ImVec4(0.212f, 0.239f, 0.290f, 1.00f); // Hovered thumb (lighter slate)

  // TODO: Cleanup unused values
  // === TAG COLORS ===
  constexpr ImVec4 TAG_TYPE_2D = ImVec4(0.204f, 0.490f, 0.965f, 1.00f);      // #347DF6 for 2D assets
  constexpr ImVec4 TAG_TYPE_3D = ImVec4(0.918f, 0.239f, 0.180f, 1.00f);      // #EA3D2E for 3D assets
  constexpr ImVec4 TAG_TYPE_AUDIO = ImVec4(0.953f, 0.655f, 0.180f, 1.00f);   // #F3A72E for audio
  constexpr ImVec4 TAG_TYPE_FONT = ImVec4(0.027f, 0.588f, 0.596f, 1.00f);    // #079698 for fonts
  constexpr ImVec4 TAG_TYPE_SHADER = ImVec4(0.129f, 0.562f, 0.773f, 1.00f);  // Bright teal for shaders
  constexpr ImVec4 TAG_TYPE_DOCUMENT = ImVec4(0.353f, 0.408f, 0.533f, 1.00f); // Muted steel for documents
  constexpr ImVec4 TAG_TYPE_ARCHIVE = ImVec4(0.294f, 0.380f, 0.522f, 1.00f); // Indigo gray for archives
  constexpr ImVec4 TAG_TYPE_DIRECTORY = ImVec4(0.184f, 0.431f, 0.702f, 1.00f); // Cool blue for folders
  constexpr ImVec4 TAG_TYPE_AUXILIARY = ImVec4(0.498f, 0.521f, 0.576f, 1.00f); // Neutral gray for aux files
  constexpr ImVec4 TAG_TYPE_UNKNOWN = ImVec4(0.424f, 0.447f, 0.490f, 1.00f);  // Default neutral tone
  constexpr ImVec4 TAG_TYPE_TEXT = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);        // White text on colored tags
  constexpr ImVec4 TAG_EXTENSION_FILL = ImVec4(0.133f, 0.149f, 0.188f, 1.00f); // Deep navy for extensions
  constexpr ImVec4 TAG_EXTENSION_TEXT = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);    // White text on extensions
  constexpr ImVec4 TAG_PILL_BORDER = BORDER_LIGHT_BLUE_1;                     // Consistent border tone for tag pills

  // === TEXT EMPHASIS COLORS ===
  constexpr ImVec4 TEXT_PATH = ImVec4(0.83f, 0.87f, 0.94f, 1.00f); // Airy path readouts

  // === LIVELY TOGGLE COLORS ===
  constexpr ImVec4 TOGGLE_OFF_BG = ImVec4(0.145f, 0.169f, 0.204f, 1.00f);        // Off state background
  constexpr ImVec4 TOGGLE_OFF_BORDER = BORDER_LIGHT_BLUE_1;    // Off state border
  constexpr ImVec4 TOGGLE_OFF_TEXT = TEXT_SECONDARY;           // Darker off state text
  constexpr ImVec4 TOGGLE_ON_BG = ACCENT_BLUE_2;               // On state background
  constexpr ImVec4 TOGGLE_ON_BORDER = BORDER_LIGHT_BLUE_1;     // Neutral contrast border
  constexpr ImVec4 TOGGLE_ON_TEXT = TEXT_DARK;                 // White on state text
  constexpr ImVec4 TOGGLE_HOVER_BG = ImVec4(0.212f, 0.239f, 0.290f, 1.00f);      // Hover background for off state
  constexpr ImVec4 SEPARATOR_GRAY = ImVec4(0.2745f, 0.2745f, 0.2745f, 1.00f);     // #464646

  // === 3D PREVIEW COLORS ===
  constexpr ImVec4 SKELETON_BONE = ImVec4(1.0f, 0.58f, 0.12f, 1.0f); // Warm yellow-orange bones

  // === UTILITY COLORS (ImU32) ===
  constexpr ImU32 COLOR_WHITE_U32 = IM_COL32(255, 255, 255, 255);        // Pure white

  // === Archived palette entries (kept for experimentation) ===
  // constexpr ImVec4 TEXT_HEADER = ImVec4(0.63f, 0.35f, 0.17f, 1.00f);
  // constexpr ImVec4 BACKGROUND_LIGHT_BLUE_2 = ImVec4(0.969f, 0.945f, 0.894f, 1.00f);
  // constexpr ImVec4 FRAME_LIGHT_BLUE_1 = ImVec4(0.957f, 0.878f, 0.784f, 1.00f);
  // constexpr ImVec4 FRAME_LIGHT_BLUE_2 = ImVec4(0.910f, 0.804f, 0.663f, 1.00f);
  // constexpr ImVec4 BORDER_LIGHT_BLUE_2 = ImVec4(0.761f, 0.624f, 0.463f, 0.85f);
  // constexpr ImVec4 BORDER_GRAY = ImVec4(0.616f, 0.478f, 0.322f, 1.00f);
  // constexpr ImVec4 ACCENT_BLUE_3 = ImVec4(0.494f, 0.278f, 0.110f, 1.00f);
  // constexpr ImVec4 ACCENT_BLUE_1_ALPHA_30 = ImVec4(0.725f, 0.459f, 0.227f, 0.30f);
  // constexpr ImVec4 ACCENT_BLUE_1_ALPHA_20 = ImVec4(0.725f, 0.459f, 0.227f, 0.20f);
  // constexpr ImVec4 ACCENT_BLUE_2_ALPHA_60 = ImVec4(0.604f, 0.361f, 0.153f, 0.60f);
  // constexpr ImVec4 ACCENT_BLUE_2_ALPHA_95 = ImVec4(0.604f, 0.361f, 0.153f, 0.95f);
  // constexpr ImVec4 ACCENT_BLUE_3_ALPHA_90 = ImVec4(0.494f, 0.278f, 0.110f, 0.90f);
  // constexpr ImVec4 SCROLLBAR_BG = ImVec4(0.953f, 0.890f, 0.773f, 0.60f);
  // constexpr ImVec4 SCROLLBAR_GRAB_1 = ImVec4(0.867f, 0.718f, 0.541f, 1.00f);
  // constexpr ImVec4 SCROLLBAR_GRAB_2 = ImVec4(0.792f, 0.624f, 0.431f, 1.00f);
  // constexpr ImVec4 SCROLLBAR_GRAB_3 = ImVec4(0.722f, 0.518f, 0.322f, 1.00f);
  // constexpr ImVec4 TAB_UNFOCUSED_1 = ImVec4(0.945f, 0.878f, 0.757f, 0.85f);
  // constexpr ImVec4 TAB_UNFOCUSED_2 = ImVec4(0.925f, 0.839f, 0.714f, 0.95f);
  // constexpr ImU32 COLOR_TRANSPARENT_U32 = IM_COL32(0, 0, 0, 0);
  // constexpr ImU32 COLOR_BORDER_GRAY_U32 = IM_COL32(157, 122, 82, 255);
  // constexpr ImU32 SELECTION_FILL_U32 = IM_COL32(199, 130, 76, 70);
  // constexpr ImU32 SELECTION_BORDER_U32 = IM_COL32(199, 130, 76, 200);

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
    colors[ImGuiCol_PopupBg] = BACKGROUND_LIGHT_GRAY;
    colors[ImGuiCol_MenuBarBg] = BACKGROUND_LIGHT_GRAY;
    colors[ImGuiCol_FrameBg] = FRAME_LIGHT_BLUE_3;
    colors[ImGuiCol_FrameBgHovered] = FRAME_LIGHT_BLUE_4;
    colors[ImGuiCol_FrameBgActive] = FRAME_LIGHT_BLUE_5;

    // Borders and separators
    colors[ImGuiCol_Border] = BORDER_LIGHT_BLUE_1;
    colors[ImGuiCol_BorderShadow] = COLOR_TRANSPARENT;
    colors[ImGuiCol_Separator] = BORDER_LIGHT_BLUE_1;
    colors[ImGuiCol_SeparatorHovered] = BORDER_LIGHT_BLUE_1;
    colors[ImGuiCol_SeparatorActive] = ACCENT_BLUE_1_ALPHA_95;

    // Widgets we actually render
    colors[ImGuiCol_ScrollbarBg] = SCROLLBAR_BG;
    colors[ImGuiCol_ScrollbarGrab] = COLOR_TRANSPARENT;
    colors[ImGuiCol_ScrollbarGrabHovered] = COLOR_TRANSPARENT;
    colors[ImGuiCol_ScrollbarGrabActive] = COLOR_TRANSPARENT;
    colors[ImGuiCol_CheckMark] = ACCENT_BLUE_1;
    colors[ImGuiCol_SliderGrab] = ACCENT_BLUE_1;
    colors[ImGuiCol_SliderGrabActive] = ACCENT_BLUE_2;
    colors[ImGuiCol_Button] = ACCENT_BLUE_1_ALPHA_80;
    colors[ImGuiCol_ButtonHovered] = ACCENT_BLUE_1_ALPHA_95;
    colors[ImGuiCol_ButtonActive] = ACCENT_BLUE_2;
    colors[ImGuiCol_Header] = FRAME_LIGHT_BLUE_4;
    colors[ImGuiCol_HeaderHovered] = FRAME_LIGHT_BLUE_5;
    colors[ImGuiCol_HeaderActive] = FRAME_LIGHT_BLUE_6;
    colors[ImGuiCol_TitleBg] = BACKGROUND_LIGHT_GRAY;
    colors[ImGuiCol_TitleBgActive] = FRAME_LIGHT_BLUE_6;
    colors[ImGuiCol_TitleBgCollapsed] = FRAME_LIGHT_BLUE_3;
    colors[ImGuiCol_ResizeGrip] = ACCENT_BLUE_1_ALPHA_35;
    colors[ImGuiCol_ResizeGripHovered] = ACCENT_BLUE_1_ALPHA_80;
    colors[ImGuiCol_ResizeGripActive] = ACCENT_BLUE_2;
    colors[ImGuiCol_TextSelectedBg] = ACCENT_BLUE_1_ALPHA_35;
    colors[ImGuiCol_DragDropTarget] = ACCENT_BLUE_1_ALPHA_95;

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
  inline constexpr const char* PRIMARY_FONT_PATH = "external/fonts/m5x7.ttf";
  inline constexpr float PRIMARY_FONT_SIZE = 18.0f;
  inline constexpr float PRIMARY_FONT_SIZE_LARGE = PRIMARY_FONT_SIZE + 2.0f;
  inline constexpr const char* TAG_FONT_PATH = PRIMARY_FONT_PATH;
  inline constexpr float TAG_FONT_SIZE = 18.0f;

} // namespace Theme
