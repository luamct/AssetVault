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
  // Use numbered suffixes for similar colors (SLATE_1, SLATE_2, etc.)

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
  constexpr ImVec4 BACKGROUND_CHARCOAL = ImVec4(0.125f, 0.125f, 0.125f, 1.00f);     // Main background #202020
  constexpr ImVec4 BACKGROUND_SLATE_1 = ImVec4(0.263f, 0.305f, 0.373f, 1.00f);           // Raised cards #43506a
  constexpr ImVec4 FRAME_SLATE_3 = ImVec4(0.212f, 0.239f, 0.290f, 0.95f);         // Subtle frame fill
  constexpr ImVec4 FRAME_SLATE_4 = ImVec4(0.263f, 0.305f, 0.373f, 0.95f);         // Header default / hovered widget fill
  constexpr ImVec4 FRAME_SLATE_5 = ImVec4(0.318f, 0.365f, 0.443f, 1.00f);         // Header hover / tiles

  // === SPECIAL SURFACE COLORS ===
  constexpr ImVec4 VIEWPORT_CANVAS = ImVec4(0.141f, 0.141f, 0.141f, 1.00f);            // Shared grid/viewport canvas (#242424)

  // === BORDERS & OVERLAYS ===
  constexpr ImVec4 BORDER_SLATE_1 = ImVec4(0.247f, 0.278f, 0.329f, 1.00f); // Soft steel borders #3F4754
  constexpr ImVec4 COLOR_TRANSPARENT = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);      // Invisible border
  constexpr ImVec4 COLOR_SEMI_TRANSPARENT = ImVec4(0.145f, 0.169f, 0.204f, 0.35f); // Hover overlay
  constexpr ImVec4 IMAGE_HOVER_OVERLAY = ImVec4(0.212f, 0.239f, 0.290f, 0.40f);    // Dark tint for thumbnails

  // === ACCENT COLORS & TINTS ===
  constexpr ImVec4 ACCENT_SLATE_1 = ImVec4(0.345f, 0.392f, 0.467f, 1.00f); // Elevated panels (#586475)
  constexpr ImVec4 ACCENT_SLATE_2 = ImVec4(0.408f, 0.455f, 0.533f, 1.00f); // Hovered accent state (#687482)
  constexpr ImVec4 ACCENT_SLATE_1_ALPHA_80 = ImVec4(0.345f, 0.392f, 0.467f, 0.80f); // Button fill
  constexpr ImVec4 ACCENT_SLATE_1_ALPHA_95 = ImVec4(0.345f, 0.392f, 0.467f, 0.95f); // Solid selection border
  constexpr ImVec4 ACCENT_SLATE_1_ALPHA_35 = ImVec4(0.345f, 0.392f, 0.467f, 0.35f); // Selection fill / scrollbar grab

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
  // === TEXT EMPHASIS COLORS ===
  constexpr ImVec4 TEXT_PATH = ImVec4(0.83f, 0.87f, 0.94f, 1.00f); // Airy path readouts

  // === LIVELY TOGGLE COLORS ===
  constexpr ImVec4 TOGGLE_OFF_BORDER = BORDER_SLATE_1;    // Off state border
  constexpr ImVec4 TOGGLE_OFF_TEXT = TEXT_SECONDARY;           // Darker off state text
  constexpr ImVec4 TOGGLE_ON_TEXT = TEXT_DARK;                 // White on state text
  constexpr ImVec4 SEPARATOR_GRAY = ImVec4(0.2745f, 0.2745f, 0.2745f, 1.00f);     // #464646

  // === UTILITY COLORS (ImU32) ===
  constexpr ImU32 COLOR_WHITE_U32 = IM_COL32(255, 255, 255, 255);        // Pure white

  // Note: For OpenGL calls, access ImVec4 components directly:
  // Theme::BACKGROUND_CHARCOAL.x, .y, .z, .w

  // Light and fun theme for game asset management
  inline void setup_light_fun_theme() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Apply centralized color scheme
    ImVec4* colors = style.Colors;

    // Background colors
    colors[ImGuiCol_WindowBg] = BACKGROUND_CHARCOAL;

    // Widgets we actually render
    colors[ImGuiCol_ScrollbarBg] = COLOR_TRANSPARENT;
    colors[ImGuiCol_ScrollbarGrab] = COLOR_TRANSPARENT;
    colors[ImGuiCol_ScrollbarGrabHovered] = COLOR_TRANSPARENT;
    colors[ImGuiCol_ScrollbarGrabActive] = COLOR_TRANSPARENT;

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
  inline constexpr float PRIMARY_FONT_SIZE = 20.0f;
  inline constexpr float PRIMARY_FONT_SIZE_LARGE = PRIMARY_FONT_SIZE + 2.0f;
  inline constexpr const char* TAG_FONT_PATH = PRIMARY_FONT_PATH;
  inline constexpr float TAG_FONT_SIZE = 20.0f;

} // namespace Theme
