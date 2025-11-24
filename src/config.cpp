#include "config.h"

#include <algorithm>
#include <cctype>

#include "database.h"
#include "logger.h"

namespace {
std::string bool_to_string(bool value) {
  return value ? "true" : "false";
}

bool string_to_bool(const std::string& value, bool default_value) {
  if (value == "true") {
    return true;
  }
  if (value == "false") {
    return false;
  }
  return default_value;
}
}

AssetDatabase* Config::database_ = nullptr;
bool Config::initialized_ = false;
std::string Config::assets_directory_value_;
bool Config::draw_debug_axes_value_ = Config::CONFIG_DEFAULT_DRAW_DEBUG_AXES;

bool Config::initialize(AssetDatabase* database) {
  database_ = database;
  if (!database_) {
    LOG_ERROR("Config requires a valid database instance");
    return false;
  }

  assets_directory_value_ = load_string_setting(CONFIG_KEY_ASSETS_DIRECTORY, "");
  draw_debug_axes_value_ = load_bool_setting(CONFIG_KEY_DRAW_DEBUG_AXES,
    CONFIG_DEFAULT_DRAW_DEBUG_AXES);
  initialized_ = true;
  return true;
}

const std::string& Config::assets_directory() {
  return assets_directory_value_;
}

bool Config::draw_debug_axes() {
  return draw_debug_axes_value_;
}

bool Config::set_assets_directory(const std::string& path) {
  if (!persist_value(CONFIG_KEY_ASSETS_DIRECTORY, path)) {
    return false;
  }
  assets_directory_value_ = path;
  return true;
}

bool Config::set_draw_debug_axes(bool enabled) {
  if (!persist_value(CONFIG_KEY_DRAW_DEBUG_AXES, bool_to_string(enabled))) {
    return false;
  }
  draw_debug_axes_value_ = enabled;
  return true;
}

std::string Config::load_string_setting(const std::string& key, const std::string& default_value) {
  if (!database_) {
    return default_value;
  }

  std::string value;
  if (database_->try_get_config_value(key, value)) {
    return value;
  }

  if (!database_->upsert_config_value(key, default_value)) {
    LOG_WARN("Failed to persist default config value for key {}", key);
  }
  return default_value;
}

bool Config::load_bool_setting(const std::string& key, bool default_value) {
  std::string stored = load_string_setting(key, bool_to_string(default_value));
  return string_to_bool(stored, default_value);
}

bool Config::persist_value(const std::string& key, const std::string& value) {
  if (!database_) {
    return false;
  }

  if (!database_->upsert_config_value(key, value)) {
    LOG_WARN("Failed to persist config value for key {}", key);
    return false;
  }
  return true;
}
