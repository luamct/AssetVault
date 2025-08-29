#include "utils.h"
#include "config.h"
#include "asset.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <vector>

namespace fs = std::filesystem;

// Function to truncate filename to specified length with ellipsis
std::string truncate_filename(const std::string& filename, size_t max_length) {
  if (filename.length() <= max_length) {
    return filename;
  }
  return filename.substr(0, max_length - 3) + "...";
}

// Function to convert string to lowercase for case-insensitive search
std::string to_lowercase(const std::string& str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}

// Function to normalize path separators to forward slashes
std::string normalize_path_separators(const std::string& path) {
  std::string normalized = path;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  return normalized;
}

// Function to get relative path from assets folder for display and search
std::string get_relative_asset_path(const std::string& full_path) {
  namespace fs = std::filesystem;
  
  try {
    fs::path full_fs_path = fs::u8path(full_path);
    fs::path assets_root = fs::u8path(Config::ASSET_ROOT_DIRECTORY);
    
    // Make both paths absolute for proper comparison
    full_fs_path = fs::absolute(full_fs_path);
    assets_root = fs::absolute(assets_root);
    
    // Get relative path from assets root
    fs::path relative_path = fs::relative(full_fs_path, assets_root);
    
    // Normalize to forward slashes
    return normalize_path_separators(relative_path.u8string());
  }
  catch (const fs::filesystem_error&) {
    // Fallback: just normalize separators in the original path
    return normalize_path_separators(full_path);
  }
}

// Function to format path for display (remove everything before first / and convert backslashes)
std::string format_display_path(const std::string& full_path) {
  // Get relative path from assets folder
  std::string display_path = get_relative_asset_path(full_path);

  // Make path wrappable by adding spaces around slashes
  size_t pos = 0;
  while ((pos = display_path.find('/', pos)) != std::string::npos) {
    display_path.replace(pos, 1, " / ");
    pos += 3; // Move past the " / "
  }

  return display_path;
}

std::string format_file_size(uint64_t size_bytes) {
  if (size_bytes >= 1024 * 1024) {
    // Convert to MB
    double size_mb = static_cast<double>(size_bytes) / (1024.0 * 1024.0);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.1f MB", size_mb);
    return std::string(buffer);
  } else if (size_bytes >= 1024) {
    // Convert to KB
    double size_kb = static_cast<double>(size_bytes) / 1024.0;
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.1f KB", size_kb);
    return std::string(buffer);
  } else {
    // Keep as bytes
    return std::to_string(size_bytes) + " bytes";
  }
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
  std::string dir_path_str = normalize_path_separators(dir_path.u8string());
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
