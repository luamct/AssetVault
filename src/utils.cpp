#include "utils.h"
#include "config.h"

#include <algorithm>
#include <cstdio>
#include <filesystem> // Added for std::filesystem::current_path()

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
