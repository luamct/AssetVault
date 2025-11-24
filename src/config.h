#pragma once

#include <unordered_set>
#include <filesystem>
#include <string>
#include <cstdlib>

#include "asset.h"

class AssetDatabase;

class Config {
public:
  // =============================================================================
  // DEBUG & DEVELOPMENT
  // =============================================================================

  static constexpr bool DEBUG_CLEAN_START = true;

  // Font settings
  static constexpr const char* FONT_PATH = "external/fonts/Inter-Regular.ttf";
  static constexpr float FONT_SIZE = 18.0f;
  static constexpr const char* TAG_FONT_PATH = "external/fonts/Inter_18pt-SemiBold.ttf";
  static constexpr float TAG_FONT_SIZE = 18.0f;

  // =============================================================================
  // WINDOW & UI LAYOUT
  // =============================================================================

  static constexpr int WINDOW_WIDTH = 1960;
  static constexpr int WINDOW_HEIGHT = 1080;

  static constexpr float SEARCH_BOX_WIDTH = 375.0f;
  static constexpr float SEARCH_BOX_HEIGHT = 60.0f;
  static constexpr float SEARCH_PANEL_HEIGHT = 120.0f;
  static constexpr float FOLDER_TREE_PANEL_HEIGHT = 220.0f;

  static constexpr float THUMBNAIL_SIZE = 240.0f;
  static constexpr float GRID_SPACING = 15.0f;
  static constexpr float TEXT_MARGIN = 10.0f;
  static constexpr float TEXT_HEIGHT = 20.0f;
  static constexpr float TEXT_MAX_LENGTH = 30.0f;
  static constexpr float ICON_SCALE = 0.5f;

  static constexpr int MODEL_THUMBNAIL_SIZE = 400;
  static constexpr int MAX_TEXTURE_RETRY_ATTEMPTS = 50;

  static constexpr float PREVIEW_3D_ROTATION_SENSITIVITY = 0.167f;
  static constexpr float PREVIEW_3D_ZOOM_FACTOR = 1.1f;
  static constexpr bool PREVIEW_DRAW_DEBUG_AXES_DEFAULT = true;
  static constexpr bool PREVIEW_PLAY_ANIMATIONS = true;

  static constexpr bool SKELETON_HIDE_CTRL_BONES = true;
  static constexpr bool SKELETON_HIDE_IK_BONES = true;
  static constexpr bool SKELETON_HIDE_ROLL_BONES = true;
  static constexpr bool SKELETON_HIDE_ROOT_CHILDREN = true;

  static constexpr float PREVIEW_RIGHT_MARGIN = 40.0f;
  static constexpr float PREVIEW_INTERNAL_PADDING = 30.0f;

  // =============================================================================
  // PERFORMANCE & PROCESSING
  // =============================================================================

  static constexpr size_t EVENT_PROCESSOR_BATCH_SIZE = 100;
  static constexpr size_t MAX_SEARCH_RESULTS = 1000;
  static constexpr int SEARCH_DEBOUNCE_MS = 250;

  inline static const std::unordered_set<AssetType> IGNORED_ASSET_TYPES = {
    AssetType::Auxiliary,
    AssetType::Unknown,
    AssetType::Directory,
    AssetType::Document
  };

  static constexpr int SVG_THUMBNAIL_SIZE = 240;
  static constexpr float MAX_THUMBNAIL_UPSCALE_FACTOR = 2.0f;
  static constexpr float MAX_PREVIEW_UPSCALE_FACTOR = 20.0f;

  // =============================================================================
  // FILE SYSTEM & MONITORING
  // =============================================================================

  static constexpr int FILE_WATCHER_DEBOUNCE_MS = 50;
  static constexpr int MAX_ASSET_CREATION_RETRIES = 3;

  static inline constexpr const char* CONFIG_KEY_ASSETS_DIRECTORY = "assets_directory";
  static inline constexpr const char* CONFIG_KEY_DRAW_DEBUG_AXES = "draw_debug_axes";
  static constexpr bool CONFIG_DEFAULT_DRAW_DEBUG_AXES = PREVIEW_DRAW_DEBUG_AXES_DEFAULT;

  static constexpr const char* DATABASE_PATH = "db/assets.db";
  static constexpr const char* THUMBNAIL_DIRECTORY = "thumbnails";

  // =============================================================================
  // PATH UTILITIES
  // =============================================================================

  static std::filesystem::path get_data_directory() {
    if (std::getenv("TESTING")) {
      return "build/data";
    }

  #ifdef _WIN32
    const char* localappdata = std::getenv("LOCALAPPDATA");
    if (localappdata) {
      return std::filesystem::path(localappdata) / "AssetInventory";
    }
    return "data";
  #elif __APPLE__
    const char* home = std::getenv("HOME");
    if (home) {
      return std::filesystem::path(home) / "Library" / "Application Support" / "AssetInventory";
    }
    return "data";
  #else
    return "data";
  #endif
  }

  static std::filesystem::path get_thumbnail_directory() {
    return get_data_directory() / THUMBNAIL_DIRECTORY;
  }

  static std::filesystem::path get_database_path() {
    return get_data_directory() / "assets.db";
  }

  static void initialize_directories() {
    std::filesystem::path data_dir = get_data_directory();
    if (!std::filesystem::exists(data_dir)) {
      std::filesystem::create_directories(data_dir);
    }

    std::filesystem::path db_dir = data_dir;
    if (!std::filesystem::exists(db_dir)) {
      std::filesystem::create_directories(db_dir);
    }

    std::filesystem::path thumbnail_dir = get_thumbnail_directory();
    if (!std::filesystem::exists(thumbnail_dir)) {
      std::filesystem::create_directories(thumbnail_dir);
    }
  }

  // =============================================================================
  // RUNTIME CONFIGURATION
  // =============================================================================

  static bool initialize(AssetDatabase* database);
  static const std::string& assets_directory();
  static bool draw_debug_axes();
  static bool set_assets_directory(const std::string& path);
  static bool set_draw_debug_axes(bool enabled);

private:
  static std::string load_string_setting(const std::string& key, const std::string& default_value);
  static bool load_bool_setting(const std::string& key, bool default_value);
  static bool persist_value(const std::string& key, const std::string& value);

  static AssetDatabase* database_;
  static bool initialized_;
  static std::string assets_directory_value_;
  static bool draw_debug_axes_value_;
};
