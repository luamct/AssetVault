#include "utils.h"
#include "config.h"
#include "asset.h"
#include "logger.h"
#include "3d.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

std::string truncate_with_ellipsis(const std::string& text, size_t max_length) {
  if (text.length() <= max_length || max_length <= 3) {
    return text;
  }
  return text.substr(0, max_length - 3) + "...";
}

// Function to convert string to lowercase for case-insensitive search
std::string to_lowercase(const std::string& str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(),
    [](unsigned char c) { return std::tolower(c); });
  return result;
}

// Function to trim leading and trailing whitespace from string
std::string trim_string(const std::string& str) {
  if (str.empty()) {
    return str;
  }

  std::string result = str;

  // Trim leading whitespace
  result.erase(result.begin(), std::find_if(result.begin(), result.end(), [](unsigned char ch) {
    return !std::isspace(ch);
  }));

  // Trim trailing whitespace
  result.erase(std::find_if(result.rbegin(), result.rend(), [](unsigned char ch) {
    return !std::isspace(ch);
  }).base(), result.end());

  return result;
}

// Function to normalize path separators to forward slashes
std::string normalize_path_separators(const std::string& path) {
  std::string normalized = path;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  return normalized;
}

// Function to get relative path from assets folder for display and search
// TODO: Remove normalize_path_separators calls
std::string get_relative_path(const std::string& full_path, const std::string& assets_directory) {
  std::string normalized_full_path = normalize_path_separators(full_path);
  std::string root_path = normalize_path_separators(assets_directory);

  // Ensure root path ends with slash for proper comparison
  if (!root_path.empty() && root_path.back() != '/') {
    root_path += '/';
  }

  // Check if full path starts with root path
  if (normalized_full_path.length() >= root_path.length() &&
    normalized_full_path.substr(0, root_path.length()) == root_path) {
    // Return substring after the root path
    return normalized_full_path.substr(root_path.length());
  }

  // Fallback: return normalized full path if not under root
  LOG_WARN("Asset path should contain configured root path: {}", full_path);
  return normalized_full_path;
}

std::string format_file_size(uint64_t size_bytes) {
  if (size_bytes >= 1024 * 1024) {
    // Convert to MB
    double size_mb = static_cast<double>(size_bytes) / (1024.0 * 1024.0);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.1f MB", size_mb);
    return std::string(buffer);
  }
  else if (size_bytes >= 1024) {
    // Convert to KB
    double size_kb = static_cast<double>(size_bytes) / 1024.0;
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.1f KB", size_kb);
    return std::string(buffer);
  }
  else {
    // Keep as bytes
    return std::to_string(size_bytes) + " bytes";
  }
}

std::string get_home_directory() {
#ifdef _WIN32
  if (const char* user_profile = std::getenv("USERPROFILE")) {
    if (user_profile[0] != '\0') {
      return std::filesystem::path(user_profile).u8string();
    }
  }
#endif

  if (const char* home_env = std::getenv("HOME")) {
    if (home_env[0] != '\0') {
      return std::filesystem::path(home_env).u8string();
    }
  }

#ifdef _WIN32
  const char* home_drive = std::getenv("HOMEDRIVE");
  const char* home_path = std::getenv("HOMEPATH");
  if (home_drive && home_path) {
    std::filesystem::path combined = std::filesystem::path(home_drive) / home_path;
    return combined.u8string();
  }
#endif

  return std::filesystem::current_path().u8string();
}

std::filesystem::path get_thumbnail_path(const std::string& relative_path) {
  std::filesystem::path path_with_source_extension(relative_path);
  path_with_source_extension += ".png";
  return Config::get_thumbnail_directory() / path_with_source_extension;
}

void ensure_executable_working_directory() {
#ifdef _WIN32
  wchar_t path_buffer[MAX_PATH] = L"";
  DWORD length = GetModuleFileNameW(nullptr, path_buffer, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return;
  }

  fs::path exe_path(path_buffer);
  fs::path exe_dir = exe_path.parent_path();
  if (exe_dir.empty()) {
    return;
  }

  std::error_code ec;
  fs::current_path(exe_dir, ec);
  if (ec) {
    LOG_WARN("Failed to switch working directory to executable directory: {}", exe_dir.u8string());
  }
#else
  // Non-Windows platforms already run from a predictable working directory.
#endif
}

