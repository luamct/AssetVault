#pragma once

#include <unordered_set>
#include <filesystem>
#include <string>
#include <cstdlib>

#include "asset.h"

class AssetDatabase;

class Config {
public:
  static constexpr bool PREVIEW_DRAW_DEBUG_AXES_DEFAULT = true;
  static constexpr bool PREVIEW_PLAY_ANIMATIONS = true;

  inline static const std::unordered_set<AssetType> IGNORED_ASSET_TYPES = {
    AssetType::Auxiliary,
    AssetType::Unknown,
    AssetType::Directory,
    AssetType::Document
  };

  static constexpr int MODEL_THUMBNAIL_SIZE = 400;
  static constexpr float ICON_SCALE = 0.5f;

  // =============================================================================
  // FILE SYSTEM & MONITORING
  // =============================================================================

  static constexpr int FILE_WATCHER_DEBOUNCE_MS = 50;

  static inline constexpr const char* CONFIG_KEY_ASSETS_DIRECTORY = "assets_directory";
  static inline constexpr const char* CONFIG_KEY_DRAW_DEBUG_AXES = "draw_debug_axes";
  static inline constexpr const char* CONFIG_KEY_GRID_ZOOM_LEVEL = "grid_zoom_level";
  static inline constexpr const char* CONFIG_KEY_PREVIEW_PROJECTION = "preview_projection";
  static constexpr bool CONFIG_DEFAULT_DRAW_DEBUG_AXES = PREVIEW_DRAW_DEBUG_AXES_DEFAULT;
  static constexpr int CONFIG_DEFAULT_GRID_ZOOM_LEVEL = 3;
  static inline constexpr const char* CONFIG_VALUE_PROJECTION_ORTHOGRAPHIC = "orthographic";
  static inline constexpr const char* CONFIG_VALUE_PROJECTION_PERSPECTIVE = "perspective";

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
      return std::filesystem::path(localappdata) / "AssetVault";
    }
    return "data";
  #elif __APPLE__
    const char* home = std::getenv("HOME");
    if (home) {
      return std::filesystem::path(home) / "Library" / "Application Support" / "AssetVault";
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
  static int grid_zoom_level();
  static std::string preview_projection();
  static bool set_assets_directory(const std::string& path);
  static bool set_draw_debug_axes(bool enabled);
  static bool set_grid_zoom_level(int level);
  static bool set_preview_projection(const std::string& projection_value);

private:
  static std::string load_string_setting(const std::string& key, const std::string& default_value);
  static bool load_bool_setting(const std::string& key, bool default_value);
  static int load_int_setting(const std::string& key, int default_value);
  static bool persist_value(const std::string& key, const std::string& value);

  static AssetDatabase* database_;
  static bool initialized_;
  static std::string assets_directory_value_;
  static bool draw_debug_axes_value_;
  static int grid_zoom_level_value_;
  static std::string preview_projection_value_;
};