std::vector<std::filesystem::path> list_root_directories() {
  std::vector<std::filesystem::path> roots;
#ifdef _WIN32
  DWORD size = GetLogicalDriveStringsW(0, nullptr);
  if (size == 0) {
    return roots;
  }

  std::wstring buffer(size + 1, L'\0');
  DWORD written = GetLogicalDriveStringsW(static_cast<DWORD>(buffer.size()), buffer.data());
  if (written == 0) {
    return roots;
  }

  const wchar_t* drive = buffer.c_str();
  while (*drive != L'\0') {
    roots.emplace_back(drive);
    drive += wcslen(drive) + 1;
  }
#else
  roots.emplace_back(std::filesystem::path("/"));
#endif
  return roots;
}

void safe_localtime(std::tm* tm_buf, const std::time_t* time) {
#ifdef _WIN32
  localtime_s(tm_buf, time);
#else
  localtime_r(time, tm_buf);
#endif
}

void safe_strcpy(char* dest, size_t dest_size, const char* src) {
#ifdef _WIN32
  strcpy_s(dest, dest_size, src);
#else
  strncpy(dest, src, dest_size - 1);
  dest[dest_size - 1] = '\0';  // Ensure null termination
#endif
}

// Efficiently find all assets under a directory path using binary search O(log n + k)
// where k is the number of assets found under the directory
std::vector<std::filesystem::path> find_assets_under_directory(const std::map<std::string, Asset>& assets, const std::filesystem::path& dir_path) {
  std::vector<std::filesystem::path> result;

  if (assets.empty()) {
    return result;
  }

  // Convert directory path to string and normalize to forward slashes for consistent matching
  std::string dir_path_str = dir_path.generic_u8string();
  if (!dir_path_str.empty() && dir_path_str.back() != '/') {
    dir_path_str += '/';
  }

  // Use binary search to find the first asset with path >= dir_path_str
  auto it = assets.lower_bound(dir_path_str);

  // Iterate through all assets that start with dir_path_str (child assets)
  while (it != assets.end()) {
    const std::string& asset_path = it->first;

    // Normalize asset path for comparison to handle different path separators
    std::string normalized_asset_path = normalize_path_separators(asset_path);

    // Check if this asset is still under the directory
    if (normalized_asset_path.substr(0, dir_path_str.length()) != dir_path_str) {
      break;  // No more assets under this directory
    }

    // Add this asset to the result
    result.push_back(fs::u8path(asset_path));
    ++it;
  }

  return result;
}

void clear_all_thumbnails() {
  LOG_WARN("DEBUG_FORCE_THUMBNAIL_CLEAR is enabled - deleting all thumbnails for debugging...");

  // Use proper cross-platform thumbnail directory
  fs::path thumbnail_dir = Config::get_thumbnail_directory();
  LOG_INFO("Using thumbnail directory: {}", thumbnail_dir.string());

  try {
    if (fs::exists(thumbnail_dir)) {
      fs::remove_all(thumbnail_dir);
      LOG_INFO("All thumbnails deleted successfully from: {}", thumbnail_dir.string());
    }
    else {
      LOG_INFO("Thumbnails directory does not exist yet: {}", thumbnail_dir.string());
    }
  }
  catch (const fs::filesystem_error& e) {
    LOG_ERROR("Failed to delete thumbnails: {}", e.what());
  }
}

std::vector<std::string> find_related_files(const Asset& asset) {
  std::vector<std::string> related_files;

  // Always include the main file
  related_files.push_back(asset.path);

  fs::path asset_path = fs::u8path(asset.path);
  fs::path parent_dir = asset_path.parent_path();
  std::string extension = to_lowercase(asset.extension);
  std::string stem = asset_path.stem().string();

  // For OBJ files, include the corresponding MTL file if it exists
  if (extension == ".obj") {
    fs::path mtl_path = parent_dir / (stem + ".mtl");
    if (fs::exists(mtl_path)) {
      related_files.push_back(mtl_path.string());
    }
  }

  // For 3D models, extract texture paths from the model file itself
  if (asset.type == AssetType::_3D) {
    // Use 3d.cpp function to extract actual texture paths referenced by the model
    std::vector<std::string> texture_paths = extract_model_texture_paths(asset.path);

    // Collect unique top-level directories/files to include
    std::set<std::string> top_level_items;

    for (const auto& tex_path : texture_paths) {
      fs::path rel_path = fs::u8path(tex_path);

      // Get the first component of the path (top-level directory or file)
      auto it = rel_path.begin();
      if (it != rel_path.end()) {
        std::string top_level = it->string();

        // Build absolute path for the top-level item
        fs::path full_path = parent_dir / top_level;

        if (fs::exists(full_path)) {
          top_level_items.insert(full_path.string());
        }
      }
    }

    // Add all unique top-level items
    for (const auto& item : top_level_items) {
      related_files.push_back(item);
    }
  }

  return related_files;
}
